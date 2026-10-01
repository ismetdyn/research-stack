from __future__ import annotations

import httpx

from research_harvester.utils import normalize_doi


BASE_URL = "https://api.biorxiv.org"
SERVERS = ("biorxiv", "medrxiv")


def _preprint_url(
    server: str,
    preprint_doi: str,
    version: str | None = None,
) -> str:
    suffix = f"v{version}" if version else ""
    return f"https://www.{server}.org/content/{preprint_doi}{suffix}"


def _relation(
    current_doi: str,
    item: dict,
    server: str,
) -> dict | None:
    preprint_doi = normalize_doi(
        item.get("biorxiv_doi") or item.get("doi")
    )
    published_doi = normalize_doi(
        item.get("published_doi") or item.get("published")
    )

    if not preprint_doi or not published_doi:
        return None

    if current_doi == preprint_doi:
        direction = "outgoing"
        related_identifier = published_doi
        related_type = "JournalArticle"
        relation_type = "is_preprint_of"
    elif current_doi == published_doi:
        direction = "incoming"
        related_identifier = preprint_doi
        related_type = "preprint"
        relation_type = "is_preprint_of"
    else:
        return None

    return {
        "source": "bioRxiv/medRxiv",
        "direction": direction,
        "relation_type": relation_type,
        "related_identifier": related_identifier,
        "related_identifier_type": "DOI",
        "related_title": item.get("preprint_title") or item.get("title"),
        "related_resource_type": related_type,
        "related_url": _preprint_url(
            server,
            preprint_doi,
            item.get("version"),
        )
        if related_identifier == preprint_doi
        else f"https://doi.org/{published_doi}",
        "raw": item,
    }


async def _collection(
    client: httpx.AsyncClient,
    url: str,
) -> list[dict]:
    response = await client.get(url)

    if response.status_code == 404:
        return []

    response.raise_for_status()
    return response.json().get("collection", [])


async def resolve(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:
    """Resolve a DOI to its bioRxiv or medRxiv counterpart."""
    current_doi = normalize_doi(doi)

    if not current_doi:
        return []

    relations: list[dict] = []
    seen: set[tuple[str, str, str]] = set()

    for server in SERVERS:
        urls = (
            f"{BASE_URL}/details/{server}/{current_doi}/na/json",
            f"{BASE_URL}/pubs/{server}/{current_doi}/na/json",
        )

        for url in urls:
            for item in await _collection(client, url):
                relation = _relation(current_doi, item, server)

                if not relation:
                    continue

                key = (
                    relation["direction"],
                    relation["relation_type"],
                    relation["related_identifier"],
                )

                if key not in seen:
                    seen.add(key)
                    relations.append(relation)

    return relations
