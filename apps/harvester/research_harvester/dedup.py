from __future__ import annotations

from rapidfuzz.fuzz import ratio

from research_harvester.models import (
    MergedWork,
    WorkCandidate,
)


TITLE_THRESHOLD = 94


def normalize_title(title: str) -> str:
    return " ".join(
        title.lower()
        .replace(":", " ")
        .replace("-", " ")
        .split()
    )


def merge_into(
    target: MergedWork,
    candidate: WorkCandidate,
) -> None:

    if not target.doi and candidate.doi:
        target.doi = candidate.doi

    if not target.year and candidate.year:
        target.year = candidate.year

    if (
        not target.abstract
        and candidate.abstract
    ):
        target.abstract = candidate.abstract

    for author in candidate.authors:
        if author not in target.authors:
            target.authors.append(author)

    if (
        candidate.url
        and candidate.url not in target.urls
    ):
        target.urls.append(candidate.url)

    if (
        candidate.open_access_url
        and candidate.open_access_url
        not in target.open_access_urls
    ):
        target.open_access_urls.append(
            candidate.open_access_url
        )

    if (
        candidate.citation_count is not None
        and (
            target.citation_count is None
            or candidate.citation_count
            > target.citation_count
        )
    ):
        target.citation_count = (
            candidate.citation_count
        )

    if candidate.is_open_access is True:
        target.is_open_access = True

    if candidate.source not in target.sources:
        target.sources.append(candidate.source)

    if candidate.source_id:
        target.source_ids[
            candidate.source
        ] = candidate.source_id


def deduplicate(
    candidates: list[WorkCandidate],
) -> list[MergedWork]:

    merged: list[MergedWork] = []

    by_doi: dict[str, MergedWork] = {}

    for candidate in candidates:

        if not candidate.title:
            continue

        if candidate.doi:
            existing = by_doi.get(candidate.doi)

            if existing:
                merge_into(existing, candidate)
                continue

        normalized = normalize_title(
            candidate.title
        )

        matched = None

        for work in merged:
            score = ratio(
                normalized,
                normalize_title(work.title),
            )

            year_compatible = (
                not candidate.year
                or not work.year
                or candidate.year == work.year
            )

            if (
                score >= TITLE_THRESHOLD
                and year_compatible
            ):
                matched = work
                break

        if matched:
            merge_into(matched, candidate)

            if candidate.doi:
                by_doi[candidate.doi] = matched

            continue

        new_work = MergedWork(
            title=candidate.title,
            doi=candidate.doi,
            year=candidate.year,
            abstract=candidate.abstract,
            authors=list(candidate.authors),
            urls=[
                candidate.url
            ] if candidate.url else [],
            open_access_urls=[
                candidate.open_access_url
            ] if candidate.open_access_url else [],
            citation_count=(
                candidate.citation_count
            ),
            is_open_access=(
                candidate.is_open_access
            ),
            sources=[candidate.source],
            source_ids={
                candidate.source:
                    candidate.source_id
            }
            if candidate.source_id
            else {},
        )

        merged.append(new_work)

        if candidate.doi:
            by_doi[candidate.doi] = new_work

    return merged
