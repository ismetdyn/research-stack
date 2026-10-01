from __future__ import annotations

from xml.etree import ElementTree

import httpx

from research_harvester.models import WorkCandidate
from research_harvester.utils import normalize_doi


BASE_URL = "https://export.arxiv.org/api/query"
ATOM = "{http://www.w3.org/2005/Atom}"
ARXIV = "{http://arxiv.org/schemas/atom}"


async def search(
    client: httpx.AsyncClient,
    query: str,
    limit: int = 10,
) -> list[WorkCandidate]:
    """Search arXiv's Atom API without persisting any result."""
    response = await client.get(
        BASE_URL,
        params={
            "search_query": f'all:"{query}"',
            "start": 0,
            "max_results": limit,
        },
    )
    response.raise_for_status()

    try:
        root = ElementTree.fromstring(response.text)
    except ElementTree.ParseError:
        return []

    results: list[WorkCandidate] = []
    for entry in root.findall(f"{ATOM}entry"):
        identifier = entry.findtext(f"{ATOM}id")
        source_id = _article_id(identifier)
        title = " ".join((entry.findtext(f"{ATOM}title") or "").split())
        if not source_id or not title:
            continue
        published = entry.findtext(f"{ATOM}published") or ""
        year = int(published[:4]) if published[:4].isdigit() else None
        authors = [
            name
            for author in entry.findall(f"{ATOM}author")
            if (name := (author.findtext(f"{ATOM}name") or "").strip())
        ]
        results.append(
            WorkCandidate(
                source="arXiv",
                source_id=source_id,
                title=title,
                doi=normalize_doi(entry.findtext(f"{ARXIV}doi")),
                year=year,
                abstract=" ".join(
                    (entry.findtext(f"{ATOM}summary") or "").split()
                )
                or None,
                authors=authors,
                url=f"https://arxiv.org/abs/{source_id}",
                is_open_access=True,
                work_type="preprint",
                raw={"id": identifier},
            )
        )
    return results


def _article_id(identifier: str | None) -> str | None:
    if not identifier:
        return None

    marker = "/abs/"
    if marker not in identifier:
        return None

    return identifier.rsplit(marker, maxsplit=1)[-1].strip() or None


async def resolve(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:
    """Find arXiv preprints whose declared DOI matches ``doi``."""
    current_doi = normalize_doi(doi)

    if not current_doi:
        return []

    response = await client.get(
        BASE_URL,
        params={
            "search_query": f'all:"{current_doi}"',
            "start": 0,
            "max_results": 10,
        },
    )
    response.raise_for_status()

    try:
        root = ElementTree.fromstring(response.text)
    except ElementTree.ParseError:
        return []

    relations: list[dict] = []

    for entry in root.findall(f"{ATOM}entry"):
        entry_doi = normalize_doi(entry.findtext(f"{ARXIV}doi"))

        if entry_doi != current_doi:
            continue

        article_id = _article_id(entry.findtext(f"{ATOM}id"))

        if not article_id:
            continue

        title = " ".join(
            (entry.findtext(f"{ATOM}title") or "").split()
        )

        relations.append(
            {
                "source": "arXiv",
                "direction": "incoming",
                "relation_type": "is_preprint_of",
                "related_identifier": f"arxiv:{article_id}",
                "related_identifier_type": "arXiv",
                "related_title": title or None,
                "related_resource_type": "preprint",
                "related_url": f"https://arxiv.org/abs/{article_id}",
                "raw": {
                    "id": entry.findtext(f"{ATOM}id"),
                    "doi": entry_doi,
                },
            }
        )

    return relations
