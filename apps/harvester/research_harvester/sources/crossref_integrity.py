from __future__ import annotations

from urllib.parse import quote

import httpx


BASE = "https://api.crossref.org/works"


async def fetch(
    client: httpx.AsyncClient,
    doi: str,
    mailto: str | None = None,
):
    encoded = quote(doi, safe="")

    params = {}

    if mailto:
        params["mailto"] = mailto

    response = await client.get(
        f"{BASE}/{encoded}",
        params=params,
    )

    response.raise_for_status()

    message = (
        response
        .json()
        .get("message", {})
    )

    updates = (
        message.get("updated-by")
        or []
    )

    return {
        "updates": updates,
        "raw": message,
    }


def determine_status(
    updates: list[dict],
) -> str:

    types = {
        str(item.get("type", "")).lower()
        for item in updates
    }

    if "retraction" in types:
        return "retracted"

    if (
        "expression-of-concern" in types
        or "expression of concern" in types
    ):
        return "expression-of-concern"

    if "correction" in types:
        return "corrected"

    if updates:
        return "updated"

    return "current"
