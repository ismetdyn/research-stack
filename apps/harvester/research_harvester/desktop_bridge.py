"""Restricted stdio worker used by the native desktop application.

This module intentionally does not import the CLI, database, or configuration
module. A desktop search is read-only: it queries only the selected public
sources and returns a small, versioned protocol on stdout.
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import os
import sys
from collections.abc import Awaitable, Callable, Sequence
from dataclasses import dataclass

import httpx

from research_harvester.desktop_fulltext import (
    DownloadedPdf,
    FullTextDiscoveryRequest,
    FullTextDiscoveryResult,
    MAX_FULLTEXT_CANDIDATES,
    _PinnedPublicAsyncHttpTransport,
    download_selected_pdf,
    discover_open_access_candidates,
    normalize_candidates,
)
from research_harvester.models import WorkCandidate
from research_harvester.sources import arxiv, crossref, openalex


PROTOCOL_HEADER = "HARVESTER_RESEARCH_V2"
MAX_QUERY_CHARACTERS = 240
MAX_LIMIT = 100
MAX_WORKS = 300
MAX_FIELD_BYTES = 4096

SourceSearch = Callable[
    [httpx.AsyncClient, str, int], Awaitable[list[WorkCandidate]]
]


async def _search_openalex(
    client: httpx.AsyncClient,
    query: str,
    limit: int,
) -> list[WorkCandidate]:
    return await openalex.search(
        client,
        query,
        limit,
        os.getenv("OPENALEX_API_KEY") or None,
    )


async def _search_crossref(
    client: httpx.AsyncClient,
    query: str,
    limit: int,
) -> list[WorkCandidate]:
    return await crossref.search(
        client,
        query,
        limit,
        os.getenv("CROSSREF_MAILTO") or None,
    )


SOURCE_SEARCHES: dict[str, SourceSearch] = {
    "openalex": _search_openalex,
    "crossref": _search_crossref,
    "arxiv": arxiv.search,
}
SOURCE_DISPLAY_NAMES = {
    "openalex": "OpenAlex",
    "crossref": "Crossref",
    "arxiv": "arXiv",
}


@dataclass(frozen=True)
class BridgeSearchResult:
    works: tuple[WorkCandidate, ...]
    failed_sources: tuple[str, ...]


def validate_request(
    query: str,
    limit: int,
    sources: Sequence[str],
) -> tuple[str, ...]:
    """Validate the small native-to-worker request boundary."""
    if (
        not isinstance(query, str)
        or "\x00" in query
        or not query.strip()
        or len(query) > MAX_QUERY_CHARACTERS
    ):
        raise ValueError("query must contain 1 to 240 non-NUL characters")
    if not isinstance(limit, int) or isinstance(limit, bool) or not 1 <= limit <= MAX_LIMIT:
        raise ValueError("limit must be from 1 to 100")
    unique_sources = tuple(dict.fromkeys(sources))
    if not unique_sources or any(source not in SOURCE_SEARCHES for source in unique_sources):
        raise ValueError("source must be one of openalex, crossref, or arxiv")
    return unique_sources


async def collect_search(
    query: str,
    limit: int,
    sources: Sequence[str],
) -> BridgeSearchResult:
    """Run requested source searches concurrently without persistence."""
    selected_sources = validate_request(query, limit, sources)
    timeout = httpx.Timeout(40.0, connect=15.0)
    headers = {
        "User-Agent": "ResearchHarvester/0.1 (open-science desktop client)",
    }
    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:
        results = await asyncio.gather(
            *(
                SOURCE_SEARCHES[source](client, query, limit)
                for source in selected_sources
            ),
            return_exceptions=True,
        )

    works: list[WorkCandidate] = []
    failed_sources: list[str] = []
    for source, result in zip(selected_sources, results, strict=True):
        if isinstance(result, Exception):
            failed_sources.append(SOURCE_DISPLAY_NAMES[source])
            continue
        works.extend(result)
    return BridgeSearchResult(
        works=tuple(works[:MAX_WORKS]),
        failed_sources=tuple(failed_sources),
    )


def _bounded_text(value: str) -> str:
    return value.encode("utf-8")[:MAX_FIELD_BYTES].decode("utf-8", errors="ignore")


def _encode_field(value: str) -> str:
    return base64.b64encode(_bounded_text(value).encode("utf-8")).decode("ascii")


def _bounded_unsigned(value: int | None) -> int:
    if value is None:
        return 0
    return max(0, min(value, (1 << 32) - 1))


def render_success(result: BridgeSearchResult) -> str:
    """Encode untrusted source metadata as one strict, ASCII-only stream."""
    lines = [PROTOCOL_HEADER]
    if result.failed_sources:
        lines.append(
            "\t".join(
                (
                    "NOTICE",
                    _encode_field(
                        "Some selected sources did not respond: "
                        + ", ".join(result.failed_sources)
                        + "."
                    ),
                )
            )
        )
    for work in result.works:
        reference = work.source_id or work.doi or work.url or ""
        lines.append(
            "\t".join(
                (
                    "WORK",
                    _encode_field(reference),
                    _encode_field(work.title),
                    _encode_field(", ".join(work.authors)),
                    _encode_field(work.source),
                    str(_bounded_unsigned(work.year)),
                    str(_bounded_unsigned(work.citation_count)),
                    _encode_field(work.doi or ""),
                    _encode_field(work.open_access_url or ""),
                )
            )
        )
    lines.append("END")
    return "\n".join(lines) + "\n"


def render_failure(message: str) -> str:
    return f"ERROR\t{_encode_field(message)}\n"


def render_fulltext_discovery_success(result: FullTextDiscoveryResult) -> str:
    """Encode candidates for the service cache; the UI never sees this stream."""
    lines = [PROTOCOL_HEADER]
    if result.failed_sources:
        lines.append(
            "\t".join(
                (
                    "NOTICE",
                    _encode_field(
                        "Some full-text sources did not respond: "
                        + ", ".join(result.failed_sources)
                        + "."
                    ),
                )
            )
        )
    for candidate in result.candidates[:MAX_FULLTEXT_CANDIDATES]:
        lines.append(
            "\t".join(
                (
                    "CANDIDATE",
                    _encode_field(candidate.url),
                    _encode_field(candidate.source),
                    _encode_field(candidate.repository or ""),
                    _encode_field(candidate.license or ""),
                    _encode_field(candidate.version or ""),
                    _encode_field(candidate.host_type or ""),
                    "1" if candidate.direct_pdf else "0",
                )
            )
        )
    lines.append("END")
    return "\n".join(lines) + "\n"


def render_download_success(result: DownloadedPdf) -> str:
    """Expose only the safe local filename, never the complete local path."""
    filename = os.path.basename(result.path)
    lines = [
        PROTOCOL_HEADER,
        "\t".join(
            (
                "DOWNLOAD",
                _encode_field(filename),
                str(_bounded_unsigned(result.byte_count)),
                result.sha256,
            )
        ),
        "END",
    ]
    return "\n".join(lines) + "\n"


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Research Harvester desktop worker")
    parser.add_argument(
        "--operation",
        choices=("search", "discover", "download"),
        default="search",
    )
    parser.add_argument("--query")
    parser.add_argument("--limit", type=int)
    parser.add_argument(
        "--source",
        action="append",
        choices=tuple(SOURCE_SEARCHES),
    )
    parser.add_argument("--title")
    parser.add_argument("--doi")
    parser.add_argument("--open-access-url")
    parser.add_argument("--candidate-url")
    parser.add_argument("--candidate-source")
    return parser.parse_args(argv)


def _validate_fulltext_title(value: str | None) -> str:
    if not isinstance(value, str) or not value.strip() or len(value) > MAX_FIELD_BYTES:
        raise ValueError("full-text title is invalid")
    return value


async def _run_discovery(args: argparse.Namespace) -> FullTextDiscoveryResult:
    request = FullTextDiscoveryRequest(
        title=_validate_fulltext_title(args.title),
        doi=args.doi,
        open_access_url=args.open_access_url,
    )
    timeout = httpx.Timeout(40.0, connect=15.0)
    headers = {"User-Agent": "ResearchHarvester/0.1 (open-science desktop client)"}
    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=False,
        headers=headers,
        trust_env=False,
    ) as client:
        return await discover_open_access_candidates(request, client)


async def _run_download(args: argparse.Namespace) -> DownloadedPdf:
    title = _validate_fulltext_title(args.title)
    candidates = normalize_candidates(
        (
            {
                "source": args.candidate_source,
                "pdf_url": args.candidate_url,
                "url": args.candidate_url,
            },
        )
    )
    if len(candidates) != 1:
        raise ValueError("full-text candidate is invalid")
    timeout = httpx.Timeout(45.0, connect=15.0)
    headers = {"User-Agent": "ResearchHarvester/0.1 (open-science desktop client)"}
    async with httpx.AsyncClient(
        transport=_PinnedPublicAsyncHttpTransport(),
        timeout=timeout,
        follow_redirects=False,
        headers=headers,
        trust_env=False,
    ) as client:
        return await download_selected_pdf(client, candidates[0], title=title)


def main(argv: Sequence[str] | None = None) -> int:
    try:
        args = _parse_args(argv)
        if args.operation == "search":
            result = asyncio.run(
                collect_search(args.query, args.limit, args.source or ())
            )
            if result.failed_sources and not result.works:
                sys.stdout.write(render_failure("Selected sources did not respond."))
                return 1
            sys.stdout.write(render_success(result))
            return 0
        if args.operation == "discover":
            result = asyncio.run(_run_discovery(args))
            sys.stdout.write(render_fulltext_discovery_success(result))
            return 0
        result = asyncio.run(_run_download(args))
    except (ValueError, RuntimeError):
        sys.stdout.write(render_failure("Desktop worker request failed."))
        return 1
    except KeyboardInterrupt:
        return 130
    sys.stdout.write(render_download_success(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
