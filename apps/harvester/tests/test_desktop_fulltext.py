import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

import httpx
import httpcore

from research_harvester.desktop_fulltext import (
    FullTextDiscoveryRequest,
    FullTextCandidate,
    FullTextDownloadError,
    MAX_FULLTEXT_CANDIDATES,
    _PinnedPublicNetworkBackend,
    desktop_papers_directory,
    discover_open_access_candidates,
    download_selected_pdf,
    normalize_candidates,
)


PUBLIC_RESOLVER = lambda host: ("93.184.216.34",)


class DesktopFullTextCandidateTests(unittest.TestCase):
    def test_normalization_keeps_only_safe_unique_https_candidates(self):
        candidates = normalize_candidates(
            (
                {
                    "source": "OpenAlex",
                    "pdf_url": "https://repository.example/papers/one.pdf#page=1",
                    "url": "https://repository.example/record/one",
                    "license": "CC-BY-4.0",
                    "version": "publishedVersion",
                    "host_type": "repository",
                    "repository": "Example Repository",
                },
                {
                    "source": "Duplicate",
                    "pdf_url": "https://repository.example/papers/one.pdf",
                    "url": "https://repository.example/record/one",
                },
                {"source": "Unsafe", "pdf_url": "http://repository.example/paper.pdf"},
                {"source": "Unsafe", "pdf_url": "https://user@repository.example/paper.pdf"},
                {"source": "Unsafe", "pdf_url": "https://127.0.0.1/paper.pdf"},
            )
        )

        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0].url, "https://repository.example/papers/one.pdf")
        self.assertTrue(candidates[0].direct_pdf)
        self.assertEqual(candidates[0].license, "CC-BY-4.0")
        self.assertEqual(candidates[0].repository, "Example Repository")

    def test_normalization_rejects_ip_literals_and_uses_the_xdg_data_directory(self):
        candidates = normalize_candidates(
            (
                {"source": "Unsafe", "pdf_url": "https://93.184.216.34/paper.pdf"},
                {"source": "Unsafe", "pdf_url": "https://[2606:2800:220:1:248:1893:25c8:1946]/paper.pdf"},
            )
        )

        self.assertEqual(candidates, ())
        self.assertEqual(
            desktop_papers_directory({"XDG_DATA_HOME": "/tmp/harvester-data"}),
            Path("/tmp/harvester-data/research-harvester/papers"),
        )

    def test_normalization_uses_landing_url_when_a_direct_pdf_is_not_declared(self):
        candidates = normalize_candidates(
            (
                {
                    "source": "DOI",
                    "pdf_url": None,
                    "url": "https://doi.org/10.1000/example",
                    "host_type": "publisher",
                },
            )
        )

        self.assertEqual(
            candidates,
            (
                FullTextCandidate(
                    source="DOI",
                    url="https://doi.org/10.1000/example",
                    direct_pdf=False,
                    license=None,
                    version=None,
                    host_type="publisher",
                    repository=None,
                ),
            ),
        )


