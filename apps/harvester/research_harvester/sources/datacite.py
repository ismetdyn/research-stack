from __future__ import annotations

from urllib.parse import quote

import httpx

from research_harvester.utils import normalize_doi, normalize_relation_type


BASE_URL = "https://api.datacite.org/dois"


def _identifier(value: str, identifier_type: str) -> str:
    if identifier_type.upper() == "DOI":
        return normalize_doi(value) or value.strip()

    return value.strip()


def _url(identifier: str, identifier_type: str) -> str | None:
    if identifier_type.upper() == "DOI":
        return f"https://doi.org/{identifier}"

    if identifier.startswith(("https://", "http://")):
        return identifier

    return None


def _title(attributes: dict) -> str | None:
    for item in attributes.get("titles") or []:
        if isinstance(item, dict) and item.get("title"):
            return item["title"]

        if isinstance(item, str) and item:
            return item

    return None


def _related_identifiers(attributes: dict) -> list[dict]:
    return attributes.get("relatedIdentifiers") or []


def _direct_relations(attributes: dict) -> list[dict]:
    relations: list[dict] = []

    for item in _related_identifiers(attributes):
        raw_identifier = item.get("relatedIdentifier")
        identifier_type = item.get("relatedIdentifierType") or "Other"

        if not isinstance(raw_identifier, str) or not raw_identifier.strip():
            continue

        identifier = _identifier(raw_identifier, identifier_type)

        relations.append(
            {
                "source": "DataCite",
                "direction": "outgoing",
                "relation_type": normalize_relation_type(
                    item.get("relationType")
                ),
                "related_identifier": identifier,
                "related_identifier_type": identifier_type,
                "related_title": None,
                "related_resource_type": item.get("resourceTypeGeneral"),
                "related_url": _url(identifier, identifier_type),
                "raw": item,
            }
        )

    return relations


def _reverse_relations(
    current_doi: str,
    records: list[dict],
) -> list[dict]:
    relations: list[dict] = []

    for record in records:
        attributes = record.get("attributes") or {}
        record_doi = normalize_doi(record.get("id"))

        if not record_doi or record_doi == current_doi:
            continue

        for item in _related_identifiers(attributes):
            identifier = _identifier(
                item.get("relatedIdentifier") or "",
                item.get("relatedIdentifierType") or "Other",
            )

            if normalize_doi(identifier) != current_doi:
                continue

            relations.append(
                {
                    "source": "DataCite",
                    "direction": "incoming",
                    "relation_type": normalize_relation_type(
                        item.get("relationType")
                    ),
                    "related_identifier": record_doi,
                    "related_identifier_type": "DOI",
                    "related_title": _title(attributes),
                    "related_resource_type": (
                        attributes.get("types", {})
                        .get("resourceTypeGeneral")
                    ),
                    "related_url": f"https://doi.org/{record_doi}",
                    "raw": {"record": record, "related": item},
                }
            )

    return relations


async def resolve(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:
    """Read DataCite related identifiers declared for a DOI."""
    current_doi = normalize_doi(doi)

    if not current_doi:
        return []

    response = await client.get(
        f"{BASE_URL}/{quote(current_doi, safe='')}",
    )

    if response.status_code != 404:
        response.raise_for_status()
        attributes = response.json().get("data", {}).get("attributes", {})
        return _direct_relations(attributes)

    response = await client.get(
        BASE_URL,
        params={
            "query": (
                "relatedIdentifiers.relatedIdentifier:"
                f'"{current_doi}"'
            ),
            "page[size]": 25,
        },
    )
    response.raise_for_status()

    return _reverse_relations(
        current_doi,
        response.json().get("data") or [],
    )
