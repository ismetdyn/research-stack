from __future__ import annotations

import asyncio

import httpx

from research_harvester import config
from research_harvester.models import WorkCandidate

from research_harvester.sources import (
    crossref,
    openaire,
    openalex,
    semantic_scholar,
)


async def search_all(
    query: str,
    limit: int = 10,
) -> list[WorkCandidate]:

    timeout = httpx.Timeout(
        40.0,
        connect=15.0,
    )

    headers = {
        "User-Agent":
            "ResearchHarvester/0.1 "
            "(open-science personal research tool)"
    }

    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:

        tasks = [
            openalex.search(
                client,
                query,
                limit,
                config.OPENALEX_API_KEY,
            ),
            openaire.search(
                client,
                query,
                limit,
            ),
            crossref.search(
                client,
                query,
                limit,
                config.CROSSREF_MAILTO,
            ),
            semantic_scholar.search(
                client,
                query,
                limit,
                config.SEMANTIC_SCHOLAR_API_KEY,
            ),
        ]

        results = await asyncio.gather(
            *tasks,
            return_exceptions=True,
        )

    all_items: list[WorkCandidate] = []

    source_names = [
        "OpenAlex",
        "OpenAIRE",
        "Crossref",
        "Semantic Scholar",
    ]

    for source_name, result in zip(
        source_names,
        results,
    ):
        if isinstance(result, Exception):
            print(
                f"[WARNING] {source_name}: {result}"
            )
            continue

        all_items.extend(result)

    return all_items
