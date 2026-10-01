"""Database-free full-text primitives for the native desktop worker.

This module deliberately has no dependency on the CLI, database configuration,
or the legacy full-text pipeline.  It accepts only normalized open-access
metadata and downloads a selected candidate after the caller has made an
explicit choice.
"""

from __future__ import annotations

import asyncio
import hashlib
import ipaddress
import os
import re
import socket
import tempfile
import unicodedata
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urljoin, urlsplit, urlunsplit

import httpx
import httpcore

from research_harvester.utils import normalize_doi


MAX_METADATA_CHARACTERS = 512
MAX_DOWNLOAD_BYTES = 64 * 1024 * 1024
MAX_REDIRECTS = 5
MAX_FULLTEXT_CANDIDATES = 200
OPENALEX_WORKS_URL = "https://api.openalex.org/works"
OPENAIRE_RESEARCH_PRODUCTS_URL = "https://api.openaire.eu/graph/v3/research-products"
EUROPE_PMC_SEARCH_URL = "https://www.ebi.ac.uk/europepmc/webservices/rest/search"
ZENODO_RECORDS_URL = "https://zenodo.org/api/records"

HostResolver = Callable[[str], Sequence[str]]


class FullTextDownloadError(RuntimeError):
    """A selected candidate could not be downloaded safely."""


@dataclass(frozen=True)
class FullTextCandidate:
    """Safe public metadata; the worker keeps any UI-facing ID separately."""

    source: str
    url: str
    direct_pdf: bool
    license: str | None
    version: str | None
    host_type: str | None
    repository: str | None


@dataclass(frozen=True)
class DownloadedPdf:
    path: str
    byte_count: int
    sha256: str


@dataclass(frozen=True)
class FullTextDiscoveryRequest:
    """The minimal work metadata needed for read-only OA discovery."""

    title: str
    doi: str | None
    open_access_url: str | None


@dataclass(frozen=True)
class FullTextDiscoveryResult:
    candidates: tuple[FullTextCandidate, ...]
    failed_sources: tuple[str, ...]


def _bounded_text(value: object) -> str | None:
    if not isinstance(value, str):
        return None
    text = value.strip()
    if not text:
        return None
    return text[:MAX_METADATA_CHARACTERS]


def _canonical_https_url(value: object) -> str | None:
    """Return a credential-free public HTTPS URL or reject the metadata."""
    text = _bounded_text(value)
    if text is None:
        return None

    try:
        parsed = urlsplit(text)
        port = parsed.port
    except ValueError:
        return None
    if (
        parsed.scheme.lower() != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
    ):
        return None

    host = parsed.hostname.rstrip(".").lower()
    if not host or host == "localhost" or host.endswith((".localhost", ".local")):
        return None
    try:
        literal_address = ipaddress.ip_address(host)
    except ValueError:
        literal_address = None
    if literal_address is not None:
        return None

    try:
        ascii_host = host.encode("idna").decode("ascii")
    except UnicodeError:
        return None
    netloc = ascii_host if port is None else f"{ascii_host}:{port}"
    return urlunsplit(("https", netloc, parsed.path or "/", parsed.query, ""))


def normalize_candidates(
    records: Sequence[Mapping[str, object]],
) -> tuple[FullTextCandidate, ...]:
    """Normalize and deduplicate untrusted open-access source metadata.

    A direct-PDF URL takes precedence over a landing URL.  Candidate discovery
    calls this function before exposing provider values to the native protocol.
    """
    unique: dict[str, FullTextCandidate] = {}
    for record in records:
        direct_url = _canonical_https_url(record.get("pdf_url"))
        landing_url = _canonical_https_url(record.get("url"))
        url = direct_url or landing_url
        source = _bounded_text(record.get("source"))
        if url is None or source is None:
            continue
        unique.setdefault(
            url,
            FullTextCandidate(
                source=source,
                url=url,
                direct_pdf=direct_url is not None,
                license=_bounded_text(record.get("license")),
                version=_bounded_text(record.get("version")),
                host_type=_bounded_text(record.get("host_type")),
                repository=_bounded_text(record.get("repository")),
            ),
        )
    return tuple(unique.values())


