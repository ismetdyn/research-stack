from __future__ import annotations

import httpx

from research_harvester.models import WorkCandidate
from research_harvester.utils import normalize_doi


BASE_URL = (
    "https://api.semanticscholar.org/"
    "graph/v1/paper/search"
)


async def search(
    client: httpx.AsyncClient,
    query: str,
    limit: int = 10,
    api_key: str | None = None,
) -> list[WorkCandidate]:

    params = {
        "query": query,
        "limit": limit,
        "fields": ",".join(
            [
                "title",
                "abstract",
                "year",
                "authors",
                "externalIds",
                "url",
                "openAccessPdf",
                "citationCount",
                "publicationTypes",
            ]
        ),
    }

    headers = {}

    if api_key:
        headers["x-api-key"] = api_key

    response = await client.get(
        BASE_URL,
        params=params,
        headers=headers,
    )

    response.raise_for_status()

    payload = response.json()

    results: list[WorkCandidate] = []

    for item in payload.get("data", []):
        external_ids = item.get("externalIds") or {}

        oa_pdf = item.get("openAccessPdf") or {}

        publication_types = item.get("publicationTypes") or []

        results.append(
            WorkCandidate(
                source="Semantic Scholar",
                source_id=item.get("paperId"),
                title=item.get("title") or "",
                doi=normalize_doi(external_ids.get("DOI")),
                year=item.get("year"),
                abstract=item.get("abstract"),
                authors=[
                    author.get("name")
                    for author in item.get("authors", [])
                    if author.get("name")
                ],
                url=item.get("url"),
                open_access_url=oa_pdf.get("url"),
                is_open_access=bool(oa_pdf.get("url"))
                if oa_pdf
                else None,
                citation_count=item.get("citationCount"),
                work_type=(
                    ", ".join(publication_types)
                    if publication_types
                    else None
                ),
                raw=item,
            )
        )

    return results
