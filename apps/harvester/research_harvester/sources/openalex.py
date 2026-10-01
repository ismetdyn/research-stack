from __future__ import annotations

import httpx

from research_harvester.models import WorkCandidate
from research_harvester.utils import (
    normalize_doi,
    reconstruct_openalex_abstract,
)


BASE_URL = "https://api.openalex.org/works"


async def search(
    client: httpx.AsyncClient,
    query: str,
    limit: int = 10,
    api_key: str | None = None,
) -> list[WorkCandidate]:

    params = {
        "search": query,
        "per-page": limit,
    }

    if api_key:
        params["api_key"] = api_key

    response = await client.get(BASE_URL, params=params)
    response.raise_for_status()

    payload = response.json()

    results: list[WorkCandidate] = []

    for item in payload.get("results", []):
        authors = [
            a.get("author", {}).get("display_name")
            for a in item.get("authorships", [])
            if a.get("author", {}).get("display_name")
        ]

        oa = item.get("open_access") or {}

        best_oa = item.get("best_oa_location") or {}

        primary_location = item.get("primary_location") or {}

        results.append(
            WorkCandidate(
                source="OpenAlex",
                source_id=item.get("id"),
                title=item.get("title") or "",
                doi=normalize_doi(item.get("doi")),
                year=item.get("publication_year"),
                abstract=reconstruct_openalex_abstract(
                    item.get("abstract_inverted_index")
                ),
                authors=authors,
                url=primary_location.get("landing_page_url")
                or item.get("id"),
                open_access_url=best_oa.get("pdf_url")
                or best_oa.get("landing_page_url"),
                is_open_access=oa.get("is_oa"),
                citation_count=item.get("cited_by_count"),
                work_type=item.get("type"),
                raw=item,
            )
        )

    return results