def _source_metadata_record(request: FullTextDiscoveryRequest) -> dict[str, object] | None:
    if _canonical_https_url(request.open_access_url) is None:
        return None
    return {
        "source": "Source metadata",
        "pdf_url": request.open_access_url,
        "url": request.open_access_url,
        "host_type": "publisher",
    }


def _mapping_list(value: object) -> tuple[Mapping[str, object], ...]:
    if isinstance(value, Mapping):
        return (value,)
    if not isinstance(value, list):
        return ()
    return tuple(item for item in value if isinstance(item, Mapping))


def _string_list(value: object) -> tuple[str, ...]:
    if isinstance(value, str):
        return (value,)
    if not isinstance(value, list):
        return ()
    return tuple(item for item in value if isinstance(item, str))


def _url_path_is_pdf(value: str) -> bool:
    """Classify a provider URL without letting malformed metadata abort discovery."""
    try:
        return urlsplit(value).path.lower().endswith(".pdf")
    except ValueError:
        return False


async def _openalex_records(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict[str, object]]:
    response = await client.get(
        OPENALEX_WORKS_URL,
        params={"filter": f"doi:{doi}", "per-page": 1},
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload, dict):
        return []
    results = payload.get("results")
    if not isinstance(results, list) or not results or not isinstance(results[0], dict):
        return []
    locations = results[0].get("locations")
    if not isinstance(locations, list):
        return []

    records: list[dict[str, object]] = []
    for location in locations:
        if not isinstance(location, dict) or location.get("is_oa") is not True:
            continue
        source = location.get("source")
        source_data = source if isinstance(source, dict) else {}
        records.append(
            {
                "source": "OpenAlex",
                "pdf_url": location.get("pdf_url"),
                "url": location.get("landing_page_url"),
                "license": location.get("license"),
                "version": location.get("version"),
                "host_type": source_data.get("type"),
                "repository": source_data.get("display_name"),
            }
        )
    return records


async def _openaire_records(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict[str, object]]:
    """Map only explicit OpenAIRE open-access instances into candidates."""
    response = await client.get(
        OPENAIRE_RESEARCH_PRODUCTS_URL,
        params={"pid": doi, "pageSize": 5},
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload, Mapping):
        return []

    records: list[dict[str, object]] = []
    for product in _mapping_list(payload.get("results")):
        instances_value = product.get("instances") or product.get("instance")
        for instance in _mapping_list(instances_value):
            access_right = instance.get("accessRight") or instance.get("accessright")
            access = access_right if isinstance(access_right, Mapping) else {}
            access_label = _bounded_text(access.get("label"))
            if access_label is None or "open access" not in access_label.lower():
                continue

            urls_value = instance.get("urls") or instance.get("url")
            for url in _string_list(urls_value):
                records.append(
                    {
                        "source": "OpenAIRE",
                        "pdf_url": url if _url_path_is_pdf(url) else None,
                        "url": url,
                        "license": instance.get("license"),
                        "version": instance.get("version") or instance.get("type"),
                        "host_type": "repository",
                        "repository": "OpenAIRE",
                    }
                )
    return records


