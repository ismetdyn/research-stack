from __future__ import annotations

import asyncio

import httpx

from research_harvester import config, db

from research_harvester.sources import (
    crossref_integrity,
    opencitations,
    unpaywall,
)


async def enrich_work(work: dict):

    doi = work.get("doi")

    if not doi:
        raise ValueError(
            "Bu kaydın DOI'si yok; DOI enrichment yapılamaz."
        )

    timeout = httpx.Timeout(
        45.0,
        connect=15.0,
    )

    headers = {
        "User-Agent":
            "ResearchHarvester/0.2 "
            "(open-science personal research tool)"
    }

    async with httpx.AsyncClient(
        timeout=timeout,
        follow_redirects=True,
        headers=headers,
    ) as client:

        crossref_task = (
            crossref_integrity.fetch(
                client,
                doi,
                config.CROSSREF_MAILTO,
            )
        )

        opencitations_task = (
            opencitations.counts(
                client,
                doi,
                config.OPENCITATIONS_ACCESS_TOKEN,
            )
        )

        tasks = [
            crossref_task,
            opencitations_task,
        ]

        if config.UNPAYWALL_EMAIL:
            unpaywall_index = len(tasks)

            tasks.append(
                unpaywall.lookup(
                    client,
                    doi,
                    config.UNPAYWALL_EMAIL,
                )
            )

        else:
            unpaywall_index = None

        results = await asyncio.gather(
            *tasks,
            return_exceptions=True,
        )

    crossref_result = results[0]
    oc_result = results[1]

    output = {
        "crossref": None,
        "opencitations": None,
        "unpaywall": None,
        "errors": [],
    }

    if isinstance(
        crossref_result,
        Exception,
    ):
        output["errors"].append(
            f"Crossref: {crossref_result}"
        )

    else:
        updates = crossref_result[
            "updates"
        ]

        status = (
            crossref_integrity.determine_status(
                updates
            )
        )

        db.replace_updates(
            work["id"],
            updates,
            status,
        )

        output["crossref"] = {
            "integrity_status": status,
            "updates": updates,
        }

    if isinstance(
        oc_result,
        Exception,
    ):
        output["errors"].append(
            f"OpenCitations: {oc_result}"
        )

    else:
        db.save_metrics(
            work["id"],
            "OpenCitations",
            oc_result["citation_count"],
            oc_result["reference_count"],
            oc_result["raw"],
        )

        output["opencitations"] = (
            oc_result
        )

    if unpaywall_index is not None:

        upw_result = results[
            unpaywall_index
        ]

        if isinstance(
            upw_result,
            Exception,
        ):
            output["errors"].append(
                f"Unpaywall: {upw_result}"
            )

        else:
            db.replace_oa_locations(
                work["id"],
                upw_result["locations"],
            )

            output["unpaywall"] = {
                "is_oa":
                    upw_result["is_oa"],
                "best":
                    upw_result.get("best"),
                "locations":
                    len(
                        upw_result["locations"]
                    ),
            }

    else:
        output["errors"].append(
            "Unpaywall atlandı: "
            "UNPAYWALL_EMAIL ayarlanmamış."
        )

    return output
