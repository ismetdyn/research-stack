from __future__ import annotations

import re
from urllib.parse import quote

import httpx

from research_harvester.utils import normalize_doi


BASE_URL = "https://api.osf.io/v2"
OSF_ID = re.compile(r"^[a-z0-9]{5,20}$", flags=re.IGNORECASE)
OSF_REFERENTS = {"nodes", "preprints", "registrations"}


def _valid_id(value: object) -> str | None:
    if not isinstance(value, str) or not OSF_ID.fullmatch(value):
        return None

    return value


async def _get_json(
    client: httpx.AsyncClient,
    url: str,
    **kwargs,
) -> dict:
    response = await client.get(url, **kwargs)

    if response.status_code == 404:
        return {}

    response.raise_for_status()
    return response.json()


async def resolve(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:
    """Link an OSF preprint DOI to its public OSF project."""
    current_doi = normalize_doi(doi)

    if not current_doi or "osf.io/" not in current_doi:
        return []

    guid = _valid_id(current_doi.rsplit("osf.io/", maxsplit=1)[-1])

    if not guid:
        return []

    guid_payload = await _get_json(
        client,
        f"{BASE_URL}/guids/{quote(guid, safe='')}/",
        params={"resolve": "false"},
    )
    guid_data = guid_payload.get("data") or {}
    referent = (
        guid_data.get("relationships", {})
        .get("referent", {})
    )
    referent_type = (
        referent.get("links", {})
        .get("related", {})
        .get("meta", {})
        .get("type")
    )

    if referent_type not in OSF_REFERENTS:
        return []

    referent_payload = await _get_json(
        client,
        f"{BASE_URL}/{referent_type}/{guid}/",
    )
    preprint = referent_payload.get("data") or {}

    if referent_type != "preprints":
        return []

    node_relation = await _get_json(
        client,
        f"{BASE_URL}/preprints/{guid}/relationships/node/",
    )
    node_id = _valid_id((node_relation.get("data") or {}).get("id"))

    if not node_id:
        return []

    node_payload = await _get_json(
        client,
        f"{BASE_URL}/nodes/{node_id}/",
    )
    node = node_payload.get("data") or {}
    node_attributes = node.get("attributes") or {}
    node_links = node.get("links") or {}

    return [
        {
            "source": "OSF",
            "direction": "outgoing",
            "relation_type": "is_part_of",
            "related_identifier": f"osf:{node_id}",
            "related_identifier_type": "OSF",
            "related_title": node_attributes.get("title"),
            "related_resource_type": "project",
            "related_url": (
                node_links.get("html")
                or f"https://osf.io/{node_id}/"
            ),
            "raw": {"preprint": preprint, "node": node},
        }
    ]