async def _europe_pmc_records(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict[str, object]]:
    """Map exact, explicitly open Europe PMC records into safe candidates."""
    response = await client.get(
        EUROPE_PMC_SEARCH_URL,
        params={
            "query": f'DOI:"{doi}"',
            "format": "json",
            "resultType": "core",
            "pageSize": 5,
        },
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload, Mapping):
        return []
    result_list = payload.get("resultList")
    if not isinstance(result_list, Mapping):
        return []

    records: list[dict[str, object]] = []
    for item in _mapping_list(result_list.get("result")):
        if normalize_doi(item.get("doi")) != doi:
            continue
        open_access = _bounded_text(item.get("isOpenAccess"))
        if open_access is None or open_access.upper() != "Y":
            continue

        fulltext = item.get("fullTextUrlList")
        fulltext_urls = fulltext if isinstance(fulltext, Mapping) else {}
        urls = fulltext_urls.get("fullTextUrl")
        entries = urls if isinstance(urls, list) else [urls]
        for entry in entries:
            if isinstance(entry, str):
                url = entry
                style = None
            elif isinstance(entry, Mapping):
                url = entry.get("url")
                style = entry.get("documentStyle") or entry.get("style")
            else:
                continue
            if not isinstance(url, str):
                continue
            style_label = _bounded_text(style)
            direct_pdf = (
                style_label is not None and style_label.lower() == "pdf"
            ) or _url_path_is_pdf(url)
            records.append(
                {
                    "source": "Europe PMC",
                    "pdf_url": url if direct_pdf else None,
                    "url": url,
                    "license": item.get("license"),
                    "host_type": "repository",
                    "repository": "Europe PMC",
                }
            )

        pmcid = _bounded_text(item.get("pmcid"))
        if pmcid is not None and re.fullmatch(r"PMC[0-9]+", pmcid):
            records.append(
                {
                    "source": "Europe PMC",
                    "url": f"https://europepmc.org/articles/{pmcid}",
                    "license": item.get("license"),
                    "host_type": "repository",
                    "repository": "Europe PMC",
                }
            )
    return records


def _zenodo_record_matches_doi(record: Mapping[str, object], doi: str) -> bool:
    metadata = record.get("metadata")
    metadata_values = metadata if isinstance(metadata, Mapping) else {}
    identifiers: list[object] = [metadata_values.get("doi")]
    pids = record.get("pids")
    pid_values = pids if isinstance(pids, Mapping) else {}
    pid_doi = pid_values.get("doi")
    if isinstance(pid_doi, Mapping):
        identifiers.append(pid_doi.get("identifier"))
    else:
        identifiers.append(pid_doi)
    return any(normalize_doi(identifier) == doi for identifier in identifiers if isinstance(identifier, str))


def _zenodo_open_access(record: Mapping[str, object], metadata: Mapping[str, object]) -> bool:
    access = metadata.get("access_right")
    if access is None:
        record_access = record.get("access")
        if isinstance(record_access, Mapping):
            access = record_access.get("record")
    access_label = _bounded_text(access)
    return access_label is not None and access_label.lower() in {"open", "public"}


def _zenodo_license(value: object) -> str | None:
    if isinstance(value, Mapping):
        return _bounded_text(value.get("title")) or _bounded_text(value.get("id"))
    return _bounded_text(value)


def _zenodo_pdf_urls(record: Mapping[str, object]) -> tuple[str, ...]:
    files = record.get("files")
    urls: list[str] = []
    if isinstance(files, Mapping):
        entries = files.get("entries")
        if isinstance(entries, Mapping):
            for filename, info in entries.items():
                if not isinstance(filename, str) or not isinstance(info, Mapping):
                    continue
                links = info.get("links")
                link_values = links if isinstance(links, Mapping) else {}
                url = (
                    link_values.get("content")
                    or link_values.get("download")
                    or link_values.get("self")
                )
                media_type = _bounded_text(info.get("mimetype") or info.get("media_type"))
                if isinstance(url, str) and (
                    filename.lower().endswith(".pdf")
                    or media_type is not None and media_type.lower() == "application/pdf"
                ):
                    urls.append(url)
    elif isinstance(files, list):
        for info in _mapping_list(files):
            filename = _bounded_text(info.get("key") or info.get("filename"))
            links = info.get("links")
            link_values = links if isinstance(links, Mapping) else {}
            url = (
                link_values.get("download")
                or link_values.get("content")
                or link_values.get("self")
            )
            media_type = _bounded_text(info.get("mimetype") or info.get("media_type"))
            if isinstance(url, str) and (
                filename is not None and filename.lower().endswith(".pdf")
                or media_type is not None and media_type.lower() == "application/pdf"
            ):
                urls.append(url)
    return tuple(urls)


