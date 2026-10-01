from __future__ import annotations

from urllib.parse import quote

import httpx


BASE = "https://api.unpaywall.org/v2"


async def lookup(
    client: httpx.AsyncClient,
    doi: str,
    email: str,
):
    encoded = quote(doi, safe="")

    response = await client.get(
        f"{BASE}/{encoded}",
        params={
            "email": email,
        },
    )

    if response.status_code == 404:
        return {
            "is_oa": False,
            "locations": [],
            "raw": {},
        }

    response.raise_for_status()

    payload = response.json()

    best = payload.get(
        "best_oa_location"
    )

    locations = []

    for location in (
        payload.get("oa_locations") or []
    ):
        location = dict(location)
        location["_is_best"] = False

        if best:
            if (
                location.get("url")
                == best.get("url")
            ):
                location["_is_best"] = True

        locations.append(location)

    return {
        "is_oa": payload.get("is_oa", False),
        "locations": locations,
        "best": best,
        "raw": payload,
    }
