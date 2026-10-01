from __future__ import annotations

import httpx

from research_harvester.models import WorkCandidate
from research_harvester.utils import normalize_doi, safe_year


BASE_URL = (
    "https://api.openaire.eu/"
    "graph/v3/research-products"
)


def get_doi(item: dict) -> str | None:
    pids = item.get("pid") or item.get("pids") or []

    for pid in pids:
        if not isinstance(pid, dict):
            continue

        scheme = (
            pid.get("scheme")
            or pid.get("type")
            or ""
        ).lower()

        value = (
            pid.get("value")
            or pid.get("id")
        )

        if scheme == "doi":
            return normalize_doi(value)

    return None


def get_authors(item: dict) -> list[str]:
    raw_authors = (
        item.get("authors")
        or item.get("author")
        or []
    )

    authors = []

    for author in raw_authors:
        if not isinstance(author, dict):
            continue

        name = (
            author.get("fullName")
            or author.get("fullname")
        )

        if name:
            authors.append(name)

    return authors


def get_description(item: dict) -> str | None:
    descriptions = item.get("descriptions") or []

    if isinstance(descriptions, str):
        return descriptions

    for desc in descriptions:
        if isinstance(desc, str):
            return desc

        if isinstance(desc, dict):
            value = (
                desc.get("value")
                or desc.get("description")
                or desc.get("text")
            )

            if value:
                return value

    return None


def is_open_access(item: dict) -> bool | None:
    obj = item.get("bestAccessRight") or {}

    label = (
        obj.get("label")
        or obj.get("code")
        or ""
    ).lower()

    if not label:
        return None

    return "open" in label


async def search(
    client: httpx.AsyncClient,
    query: str,
    limit: int = 10,
) -> list[WorkCandidate]:

    params = {
        "search": query,
        "pageSize": limit,
    }

    response = await client.get(
        BASE_URL,
        params=params,
    )

    response.raise_for_status()

    payload = response.json()

    results: list[WorkCandidate] = []

    for item in payload.get("results", []):
        title = (
            item.get("mainTitle")
            or item.get("maintitle")
            or ""
        )

        results.append(
            WorkCandidate(
                source="OpenAIRE",
                source_id=item.get("id"),
                title=title,
                doi=get_doi(item),
                year=safe_year(
                    item.get("publicationDate")
                ),
                abstract=get_description(item),
                authors=get_authors(item),
                is_open_access=is_open_access(item),
                citation_count=(
                    item
                    .get("indicators", {})
                    .get("citationCount")
                ),
                work_type=item.get("type"),
                raw=item,
            )
        )

    return results