async def _zenodo_records(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict[str, object]]:
    """Map public Zenodo record PDFs only when their DOI matches exactly."""
    response = await client.get(
        ZENODO_RECORDS_URL,
        params={"q": f'"{doi}"', "size": 10},
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload, Mapping):
        return []
    hits = payload.get("hits")
    hit_values = hits if isinstance(hits, Mapping) else {}

    records: list[dict[str, object]] = []
    for record in _mapping_list(hit_values.get("hits")):
        metadata = record.get("metadata")
        metadata_values = metadata if isinstance(metadata, Mapping) else {}
        if not _zenodo_record_matches_doi(record, doi) or not _zenodo_open_access(
            record,
            metadata_values,
        ):
            continue
        for url in _zenodo_pdf_urls(record):
            records.append(
                {
                    "source": "Zenodo",
                    "pdf_url": url,
                    "url": url,
                    "license": _zenodo_license(metadata_values.get("license")),
                    "version": metadata_values.get("version"),
                    "host_type": "repository",
                    "repository": "Zenodo",
                }
            )
    return records


async def discover_open_access_candidates(
    request: FullTextDiscoveryRequest,
    client: httpx.AsyncClient,
) -> FullTextDiscoveryResult:
    """Collect declared OA metadata and public provider locations independently.

    This function does not write files, touch the database, launch a browser,
    or reveal provider failure details to the caller.
    """
    records: list[dict[str, object]] = []
    source_metadata = _source_metadata_record(request)
    if source_metadata is not None:
        records.append(source_metadata)

    failures: list[str] = []
    doi = normalize_doi(request.doi)
    if doi:
        provider_results = await asyncio.gather(
            _openalex_records(client, doi),
            _openaire_records(client, doi),
            _europe_pmc_records(client, doi),
            _zenodo_records(client, doi),
            return_exceptions=True,
        )
        for source, result in zip(
            ("OpenAlex", "OpenAIRE", "Europe PMC", "Zenodo"),
            provider_results,
            strict=True,
        ):
            if isinstance(result, Exception):
                failures.append(source)
            elif isinstance(result, list):
                records.extend(result)
    return FullTextDiscoveryResult(
        candidates=normalize_candidates(records)[:MAX_FULLTEXT_CANDIDATES],
        failed_sources=tuple(failures),
    )


def desktop_papers_directory(environ: Mapping[str, str] | None = None) -> Path:
    """Return the application-owned XDG directory without creating it."""
    environment = os.environ if environ is None else environ
    configured = environment.get("XDG_DATA_HOME", "").strip()
    data_home = Path(configured) if configured else Path.home() / ".local" / "share"
    return data_home / "research-harvester" / "papers"


def _resolve_public_host(host: str, resolve_host: HostResolver) -> None:
    try:
        addresses = tuple(resolve_host(host))
    except (OSError, socket.gaierror, ValueError) as error:
        raise FullTextDownloadError("download host could not be resolved.") from error
    if not addresses:
        raise FullTextDownloadError("download host could not be resolved.")
    try:
        all_public = all(ipaddress.ip_address(address).is_global for address in addresses)
    except ValueError as error:
        raise FullTextDownloadError("unsafe download address.") from error
    if not all_public:
        raise FullTextDownloadError("unsafe download address.")


def _system_resolve_host(host: str) -> tuple[str, ...]:
    return tuple(
        dict.fromkeys(
            info[4][0]
            for info in socket.getaddrinfo(host, 443, type=socket.SOCK_STREAM)
        )
    )


class _PinnedPublicNetworkBackend(httpcore.AsyncNetworkBackend):
    """Connect only to a public address resolved immediately before TCP setup."""

    def __init__(self, backend: httpcore.AsyncNetworkBackend | None = None) -> None:
        self._backend = backend or httpcore.AnyIOBackend()

    async def connect_tcp(
        self,
        host: str,
        port: int,
        timeout: float | None = None,
        local_address: str | None = None,
        socket_options=None,
    ):
        try:
            addresses = tuple(
                dict.fromkeys(
                    info[4][0]
                    for info in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
                )
            )
            if not addresses or not all(
                ipaddress.ip_address(address).is_global for address in addresses
            ):
                raise ValueError("unsafe address")
        except (OSError, ValueError) as error:
            raise httpcore.ConnectError("unsafe download address") from error
        return await self._backend.connect_tcp(
            addresses[0],
            port,
            timeout=timeout,
            local_address=local_address,
            socket_options=socket_options,
        )

    async def connect_unix_socket(self, path: str, timeout=None, socket_options=None):
        del path, timeout, socket_options
        raise httpcore.ConnectError("unix sockets are not allowed")

    async def sleep(self, seconds: float) -> None:
        await self._backend.sleep(seconds)


