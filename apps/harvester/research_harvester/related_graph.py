from __future__ import annotations

import asyncio

import httpx

from research_harvester import db
from research_harvester.sources import arxiv, biorxiv, datacite, osf, zenodo
from research_harvester.utils import normalize_doi


async def resolve_related_outputs(work: dict) -> dict:
    """Resolve and store preprint, dataset, software, and OSF links."""
    doi = normalize_doi(work.get("doi"))

    if not doi:
        raise RuntimeError("Bu çalışma için geçerli bir DOI yok.")

    resolvers = [
        ("bioRxiv/medRxiv", biorxiv.resolve),
        ("arXiv", arxiv.resolve),
        ("OSF", osf.resolve),
        ("DataCite", datacite.resolve),
        ("Zenodo", zenodo.resolve),
    ]
    timeout = httpx.Timeout(40.0, connect=15.0)
    headers = {
        "User-Agent": "ResearchHarvester/0.7 (open-science research tool)"
    }

    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:
        results = await asyncio.gather(
            *(resolver(client, doi) for _, resolver in resolvers),
            return_exceptions=True,
        )

    successful: dict[str, list[dict]] = {}
    errors: list[str] = []

    for (source_name, _), result in zip(resolvers, results):
        if isinstance(result, Exception):
            errors.append(
                f"{source_name}: {type(result).__name__}: {result}"
            )
            continue

        successful[source_name] = result

    db.replace_work_relationships(work["id"], successful)

    return {
        "work_id": work["id"],
        "doi": doi,
        "relationships": sum(len(rows) for rows in successful.values()),
        "sources_checked": list(successful),
        "source_errors": errors,
    }
