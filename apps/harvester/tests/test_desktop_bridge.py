import asyncio
import base64
import io
import unittest
from unittest.mock import patch

from research_harvester.desktop_bridge import (
    BridgeSearchResult,
    PROTOCOL_HEADER,
    collect_search,
    main,
    render_download_success,
    render_fulltext_discovery_success,
    render_failure,
    render_success,
    validate_request,
)
from research_harvester.desktop_fulltext import (
    DownloadedPdf,
    FullTextCandidate,
    FullTextDiscoveryResult,
    MAX_FULLTEXT_CANDIDATES,
)
from research_harvester.models import WorkCandidate


class DesktopBridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_collect_search_uses_only_the_selected_sources(self):
        calls: list[str] = []

        async def openalex_search(client, query, limit):
            del client
            calls.append(f"openalex:{query}:{limit}")
            return [
                WorkCandidate(
                    source="OpenAlex",
                    source_id="https://openalex.org/W1",
                    title="Türkçe başlık",
                    authors=["Ada Lovelace"],
                    year=2026,
                    citation_count=4,
                )
            ]

        async def crossref_search(client, query, limit):
            del client, query, limit
            calls.append("crossref")
            raise RuntimeError("network failure")

        with patch(
            "research_harvester.desktop_bridge.SOURCE_SEARCHES",
            {"openalex": openalex_search, "crossref": crossref_search},
        ):
            result = await collect_search(
                query="graph rendering",
                limit=7,
                sources=("openalex", "crossref"),
            )

        self.assertEqual(calls, ["openalex:graph rendering:7", "crossref"])
        self.assertEqual([work.title for work in result.works], ["Türkçe başlık"])
        self.assertEqual(result.failed_sources, ("Crossref",))

    async def test_collect_search_preserves_an_empty_successful_result(self):
        async def empty_search(client, query, limit):
            del client, query, limit
            return []

        with patch(
            "research_harvester.desktop_bridge.SOURCE_SEARCHES",
            {"openalex": empty_search},
        ):
            result = await collect_search(
                query="no matches",
                limit=1,
                sources=("openalex",),
            )

        self.assertEqual(result, BridgeSearchResult(works=(), failed_sources=()))

    def test_validate_request_rejects_oversized_or_unsupported_input(self):
        with self.assertRaisesRegex(ValueError, "query"):
            validate_request(" ", 1, ("openalex",))
        with self.assertRaisesRegex(ValueError, "limit"):
            validate_request("valid", 101, ("openalex",))
        with self.assertRaisesRegex(ValueError, "source"):
            validate_request("valid", 1, ("unknown",))

    def test_protocol_is_ascii_safe_and_does_not_emit_raw_work_fields(self):
        result = BridgeSearchResult(
            works=(
                WorkCandidate(
                    source="OpenAlex",
                    source_id="work:1",
                    title="A\tUnicode Δ title",
                    authors=["Ada Lovelace", "İpek"],
                    doi="10.1000/example",
                    open_access_url="https://repository.example/paper.pdf",
                    year=2026,
                    citation_count=9,
                ),
            ),
            failed_sources=(),
        )

        lines = render_success(result).splitlines()

        self.assertEqual(lines[0], PROTOCOL_HEADER)
        self.assertEqual(lines[-1], "END")
        fields = lines[1].split("\t")
        self.assertEqual(fields[0], "WORK")
        self.assertEqual(base64.b64decode(fields[2]).decode("utf-8"), "A\tUnicode Δ title")
        self.assertEqual(base64.b64decode(fields[7]).decode("utf-8"), "10.1000/example")
        self.assertEqual(
            base64.b64decode(fields[8]).decode("utf-8"),
            "https://repository.example/paper.pdf",
        )
        self.assertNotIn("A\tUnicode Δ title", lines[1])

    def test_failure_protocol_is_single_line_and_base64_encoded(self):
        fields = render_failure("Search request failed.").split("\t")

        self.assertEqual(fields[0], "ERROR")
        self.assertEqual(
            base64.b64decode(fields[1]).decode("utf-8"),
            "Search request failed.",
        )

    def test_success_protocol_reports_partial_source_failures_without_details(self):
        result = BridgeSearchResult(
            works=(),
            failed_sources=("Crossref", "arXiv"),
        )

        fields = render_success(result).splitlines()[1].split("\t")

        self.assertEqual(fields[0], "NOTICE")
        self.assertEqual(
            base64.b64decode(fields[1]).decode("utf-8"),
            "Some selected sources did not respond: Crossref, arXiv.",
        )

    def test_fulltext_protocol_keeps_remote_url_out_of_ui_facing_text(self):
        output = render_fulltext_discovery_success(
            FullTextDiscoveryResult(
                candidates=(
                    FullTextCandidate(
                        source="OpenAlex",
                        url="https://repository.example/paper.pdf",
                        direct_pdf=True,
                        license="CC-BY-4.0",
                        version="acceptedVersion",
                        host_type="repository",
                        repository="Example Repository",
                    ),
                ),
                failed_sources=("OpenAlex mirror",),
            )
        )

        lines = output.splitlines()
        self.assertEqual(lines[0], PROTOCOL_HEADER)
        self.assertEqual(lines[1].split("\t")[0], "NOTICE")
        fields = lines[2].split("\t")
        self.assertEqual(fields[0], "CANDIDATE")
        self.assertEqual(base64.b64decode(fields[1]).decode("utf-8"), "https://repository.example/paper.pdf")
        self.assertEqual(fields[-1], "1")
        self.assertNotIn("https://repository.example/paper.pdf", lines[2])

    def test_fulltext_protocol_applies_the_candidate_output_bound_defensively(self):
        candidate = FullTextCandidate(
            source="OpenAlex",
            url="https://repository.example/paper.pdf",
            direct_pdf=True,
            license=None,
            version=None,
            host_type="repository",
            repository="Example Repository",
        )
        output = render_fulltext_discovery_success(
            FullTextDiscoveryResult(
                candidates=(candidate,) * (MAX_FULLTEXT_CANDIDATES + 1),
                failed_sources=(),
            )
        )

        self.assertEqual(
            sum(line.startswith("CANDIDATE\t") for line in output.splitlines()),
            MAX_FULLTEXT_CANDIDATES,
        )

    def test_worker_operations_use_typed_discovery_and_download_helpers(self):
        discovered = FullTextDiscoveryResult(
            candidates=(),
            failed_sources=(),
        )

        async def fake_discovery(request, client):
            del client
            self.assertEqual(request.doi, "10.1000/example")
            self.assertEqual(request.open_access_url, "https://repository.example/paper.pdf")
            return discovered

        async def fake_download(client, candidate, *, title):
            del client
            self.assertEqual(candidate.url, "https://repository.example/paper.pdf")
            self.assertEqual(title, "Example work")
            return DownloadedPdf(
                path="/safe/paper-a1b2c3d4e5f6.pdf",
                byte_count=42,
                sha256="a1b2c3d4e5f60718293a4b5c6d7e8f90123456789abcdef0123456789abcdef0",
            )

        with (
            patch("research_harvester.desktop_bridge.discover_open_access_candidates", fake_discovery),
            patch("sys.stdout", new_callable=io.StringIO) as stdout,
        ):
            self.assertEqual(
                main(
                    [
                        "--operation", "discover", "--title", "Example work",
                        "--doi", "10.1000/example", "--open-access-url",
                        "https://repository.example/paper.pdf",
                    ]
                ),
                0,
            )
            self.assertIn("HARVESTER_RESEARCH_V2", stdout.getvalue())

        with (
            patch("research_harvester.desktop_bridge.download_selected_pdf", fake_download),
            patch("sys.stdout", new_callable=io.StringIO) as stdout,
        ):
            self.assertEqual(
                main(
                    [
                        "--operation", "download", "--title", "Example work",
                        "--candidate-url", "https://repository.example/paper.pdf",
                        "--candidate-source", "OpenAlex",
                    ]
                ),
                0,
            )
            self.assertEqual(
                base64.b64decode(stdout.getvalue().splitlines()[1].split("\t")[1]).decode("utf-8"),
                "paper-a1b2c3d4e5f6.pdf",
            )


if __name__ == "__main__":
    unittest.main()
