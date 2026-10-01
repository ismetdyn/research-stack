from __future__ import annotations

import re

import httpx

from research_harvester.models import WorkCandidate
from research_harvester.utils import normalize_doi


BASE_URL = "https://api.crossref.org/works"


def clean_abstract(value: str | None) -> str | None:
    if not value:
        return None

    return re.sub(r"<[^>]+>", " ", value).strip()


def get_year(item: dict) -> int | None:
    for key in (
        "published-print",
        "published-online",
        "published",
        "issued",
    ):
        obj = item.get(key)

        if not obj:
            continue

        parts = obj.get("date-parts") or []

        if parts and parts[0]:
            return parts[0][0]

    return None


async def search(
    client: httpx.AsyncClient,
    query: str,
    limit: int = 10,
    mailto: str | None = None,
) -> list[WorkCandidate]:

    params = {
        "query.bibliographic": query,
        "rows": limit,
    }

    if mailto:
        params["mailto"] = mailto

    response = await client.get(BASE_URL, params=params)
    response.raise_for_status()

    payload = response.json()

    items = (
        payload
        .get("message", {})
        .get("items", [])
    )

    results: list[WorkCandidate] = []

    for item in items:
        titles = item.get("title") or []
        title = titles[0] if titles else ""

        authors = []

        for author in item.get("author", []):
            name = " ".join(
                x for x in [
                    author.get("given"),
                    author.get("family"),
                ]
                if x
            )

            if name:
                authors.append(name)

        results.append(
            WorkCandidate(
                source="Crossref",
                source_id=item.get("DOI"),
                title=title,
                doi=normalize_doi(item.get("DOI")),
                year=get_year(item),
                abstract=clean_abstract(item.get("abstract")),
                authors=authors,
                url=item.get("URL"),
                citation_count=item.get("is-referenced-by-count"),
                work_type=item.get("type"),
                raw=item,
            )
        )

    return results
