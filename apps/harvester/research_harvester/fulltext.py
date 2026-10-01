from __future__ import annotations

import hashlib
import re
from pathlib import Path

import httpx

from research_harvester import config, db

from research_harvester.pdf_discovery import (
    discover_pdf,
)

from research_harvester.sources import (
    unpaywall,
)

from research_harvester.fulltext_sources import (
    openalex_candidates,
    core_candidates,
    openaire_candidates,
    europe_pmc_candidates,
    zenodo_candidates,
    doi_candidate,
)


PAPER_DIR = (
    config.ROOT
    / "data"
    / "papers"
)


def slugify(
    value: str,
    max_length: int = 80,
) -> str:

    value = value.lower()

    value = re.sub(
        r"<[^>]+>",
        "",
        value,
    )

    value = re.sub(
        r"[^\w\s-]",
        "",
        value,
    )

    value = re.sub(
        r"[\s_-]+",
        "-",
        value,
    )

    return (
        value.strip("-")[:max_length]
        or "paper"
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as f:
        while chunk := f.read(
            1024 * 1024
        ):
            digest.update(chunk)

    return digest.hexdigest()


def looks_like_pdf(data: bytes) -> bool:
    return data.startswith(b"%PDF-")


async def unpaywall_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    if not config.UNPAYWALL_EMAIL:
        return []

    try:
        result = await unpaywall.lookup(
            client,
            doi,
            config.UNPAYWALL_EMAIL,
        )
    except Exception:
        return []

    candidates = []

    for location in result.get(
        "locations",
        [],
    ):
        candidates.append(
            {
                "source": "Unpaywall",
                "pdf_url":
                    location.get(
                        "url_for_pdf"
                    ),
                "url":
                    location.get("url"),
                "license":
                    location.get(
                        "license"
                    ),
                "version":
                    location.get(
                        "version"
                    ),
                "host_type":
                    location.get(
                        "host_type"
                    ),
                "repository": None,
            }
        )

    return candidates


async def collect_candidates(
    client: httpx.AsyncClient,
    work: dict,
) -> tuple[list[dict], list[str]]:

    doi = work.get("doi")

    if not doi:
        return [], [
            "Çalışmanın DOI'si yok."
        ]

    errors = []

    tasks = [
        (
            "Unpaywall",
            unpaywall_candidates(
                client,
                doi,
            ),
        ),
        (
            "OpenAlex",
            openalex_candidates(
                client,
                doi,
            ),
        ),
        (
            "OpenAIRE",
            openaire_candidates(
                client,
                doi,
            ),
        ),
        (
            "Europe PMC",
            europe_pmc_candidates(
                client,
                doi,
            ),
        ),
        (
            "Zenodo",
            zenodo_candidates(
                client,
                doi,
            ),
        ),
        (
            "CORE",
            core_candidates(
                client,
                doi,
            ),
        ),
    ]

    candidates = []

    for source_name, coroutine in tasks:
        try:
            results = await coroutine
            candidates.extend(results)

        except Exception as exc:
            errors.append(
                f"{source_name}: "
                f"{type(exc).__name__}: "
                f"{exc}"
            )

    # OA indeksleri hiçbir şey bulmasa
    # bile DOI'nin publisher landing page'ini
    # incele.
    candidates.extend(
        await doi_candidate(doi)
    )

    # URL bazında duplicate kaldır.
    seen = set()
    unique = []

    for item in candidates:
        key = (
            item.get("pdf_url"),
            item.get("url"),
        )

        if key in seen:
            continue

        seen.add(key)
        unique.append(item)

    return unique, errors


async def save_pdf(
    work: dict,
    response: httpx.Response,
    source_name: str,
    license_value: str | None,
):

    data = response.content

    if not looks_like_pdf(data):
        return None

    filename = (
        f"{work['id']}_"
        f"{slugify(work['title'])}.pdf"
    )

    PAPER_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    path = PAPER_DIR / filename

    path.write_bytes(data)

    checksum = sha256_file(path)

    document_id = db.save_document(
        work_id=work["id"],
        source_name=source_name,
        local_path=str(path),
        document_url=str(
            response.url
        ),
        media_type="application/pdf",
        sha256=checksum,
        legally_open=True,
    )

    return {
        "success": True,
        "access_status": "open_fulltext",
        "document_id": document_id,
        "path": str(path),
        "sha256": checksum,
        "bytes": len(data),
        "source": source_name,
        "source_url": str(
            response.url
        ),
        "license": license_value,
    }


async def download_fulltext(
    work: dict,
):
    doi = work.get("doi")

    if not doi:
        return {
            "success": False,
            "access_status":
                "no_identifier",
            "error":
                "Çalışmanın DOI'si yok.",
        }

    timeout = httpx.Timeout(
        120.0,
        connect=20.0,
    )

    headers = {
        "User-Agent":
            "Mozilla/5.0 "
            "(X11; Linux x86_64) "
            "ResearchHarvester/0.6",
        "Accept":
            "application/pdf,"
            "text/html,"
            "application/xhtml+xml;"
            "q=0.9,*/*;q=0.8",
    }

    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:

        candidates, source_errors = (
            await collect_candidates(
                client,
                work,
            )
        )

        attempts = []

        for candidate in candidates:

            source = candidate["source"]

            urls = []

            if candidate.get("pdf_url"):
                urls.append(
                    candidate["pdf_url"]
                )

            if (
                candidate.get("url")
                and candidate["url"]
                not in urls
            ):
                urls.append(
                    candidate["url"]
                )

            for url in urls:

                try:
                    response = (
                        await client.get(url)
                    )

                    response.raise_for_status()

                    content_type = (
                        response.headers
                        .get(
                            "content-type",
                            "",
                        )
                        .lower()
                    )

                    #
                    # Doğrudan PDF.
                    #
                    if (
                        "application/pdf"
                        in content_type
                        or looks_like_pdf(
                            response.content
                        )
                    ):
                        result = (
                            await save_pdf(
                                work,
                                response,
                                source,
                                candidate.get(
                                    "license"
                                ),
                            )
                        )

                        if result:
                            result[
                                "discovery"
                            ] = "direct"

                            return result

                    #
                    # Landing page ise
                    # PDF linklerini keşfet.
                    #
                    if "html" in content_type:

                        discovered = (
                            await discover_pdf(
                                client,
                                str(response.url),
                                max_depth=3,
                            )
                        )

                        if discovered:
                            result = (
                                await save_pdf(
                                    work,
                                    discovered[
                                        "response"
                                    ],
                                    source,
                                    candidate.get(
                                        "license"
                                    ),
                                )
                            )

                            if result:
                                result[
                                    "discovery"
                                ] = (
                                    "landing-page"
                                )

                                result[
                                    "discovery_depth"
                                ] = (
                                    discovered[
                                        "depth"
                                    ]
                                )

                                return result

                    attempts.append(
                        {
                            "source": source,
                            "url":
                                str(
                                    response.url
                                ),
                            "result":
                                "not_pdf",
                            "content_type":
                                content_type,
                        }
                    )

                except Exception as exc:
                    attempts.append(
                        {
                            "source": source,
                            "url": url,
                            "result": "error",
                            "error":
                                f"{type(exc).__name__}: "
                                f"{exc}",
                        }
                    )

    return {
        "success": False,
        "access_status":
            "no_oa_found",
        "doi": doi,
        "error":
            "Kontrol edilen açık erişim "
            "kaynaklarında indirilebilir "
            "tam metin bulunamadı.",
        "sources_checked": sorted(
            {
                item["source"]
                for item in candidates
            }
        ),
        "source_errors":
            source_errors,
        "attempts":
            attempts,
        "publisher_url":
            f"https://doi.org/{doi}",
    }