class DesktopFullTextDiscoveryTests(unittest.IsolatedAsyncioTestCase):
    async def test_discovery_caps_normalized_candidates_before_the_worker_protocol(self):
        provider_records = [
            {
                "source": "OpenAlex",
                "pdf_url": f"https://repository.example/{index}.pdf",
            }
            for index in range(MAX_FULLTEXT_CANDIDATES + 1)
        ]
        request = FullTextDiscoveryRequest(
            title="Example work",
            doi="10.1000/example",
            open_access_url=None,
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(lambda request: None)) as client:
            with (
                patch(
                    "research_harvester.desktop_fulltext._openalex_records",
                    new=AsyncMock(return_value=provider_records),
                ),
                patch(
                    "research_harvester.desktop_fulltext._openaire_records",
                    new=AsyncMock(return_value=[]),
                ),
                patch(
                    "research_harvester.desktop_fulltext._europe_pmc_records",
                    new=AsyncMock(return_value=[]),
                ),
                patch(
                    "research_harvester.desktop_fulltext._zenodo_records",
                    new=AsyncMock(return_value=[]),
                ),
            ):
                result = await discover_open_access_candidates(request, client)

        self.assertEqual(len(result.candidates), MAX_FULLTEXT_CANDIDATES)
        self.assertEqual(
            result.candidates[-1].url,
            f"https://repository.example/{MAX_FULLTEXT_CANDIDATES - 1}.pdf",
        )

    async def test_discovery_combines_safe_candidates_from_all_public_providers(self):
        def handler(request: httpx.Request) -> httpx.Response:
            if request.url.host == "api.openalex.org":
                self.assertEqual(request.url.params["filter"], "doi:10.1000/example")
                return httpx.Response(
                    200,
                    json={
                        "results": [
                            {
                                "locations": [
                                    {
                                        "is_oa": True,
                                        "pdf_url": "https://repository.example/openalex.pdf",
                                        "landing_page_url": "https://repository.example/record",
                                        "license": "cc-by",
                                        "version": "submittedVersion",
                                        "source": {
                                            "type": "repository",
                                            "display_name": "Example Repository",
                                        },
                                    },
                                    {"is_oa": True, "pdf_url": "http://unsafe.example/file.pdf"},
                                ]
                            }
                        ]
                    },
                )
            if request.url.host == "api.openaire.eu":
                self.assertEqual(request.url.path, "/graph/v3/research-products")
                self.assertEqual(request.url.params["pid"], "10.1000/example")
                self.assertEqual(request.url.params["pageSize"], "5")
                return httpx.Response(
                    200,
                    json={
                        "results": [
                            {
                                "instances": [
                                    {
                                        "accessRight": {"label": "Open Access"},
                                        "urls": [
                                            "https://openaire.example/paper.pdf",
                                            "https://openaire.example/record",
                                            "https://[malformed.example/paper.pdf",
                                        ],
                                        "license": "CC-BY-4.0",
                                        "version": "acceptedVersion",
                                    },
                                    {
                                        "accessRight": {"label": "Closed Access"},
                                        "url": "https://openaire.example/closed.pdf",
                                    },
                                ]
                            }
                        ]
                    },
                )
            if request.url.host == "www.ebi.ac.uk":
                self.assertEqual(request.url.path, "/europepmc/webservices/rest/search")
                self.assertEqual(request.url.params["query"], 'DOI:"10.1000/example"')
                self.assertEqual(request.url.params["format"], "json")
                self.assertEqual(request.url.params["resultType"], "core")
                return httpx.Response(
                    200,
                    json={
                        "resultList": {
                            "result": [
                                {
                                    "doi": "10.1000/Example",
                                    "isOpenAccess": "Y",
                                    "license": "CC BY 4.0",
                                    "pmcid": "PMC123456",
                                    "fullTextUrlList": {
                                        "fullTextUrl": [
                                            {
                                                "url": "https://europepmc.example/file",
                                                "documentStyle": "pdf",
                                            },
                                            {
                                                "url": "https://europepmc.example/article",
                                                "documentStyle": "html",
                                            },
                                        ]
                                    },
                                },
                                {
                                    "doi": "10.1000/other",
                                    "isOpenAccess": "Y",
                                    "pmcid": "PMC999999",
                                },
                                {
                                    "doi": "10.1000/example",
                                    "isOpenAccess": "N",
                                    "pmcid": "PMC000000",
                                },
                            ]
                        }
                    },
                )
            if request.url.host == "zenodo.org":
                self.assertEqual(request.url.path, "/api/records")
                self.assertEqual(request.url.params["q"], '"10.1000/example"')
                self.assertEqual(request.url.params["size"], "10")
                return httpx.Response(
                    200,
                    json={
                        "hits": {
                            "hits": [
                                {
                                    "pids": {"doi": {"identifier": "10.1000/Example"}},
                                    "access": {"record": "public"},
                                    "metadata": {
                                        "license": {"title": "CC-BY-4.0"},
                                        "version": "2.0",
                                    },
                                    "files": {
                                        "entries": {
                                            "article.pdf": {
                                                "mimetype": "application/pdf",
                                                "links": {
                                                    "content": "https://zenodo.example/current.pdf"
                                                },
                                            },
                                            "data.csv": {
                                                "mimetype": "text/csv",
                                                "links": {
                                                    "content": "https://zenodo.example/data.csv"
                                                },
                                            },
                                        }
                                    },
                                },
                                {
                                    "metadata": {
                                        "doi": "10.1000/example",
                                        "access_right": "open",
                                        "license": "CC0-1.0",
                                    },
                                    "files": [
                                        {
                                            "key": "legacy.pdf",
                                            "links": {
                                                "download": "https://zenodo.example/legacy.pdf"
                                            },
                                        }
                                    ],
                                },
                                {
                                    "metadata": {
                                        "doi": "10.1000/other",
                                        "access_right": "open",
                                    },
                                    "files": [
                                        {
                                            "key": "wrong.pdf",
                                            "links": {
                                                "download": "https://zenodo.example/wrong.pdf"
                                            },
                                        }
                                    ],
                                },
                                {
                                    "metadata": {
                                        "doi": "10.1000/example",
                                        "access_right": "restricted",
                                    },
                                    "files": [
                                        {
                                            "key": "closed.pdf",
                                            "links": {
                                                "download": "https://zenodo.example/closed.pdf"
                                            },
                                        }
                                    ],
                                },
                            ]
                        }
                    },
                )
            self.fail(f"unexpected provider host: {request.url.host}")

        request = FullTextDiscoveryRequest(
            title="Example work",
            doi="https://doi.org/10.1000/Example",
            open_access_url="https://publisher.example/article.pdf",
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
            result = await discover_open_access_candidates(request, client)

        self.assertEqual(result.failed_sources, ())
        self.assertEqual(
            [candidate.source for candidate in result.candidates],
            [
                "Source metadata",
                "OpenAlex",
                "OpenAIRE",
                "OpenAIRE",
                "Europe PMC",
                "Europe PMC",
                "Europe PMC",
                "Zenodo",
                "Zenodo",
            ],
        )
        self.assertEqual(result.candidates[0].url, "https://publisher.example/article.pdf")
        self.assertEqual(result.candidates[1].license, "cc-by")
        self.assertTrue(result.candidates[2].direct_pdf)
        self.assertFalse(result.candidates[3].direct_pdf)
        self.assertEqual(result.candidates[2].license, "CC-BY-4.0")
        self.assertTrue(result.candidates[4].direct_pdf)
        self.assertFalse(result.candidates[5].direct_pdf)
        self.assertEqual(result.candidates[6].url, "https://europepmc.org/articles/PMC123456")
        self.assertEqual(result.candidates[7].license, "CC-BY-4.0")
        self.assertEqual(result.candidates[8].url, "https://zenodo.example/legacy.pdf")
        self.assertNotIn("config", __import__("research_harvester.desktop_fulltext").desktop_fulltext.__dict__)

    async def test_discovery_keeps_source_metadata_when_providers_fail_without_raw_errors(self):
        def handler(request: httpx.Request) -> httpx.Response:
            del request
            return httpx.Response(503)

        request = FullTextDiscoveryRequest(
            title="Example work",
            doi="10.1000/example",
            open_access_url="https://publisher.example/article.pdf",
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
            result = await discover_open_access_candidates(request, client)

        self.assertEqual([candidate.source for candidate in result.candidates], ["Source metadata"])
        self.assertEqual(
            result.failed_sources,
            ("OpenAlex", "OpenAIRE", "Europe PMC", "Zenodo"),
        )

    async def test_discovery_retains_other_provider_results_when_one_provider_fails(self):
        def handler(request: httpx.Request) -> httpx.Response:
            if request.url.host == "api.openaire.eu":
                return httpx.Response(503)
            if request.url.host == "api.openalex.org":
                return httpx.Response(200, json={"results": []})
            if request.url.host == "www.ebi.ac.uk":
                return httpx.Response(200, json={"resultList": {"result": []}})
            if request.url.host == "zenodo.org":
                return httpx.Response(200, json={"hits": {"hits": []}})
            self.fail(f"unexpected provider host: {request.url.host}")

        request = FullTextDiscoveryRequest(
            title="Example work",
            doi="10.1000/example",
            open_access_url="https://publisher.example/article.pdf",
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
            result = await discover_open_access_candidates(request, client)

        self.assertEqual([candidate.source for candidate in result.candidates], ["Source metadata"])
        self.assertEqual(result.failed_sources, ("OpenAIRE",))

    async def test_discovery_reports_an_unexpected_provider_failure_without_details(self):
        request = FullTextDiscoveryRequest(
            title="Example work",
            doi="10.1000/example",
            open_access_url=None,
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(lambda request: None)) as client:
            with (
                patch(
                    "research_harvester.desktop_fulltext._openalex_records",
                    new=AsyncMock(return_value=[]),
                ),
                patch(
                    "research_harvester.desktop_fulltext._openaire_records",
                    new=AsyncMock(side_effect=RuntimeError("provider detail")),
                ),
                patch(
                    "research_harvester.desktop_fulltext._europe_pmc_records",
                    new=AsyncMock(return_value=[]),
                ),
                patch(
                    "research_harvester.desktop_fulltext._zenodo_records",
                    new=AsyncMock(return_value=[]),
                ),
            ):
                result = await discover_open_access_candidates(request, client)

        self.assertEqual(result.candidates, ())
        self.assertEqual(result.failed_sources, ("OpenAIRE",))


class DesktopFullTextDownloadTests(unittest.IsolatedAsyncioTestCase):
    async def test_pinned_backend_rejects_private_addresses_and_pins_public_ones(self):
        class RecordingBackend(httpcore.AsyncNetworkBackend):
            def __init__(self):
                self.host = None

            async def connect_tcp(self, host, port, **kwargs):
                del port, kwargs
                self.host = host
                return object()

        private_backend = _PinnedPublicNetworkBackend(RecordingBackend())
        with patch(
            "research_harvester.desktop_fulltext.socket.getaddrinfo",
            return_value=[(None, None, None, None, ("127.0.0.1", 443))],
        ):
            with self.assertRaises(httpcore.ConnectError):
                await private_backend.connect_tcp("repository.example", 443)

        recording_backend = RecordingBackend()
        public_backend = _PinnedPublicNetworkBackend(recording_backend)
        with patch(
            "research_harvester.desktop_fulltext.socket.getaddrinfo",
            return_value=[(None, None, None, None, ("93.184.216.34", 443))],
        ):
            await public_backend.connect_tcp("repository.example", 443)
        self.assertEqual(recording_backend.host, "93.184.216.34")

    async def test_download_follows_a_validated_redirect_and_writes_atomically(self):
        payload = b"%PDF-1.7\nresearch paper\n"

        def handler(request: httpx.Request) -> httpx.Response:
            if request.url.path == "/record":
                return httpx.Response(302, headers={"location": "/paper.pdf"})
            if request.url.path == "/paper.pdf":
                return httpx.Response(200, content=payload)
            return httpx.Response(404)

        candidate = FullTextCandidate(
            source="OpenAlex",
            url="https://repository.example/record",
            direct_pdf=False,
            license="CC-BY-4.0",
            version="acceptedVersion",
            host_type="repository",
            repository="Example Repository",
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
            with tempfile.TemporaryDirectory() as temporary_directory:
                result = await download_selected_pdf(
                    client,
                    candidate,
                    title="A / risky: paper title",
                    papers_directory=Path(temporary_directory),
                    resolve_host=PUBLIC_RESOLVER,
                )
                saved = Path(result.path)

                self.assertEqual(saved.parent, Path(temporary_directory))
                self.assertTrue(saved.name.startswith("a-risky-paper-title-"))
                self.assertEqual(saved.read_bytes(), payload)
                self.assertEqual(result.byte_count, len(payload))
                self.assertEqual(result.sha256, hashlib.sha256(payload).hexdigest())
                self.assertEqual(list(Path(temporary_directory).glob("*.tmp")), [])

    async def test_download_rejects_private_redirect_before_writing_a_file(self):
        def handler(request: httpx.Request) -> httpx.Response:
            return httpx.Response(302, headers={"location": "https://127.0.0.1/paper.pdf"})

        candidate = FullTextCandidate(
            source="OpenAlex",
            url="https://repository.example/record",
            direct_pdf=False,
            license=None,
            version=None,
            host_type="repository",
            repository="Example Repository",
        )
        async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
            with tempfile.TemporaryDirectory() as temporary_directory:
                with self.assertRaisesRegex(FullTextDownloadError, "unsafe"):
                    await download_selected_pdf(
                        client,
                        candidate,
                        title="paper",
                        papers_directory=Path(temporary_directory),
                        resolve_host=PUBLIC_RESOLVER,
                    )
                self.assertEqual(list(Path(temporary_directory).iterdir()), [])

    async def test_download_rejects_non_pdf_and_oversize_bodies_without_partial_output(self):
        candidate = FullTextCandidate(
            source="OpenAlex",
            url="https://repository.example/paper",
            direct_pdf=True,
            license=None,
            version=None,
            host_type="repository",
            repository="Example Repository",
        )

        for payload, maximum in ((b"not a PDF", 1024), (b"%PDF-1.7" + b"x" * 32, 12)):
            def handler(request: httpx.Request, response_payload=payload) -> httpx.Response:
                del request
                return httpx.Response(200, content=response_payload)

            async with httpx.AsyncClient(transport=httpx.MockTransport(handler)) as client:
                with tempfile.TemporaryDirectory() as temporary_directory:
                    with self.assertRaises(FullTextDownloadError):
                        await download_selected_pdf(
                            client,
                            candidate,
                            title="paper",
                            papers_directory=Path(temporary_directory),
                            resolve_host=PUBLIC_RESOLVER,
                            maximum_bytes=maximum,
                        )
                    self.assertEqual(list(Path(temporary_directory).iterdir()), [])


if __name__ == "__main__":
    unittest.main()
