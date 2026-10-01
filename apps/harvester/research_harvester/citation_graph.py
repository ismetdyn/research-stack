from __future__ import annotations

import asyncio

import httpx

from research_harvester import (
    config,
    db,
)

from research_harvester.sources import (
    opencitations,
)


async def fetch_citation_graph(
    work: dict,
):
    doi = work.get("doi")

    if not doi:
        raise RuntimeError(
            "Bu çalışma için DOI yok."
        )

    timeout = httpx.Timeout(
        90.0,
        connect=15.0,
    )

    headers = {
        "User-Agent":
            "ResearchHarvester/0.5 "
            "(open-science research tool)"
    }

    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:

        outgoing_task = (
            opencitations.references(
                client,
                doi,
                config.OPENCITATIONS_ACCESS_TOKEN,
            )
        )

        incoming_task = (
            opencitations.citations(
                client,
                doi,
                config.OPENCITATIONS_ACCESS_TOKEN,
            )
        )

        outgoing, incoming = (
            await asyncio.gather(
                outgoing_task,
                incoming_task,
            )
        )

    db.replace_citation_links(
        work_id=work["id"],
        direction="outgoing",
        rows=outgoing,
    )

    db.replace_citation_links(
        work_id=work["id"],
        direction="incoming",
        rows=incoming,
    )

    return {
        "work_id": work["id"],
        "doi": doi,
        "references": len(outgoing),
        "citations": len(incoming),
        "references_with_doi": sum(
            1
            for item in outgoing
            if item.get("related_doi")
        ),
        "citations_with_doi": sum(
            1
            for item in incoming
            if item.get("related_doi")
        ),
    }
