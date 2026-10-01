from __future__ import annotations

import re
from typing import Any


DOI_PREFIXES = (
    "https://doi.org/",
    "http://doi.org/",
    "http://dx.doi.org/",
    "https://dx.doi.org/",
    "doi:",
)


def normalize_doi(value: str | None) -> str | None:
    if not value:
        return None

    doi = value.strip()

    lower = doi.lower()

    for prefix in DOI_PREFIXES:
        if lower.startswith(prefix):
            doi = doi[len(prefix):]
            break

    doi = doi.strip().lower()

    if not doi.startswith("10."):
        return None

    return doi


def normalize_relation_type(value: str | None) -> str:
    if not value:
        return "related_to"

    value = re.sub(r"(?<!^)(?=[A-Z])", "_", value)
    return re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")


def safe_year(value: Any) -> int | None:
    if value is None:
        return None

    if isinstance(value, int):
        return value

    match = re.search(r"\b(18|19|20|21)\d{2}\b", str(value))
    if not match:
        return None

    return int(match.group(0))


def reconstruct_openalex_abstract(
    inverted_index: dict[str, list[int]] | None,
) -> str | None:
    if not inverted_index:
        return None

    words: list[tuple[int, str]] = []

    for word, positions in inverted_index.items():
        for position in positions:
            words.append((position, word))

    words.sort(key=lambda item: item[0])

    return " ".join(word for _, word in words)


def first_non_empty(*values):
    for value in values:
        if value is not None and value != "" and value != []:
            return value
    return None
