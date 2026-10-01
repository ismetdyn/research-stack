from __future__ import annotations

import httpx

from research_harvester.utils import normalize_doi, normalize_relation_type


BASE_URL = "https://zenodo.org/api/records"


def _records(payload: dict) -> list[dict]:
    hits = payload.get("hits", {})
    records = hits.get("hits", []) if isinstance(hits, dict) else hits or []
    return [record for record in records if isinstance(record, dict)]


def _record_doi(record: dict, metadata: dict) -> str | None:
    pids = record.get("pids") or {}
    doi_pid = pids.get("doi") or {}

    return normalize_doi(
        metadata.get("doi")
        or record.get("doi")
        or doi_pid.get("identifier")
    )


def _resource_type(metadata: dict) -> str | None:
    resource_type = metadata.get("resource_type") or {}

    if isinstance(resource_type, dict):
        return (
            resource_type.get("type")
            or resource_type.get("id")
            or metadata.get("upload_type")
        )

    return metadata.get("upload_type")


async def resolve(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:
    """Find Zenodo records that declare a relationship to a DOI."""
    current_doi = normalize_doi(doi)

    if not current_doi:
        return []

    response = await client.get(
        BASE_URL,
        params={
            "q": f'related.identifier:"{current_doi}"',
            "size": 25,
        },
    )

    # Zenodo'nun alan-adlı Elasticsearch sorgusu bazı kayıtlar için
    # zaman aşımına uğrayabiliyor. Genel DOI araması daha dar olmayan
    # bir fallback'tir; sonuçları aşağıda yine tam ilişkiyle filtreliyoruz.
    if 500 <= response.status_code < 600:
        response = await client.get(
            BASE_URL,
            params={"q": f'"{current_doi}"', "size": 25},
        )

    response.raise_for_status()

    relations: list[dict] = []

    for record in _records(response.json()):
        metadata = record.get("metadata") or {}
        record_doi = _record_doi(record, metadata)
        record_id = str(record.get("id") or "")
        identifier = record_doi or f"zenodo:{record_id}"
        links = record.get("links") or {}
        record_url = (
            links.get("html")
            or links.get("self_html")
            or (f"https://zenodo.org/records/{record_id}" if record_id else None)
        )

        for related in metadata.get("related_identifiers") or []:
            related_doi = normalize_doi(related.get("identifier"))

            if related_doi != current_doi:
                continue

            is_current_record = record_doi == current_doi

            relations.append(
                {
                    "source": "Zenodo",
                    "direction": "outgoing" if is_current_record else "incoming",
                    "relation_type": normalize_relation_type(
                        related.get("relation")
                    ),
                    "related_identifier": (
                        related_doi if is_current_record else identifier
                    ),
                    "related_identifier_type": "DOI" if record_doi else "Zenodo",
                    "related_title": metadata.get("title"),
                    "related_resource_type": _resource_type(metadata),
                    "related_url": (
                        f"https://doi.org/{related_doi}"
                        if is_current_record
                        else record_url
                    ),
                    "raw": {"record": record, "related": related},
                }
            )

    return relations