class _PinnedPublicAsyncHttpTransport(httpx.AsyncHTTPTransport):
    """Preserve HTTPX TLS/SNI handling while pinning the TCP destination."""

    def __init__(self) -> None:
        super().__init__(trust_env=False, retries=0)
        # HTTPX delegates TCP setup to this httpcore pool. Replacing only the
        # backend preserves HTTPX's certificate-verification and SNI setup.
        self._pool._network_backend = _PinnedPublicNetworkBackend()


def _validated_download_url(url: str, resolve_host: HostResolver) -> str:
    canonical = _canonical_https_url(url)
    if canonical is None:
        raise FullTextDownloadError("unsafe download address.")
    host = urlsplit(canonical).hostname
    assert host is not None
    _resolve_public_host(host, resolve_host)
    return canonical


def _safe_filename(title: str, digest: str) -> str:
    normalized = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode("ascii")
    stem = re.sub(r"[^a-z0-9]+", "-", normalized.lower()).strip("-")[:72]
    return f"{stem or 'paper'}-{digest[:12]}.pdf"


async def download_selected_pdf(
    client: httpx.AsyncClient,
    candidate: FullTextCandidate,
    *,
    title: str,
    papers_directory: Path | None = None,
    resolve_host: HostResolver = _system_resolve_host,
    maximum_bytes: int = MAX_DOWNLOAD_BYTES,
) -> DownloadedPdf:
    """Stream one selected candidate into an atomically named PDF file.

    Redirects are deliberately disabled in HTTPX and checked one hop at a
    time.  The caller supplies a candidate selected from discovery, never an
    editable URL field.
    """
    if maximum_bytes < 5:
        raise ValueError("maximum_bytes must allow a PDF header")
    current_url = _validated_download_url(candidate.url, resolve_host)

    for _ in range(MAX_REDIRECTS + 1):
        try:
            async with client.stream("GET", current_url, follow_redirects=False) as response:
                if response.status_code in {301, 302, 303, 307, 308}:
                    location = response.headers.get("location")
                    if not location:
                        raise FullTextDownloadError("download redirect was invalid.")
                    current_url = _validated_download_url(
                        urljoin(current_url, location),
                        resolve_host,
                    )
                    continue
                if response.status_code < 200 or response.status_code >= 300:
                    raise FullTextDownloadError("download request failed.")
                return await _save_pdf_response(
                    response,
                    title=title,
                    papers_directory=papers_directory or desktop_papers_directory(),
                    maximum_bytes=maximum_bytes,
                )
        except FullTextDownloadError:
            raise
        except httpx.HTTPError as error:
            raise FullTextDownloadError("download request failed.") from error
    raise FullTextDownloadError("download redirected too many times.")


async def _save_pdf_response(
    response: httpx.Response,
    *,
    title: str,
    papers_directory: Path,
    maximum_bytes: int,
) -> DownloadedPdf:
    papers_directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(papers_directory, 0o700)
    temporary_path: Path | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=".download-",
            suffix=".tmp",
            dir=papers_directory,
        )
        temporary_path = Path(temporary_name)
        byte_count = 0
        prefix = bytearray()
        digest = hashlib.sha256()
        with os.fdopen(descriptor, "wb") as output:
            async for chunk in response.aiter_bytes():
                byte_count += len(chunk)
                if byte_count > maximum_bytes:
                    raise FullTextDownloadError("download exceeded the size limit.")
                if len(prefix) < 5:
                    prefix.extend(chunk[: 5 - len(prefix)])
                digest.update(chunk)
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        if not bytes(prefix).startswith(b"%PDF-"):
            raise FullTextDownloadError("download was not a PDF.")

        checksum = digest.hexdigest()
        destination = papers_directory / _safe_filename(title, checksum)
        if destination.exists():
            temporary_path.unlink()
        else:
            os.replace(temporary_path, destination)
            os.chmod(destination, 0o600)
        temporary_path = None
        return DownloadedPdf(str(destination), byte_count, checksum)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
