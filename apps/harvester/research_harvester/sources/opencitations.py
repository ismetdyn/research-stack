from __future__ import annotations

import re
from urllib.parse import quote

import httpx


BASE = "https://api.opencitations.net/index/v2"


DOI_PATTERN = re.compile(
    r"doi:(10\.\d{4,9}/[^\s;]+)",
    flags=re.IGNORECASE,
)


def _headers(token: str | None):
    headers = {}

    if token:
        headers["authorization"] = token

    return headers


def extract_dois(value: str | None) -> list[str]:
    if not value:
        return []

    results: list[str] = []

    for match in DOI_PATTERN.findall(value):
        doi = (
            match
            .strip()
            .rstrip(".,;)]}")
            .lower()
        )

        if doi not in results:
            results.append(doi)

    return results


def bool_from_oc(value: str | None):
    if not value:
        return None

    value = value.lower().strip()

    if value.endswith("yes"):
        return True

    if value.endswith("no"):
        return False

    return None


async def counts(
    client: httpx.AsyncClient,
    doi: str,
    token: str | None = None,
):
    identifier = quote(
        f"doi:{doi}",
        safe=":",
    )

    headers = _headers(token)

    citation_response = await client.get(
        f"{BASE}/citation-count/{identifier}",
        headers=headers,
    )

    citation_response.raise_for_status()

    reference_response = await client.get(
        f"{BASE}/reference-count/{identifier}",
        headers=headers,
    )

    reference_response.raise_for_status()

    citation_data = citation_response.json()
    reference_data = reference_response.json()

    citation_count = (
        int(citation_data[0]["count"])
        if citation_data
        else 0
    )

    reference_count = (
        int(reference_data[0]["count"])
        if reference_data
        else 0
    )

    return {
        "citation_count": citation_count,
        "reference_count": reference_count,
        "raw": {
            "citation": citation_data,
            "reference": reference_data,
        },
    }


async def _relations(
    client: httpx.AsyncClient,
    doi: str,
    endpoint: str,
    direction: str,
    token: str | None = None,
):
    identifier = quote(
        f"doi:{doi}",
        safe=":",
    )

    response = await client.get(
        f"{BASE}/{endpoint}/{identifier}",
        headers=_headers(token),
    )

    response.raise_for_status()

    payload = response.json()

    rows = []

    for item in payload:
        if not isinstance(item, dict):
            continue

        if direction == "outgoing":
            related_raw = item.get("cited")
        else:
            related_raw = item.get("citing")

        related_dois = extract_dois(
            related_raw
        )

        related_doi = (
            related_dois[0]
            if related_dois
            else None
        )

        rows.append(
            {
                "direction": direction,
                "related_doi": related_doi,
                "related_pid": related_raw,
                "oci": item.get("oci"),
                "creation": item.get(
                    "creation"
                ),
                "author_self_citation":
                    bool_from_oc(
                        item.get("author_sc")
                    ),
                "journal_self_citation":
                    bool_from_oc(
                        item.get("journal_sc")
                    ),
                "raw": item,
            }
        )

    return rows


async def references(
    client: httpx.AsyncClient,
    doi: str,
    token: str | None = None,
):
    return await _relations(
        client=client,
        doi=doi,
        endpoint="references",
        direction="outgoing",
        token=token,
    )


async def citations(
    client: httpx.AsyncClient,
    doi: str,
    token: str | None = None,
):
    return await _relations(
        client=client,
        doi=doi,
        endpoint="citations",
        direction="incoming",
        token=token,
    )
