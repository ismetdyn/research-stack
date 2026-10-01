from __future__ import annotations

import json

import psycopg
from psycopg.rows import dict_row

from research_harvester import config
from research_harvester.models import MergedWork


def connect():
    # Import anında değil bağlantı anında kontrol edilir; .env'siz import çalışsın.
    if not config.POSTGRES_PASSWORD:
        raise RuntimeError(
            f"POSTGRES_PASSWORD bulunamadı: {config.DB_ENV_FILE}"
        )
    return psycopg.connect(
        host=config.POSTGRES_HOST,
        port=config.POSTGRES_PORT,
        dbname=config.POSTGRES_DB,
        user=config.POSTGRES_USER,
        password=config.POSTGRES_PASSWORD,
        row_factory=dict_row,
    )


def source_id(conn, name: str) -> int:
    with conn.cursor() as cur:
        cur.execute(
            """
            INSERT INTO sources(name, source_type, is_foss)
            VALUES (%s, 'external', false)
            ON CONFLICT (name)
            DO UPDATE SET name = EXCLUDED.name
            RETURNING id
            """,
            (name,),
        )

        return cur.fetchone()["id"]


def _find_existing_without_doi(
    conn,
    title: str,
    year: int | None,
):
    with conn.cursor() as cur:
        cur.execute(
            """
            SELECT id
            FROM works
            WHERE lower(title) = lower(%s)
              AND publication_year IS NOT DISTINCT FROM %s
            LIMIT 1
            """,
            (title, year),
        )

        row = cur.fetchone()
        return row["id"] if row else None


def save_work(
    conn,
    work: MergedWork,
) -> int:

    if work.doi:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO works(
                    doi,
                    title,
                    abstract,
                    publication_year,
                    is_open_access
                )
                VALUES (%s, %s, %s, %s, %s)

                ON CONFLICT (doi)
                DO UPDATE SET
                    title = EXCLUDED.title,
                    abstract = COALESCE(
                        EXCLUDED.abstract,
                        works.abstract
                    ),
                    publication_year = COALESCE(
                        EXCLUDED.publication_year,
                        works.publication_year
                    ),
                    is_open_access = CASE
                        WHEN EXCLUDED.is_open_access IS TRUE
                          OR works.is_open_access IS TRUE
                        THEN TRUE
                        ELSE COALESCE(
                            EXCLUDED.is_open_access,
                            works.is_open_access
                        )
                    END,
                    updated_at = now()

                RETURNING id
                """,
                (
                    work.doi,
                    work.title,
                    work.abstract,
                    work.year,
                    work.is_open_access,
                ),
            )

            work_id = cur.fetchone()["id"]

    else:
        work_id = _find_existing_without_doi(
            conn,
            work.title,
            work.year,
        )

        if work_id is None:
            with conn.cursor() as cur:
                cur.execute(
                    """
                    INSERT INTO works(
                        title,
                        abstract,
                        publication_year,
                        is_open_access
                    )
                    VALUES (%s, %s, %s, %s)
                    RETURNING id
                    """,
                    (
                        work.title,
                        work.abstract,
                        work.year,
                        work.is_open_access,
                    ),
                )

                work_id = cur.fetchone()["id"]

    for source_name, external_id in work.source_ids.items():
        sid = source_id(conn, source_name)

        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO external_ids(
                    work_id,
                    source_id,
                    external_id
                )
                VALUES (%s, %s, %s)

                ON CONFLICT (source_id, external_id)
                DO UPDATE SET
                    work_id = EXCLUDED.work_id
                """,
                (
                    work_id,
                    sid,
                    external_id,
                ),
            )

    return work_id


def save_works(
    works: list[MergedWork],
) -> list[int]:

    ids = []

    with connect() as conn:
        for work in works:
            ids.append(
                save_work(conn, work)
            )

        conn.commit()

    return ids


def list_works(limit: int = 20):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    id,
                    publication_year,
                    title,
                    doi,
                    is_open_access,
                    integrity_status
                FROM works
                ORDER BY id DESC
                LIMIT %s
                """,
                (limit,),
            )

            return cur.fetchall()


def get_work(ref: str):
    with connect() as conn:
        with conn.cursor() as cur:

            if ref.isdigit():
                cur.execute(
                    """
                    SELECT *
                    FROM works
                    WHERE id = %s
                    """,
                    (int(ref),),
                )

            else:
                normalized = ref.lower()

                if normalized.startswith(
                    "https://doi.org/"
                ):
                    normalized = normalized[
                        len("https://doi.org/"):
                    ]

                cur.execute(
                    """
                    SELECT *
                    FROM works
                    WHERE doi = %s
                    """,
                    (normalized,),
                )

            return cur.fetchone()


def replace_updates(
    work_id: int,
    updates: list[dict],
    integrity_status: str,
):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                DELETE FROM work_updates
                WHERE work_id = %s
                """,
                (work_id,),
            )

            for item in updates:
                cur.execute(
                    """
                    INSERT INTO work_updates(
                        work_id,
                        update_type,
                        label,
                        source_name,
                        update_doi,
                        updated_date,
                        raw
                    )
                    VALUES (
                        %s, %s, %s, %s, %s,
                        %s, %s::jsonb
                    )
                    """,
                    (
                        work_id,
                        item.get("type"),
                        item.get("label"),
                        item.get("source"),
                        item.get("DOI"),
                        (
                            item
                            .get("updated", {})
                            .get("date-time")
                        ),
                        json.dumps(item),
                    ),
                )

            cur.execute(
                """
                UPDATE works
                SET integrity_status = %s,
                    updated_at = now()
                WHERE id = %s
                """,
                (
                    integrity_status,
                    work_id,
                ),
            )

        conn.commit()


def save_metrics(
    work_id: int,
    source_name: str,
    citation_count: int | None,
    reference_count: int | None,
    raw: dict,
):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO work_metrics(
                    work_id,
                    source_name,
                    citation_count,
                    reference_count,
                    raw
                )
                VALUES (
                    %s, %s, %s, %s, %s::jsonb
                )

                ON CONFLICT (work_id, source_name)
                DO UPDATE SET
                    citation_count =
                        EXCLUDED.citation_count,
                    reference_count =
                        EXCLUDED.reference_count,
                    raw = EXCLUDED.raw,
                    updated_at = now()
                """,
                (
                    work_id,
                    source_name,
                    citation_count,
                    reference_count,
                    json.dumps(raw),
                ),
            )

        conn.commit()


def replace_oa_locations(
    work_id: int,
    locations: list[dict],
):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                DELETE FROM oa_locations
                WHERE work_id = %s
                  AND source_name = 'Unpaywall'
                """,
                (work_id,),
            )

            for location in locations:
                cur.execute(
                    """
                    INSERT INTO oa_locations(
                        work_id,
                        source_name,
                        url,
                        pdf_url,
                        host_type,
                        version,
                        license,
                        is_best,
                        raw
                    )
                    VALUES (
                        %s,
                        'Unpaywall',
                        %s, %s, %s, %s, %s,
                        %s, %s::jsonb
                    )
                    ON CONFLICT DO NOTHING
                    """,
                    (
                        work_id,
                        location.get("url"),
                        location.get("url_for_pdf"),
                        location.get("host_type"),
                        location.get("version"),
                        location.get("license"),
                        location.get("_is_best", False),
                        json.dumps(location),
                    ),
                )

            is_oa = bool(locations)

            cur.execute(
                """
                UPDATE works
                SET is_open_access = %s,
                    updated_at = now()
                WHERE id = %s
                """,
                (
                    is_oa,
                    work_id,
                ),
            )

        conn.commit()


def get_oa_locations(work_id: int):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT *
                FROM oa_locations
                WHERE work_id = %s
                ORDER BY is_best DESC, id ASC
                """,
                (work_id,),
            )
            return cur.fetchall()


def save_document(
    work_id: int,
    source_name: str,
    local_path: str,
    document_url: str,
    media_type: str,
    sha256: str,
    legally_open: bool | None = True,
):
    with connect() as conn:
        sid = source_id(conn, source_name)

        with conn.cursor() as cur:
            # Aynı dosya zaten kayıtlıysa tekrar ekleme.
            cur.execute(
                """
                SELECT id
                FROM documents
                WHERE work_id = %s
                  AND sha256 = %s
                LIMIT 1
                """,
                (work_id, sha256),
            )

            existing = cur.fetchone()

            if existing:
                return existing["id"]

            cur.execute(
                """
                INSERT INTO documents(
                    work_id,
                    source_id,
                    local_path,
                    document_url,
                    media_type,
                    sha256,
                    legally_open
                )
                VALUES (%s, %s, %s, %s, %s, %s, %s)
                RETURNING id
                """,
                (
                    work_id,
                    sid,
                    local_path,
                    document_url,
                    media_type,
                    sha256,
                    legally_open,
                ),
            )

            document_id = cur.fetchone()["id"]

        conn.commit()

    return document_id


def get_latest_pdf_document(work_id: int):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    d.id,
                    d.work_id,
                    d.local_path,
                    d.document_url,
                    d.media_type,
                    d.sha256,
                    d.legally_open,
                    s.name AS source_name
                FROM documents d
                LEFT JOIN sources s
                  ON s.id = d.source_id
                WHERE d.work_id = %s
                  AND d.media_type = 'application/pdf'
                  AND d.local_path IS NOT NULL
                ORDER BY d.id DESC
                LIMIT 1
                """,
                (work_id,),
            )

            return cur.fetchone()


def save_artifact(
    document_id: int,
    artifact_type: str,
    local_path: str,
    sha256: str,
    processor: str,
    processor_version: str | None = None,
):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO document_artifacts(
                    document_id,
                    artifact_type,
                    local_path,
                    sha256,
                    processor,
                    processor_version
                )
                VALUES (%s, %s, %s, %s, %s, %s)

                ON CONFLICT(document_id, artifact_type)
                DO UPDATE SET
                    local_path = EXCLUDED.local_path,
                    sha256 = EXCLUDED.sha256,
                    processor = EXCLUDED.processor,
                    processor_version =
                        EXCLUDED.processor_version,
                    updated_at = now()

                RETURNING id
                """,
                (
                    document_id,
                    artifact_type,
                    local_path,
                    sha256,
                    processor,
                    processor_version,
                ),
            )

            artifact_id = cur.fetchone()["id"]

        conn.commit()

    return artifact_id


def get_artifacts_for_work(work_id: int):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    a.id,
                    a.document_id,
                    a.artifact_type,
                    a.local_path,
                    a.sha256,
                    a.processor,
                    a.processor_version,
                    a.created_at,
                    a.updated_at
                FROM document_artifacts a
                JOIN documents d
                  ON d.id = a.document_id
                WHERE d.work_id = %s
                ORDER BY a.id
                """,
                (work_id,),
            )

            return cur.fetchall()


def replace_citation_links(
    work_id: int,
    direction: str,
    rows: list[dict],
    source_name: str = "OpenCitations",
):
    with connect() as conn:

        with conn.cursor() as cur:
            cur.execute(
                """
                DELETE FROM citation_links
                WHERE work_id = %s
                  AND direction = %s
                  AND source_name = %s
                """,
                (
                    work_id,
                    direction,
                    source_name,
                ),
            )

            for item in rows:
                cur.execute(
                    """
                    INSERT INTO citation_links(
                        work_id,
                        direction,
                        related_doi,
                        related_pid,
                        oci,
                        creation_date,
                        author_self_citation,
                        journal_self_citation,
                        source_name,
                        raw
                    )
                    VALUES (
                        %s, %s, %s, %s, %s,
                        %s, %s, %s, %s,
                        %s::jsonb
                    )
                    """,
                    (
                        work_id,
                        direction,
                        item.get("related_doi"),
                        item.get("related_pid"),
                        item.get("oci"),
                        item.get("creation"),
                        item.get(
                            "author_self_citation"
                        ),
                        item.get(
                            "journal_self_citation"
                        ),
                        source_name,
                        json.dumps(
                            item.get("raw") or {}
                        ),
                    ),
                )

        conn.commit()


def get_citation_links(
    work_id: int,
    direction: str | None = None,
):
    with connect() as conn:
        with conn.cursor() as cur:

            if direction:
                cur.execute(
                    """
                    SELECT *
                    FROM citation_links
                    WHERE work_id = %s
                      AND direction = %s
                    ORDER BY creation_date DESC NULLS LAST,
                             id
                    """,
                    (
                        work_id,
                        direction,
                    ),
                )

            else:
                cur.execute(
                    """
                    SELECT *
                    FROM citation_links
                    WHERE work_id = %s
                    ORDER BY direction,
                             creation_date DESC NULLS LAST,
                             id
                    """,
                    (work_id,),
                )

            return cur.fetchall()


def replace_work_relationships(
    work_id: int,
    relationships_by_source: dict[str, list[dict]],
):
    """Replace resolved relationships for the sources that completed."""
    with connect() as conn:
        with conn.cursor() as cur:
            for source_name, rows in relationships_by_source.items():
                cur.execute(
                    """
                    DELETE FROM work_relationships
                    WHERE work_id = %s
                      AND source_name = %s
                    """,
                    (work_id, source_name),
                )

                if not rows:
                    continue

                cur.executemany(
                    """
                    INSERT INTO work_relationships(
                        work_id,
                        source_name,
                        direction,
                        relation_type,
                        related_identifier,
                        related_identifier_type,
                        related_title,
                        related_resource_type,
                        related_url,
                        raw
                    )
                    VALUES (
                        %s, %s, %s, %s, %s,
                        %s, %s, %s, %s, %s::jsonb
                    )
                    ON CONFLICT (
                        work_id,
                        source_name,
                        direction,
                        relation_type,
                        related_identifier
                    )
                    DO UPDATE SET
                        related_identifier_type =
                            EXCLUDED.related_identifier_type,
                        related_title = EXCLUDED.related_title,
                        related_resource_type =
                            EXCLUDED.related_resource_type,
                        related_url = EXCLUDED.related_url,
                        raw = EXCLUDED.raw,
                        updated_at = now()
                    """,
                    [
                        (
                            work_id,
                            source_name,
                            item["direction"],
                            item["relation_type"],
                            item["related_identifier"],
                            item["related_identifier_type"],
                            item.get("related_title"),
                            item.get("related_resource_type"),
                            item.get("related_url"),
                            json.dumps(item.get("raw") or {}),
                        )
                        for item in rows
                    ],
                )

        conn.commit()


def get_work_relationships(work_id: int):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    source_name,
                    direction,
                    relation_type,
                    related_identifier,
                    related_identifier_type,
                    related_title,
                    related_resource_type,
                    related_url
                FROM work_relationships
                WHERE work_id = %s
                ORDER BY direction, source_name, relation_type, related_identifier
                """,
                (work_id,),
            )

            return cur.fetchall()


def get_artifact(
    work_id: int,
    artifact_type: str,
):
    with connect() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    a.*,
                    d.work_id
                FROM document_artifacts a
                JOIN documents d
                  ON d.id = a.document_id
                WHERE d.work_id = %s
                  AND a.artifact_type = %s
                ORDER BY a.id DESC
                LIMIT 1
                """,
                (
                    work_id,
                    artifact_type,
                ),
            )

            return cur.fetchone()


def replace_document_structure(
    work_id: int,
    document_id: int,
    sections: list[dict],
    references: list[dict],
):
    with connect() as conn:
        with conn.cursor() as cur:

            cur.execute(
                """
                DELETE FROM paper_sections
                WHERE document_id = %s
                """,
                (document_id,),
            )

            cur.execute(
                """
                DELETE FROM parsed_references
                WHERE document_id = %s
                """,
                (document_id,),
            )

            for section in sections:
                cur.execute(
                    """
                    INSERT INTO paper_sections(
                        work_id,
                        document_id,
                        section_index,
                        section_type,
                        heading,
                        text_content
                    )
                    VALUES (
                        %s, %s, %s, %s, %s, %s
                    )
                    """,
                    (
                        work_id,
                        document_id,
                        section[
                            "section_index"
                        ],
                        section.get(
                            "section_type"
                        ),
                        section.get(
                            "heading"
                        ),
                        section.get(
                            "text_content"
                        )
                        or "",
                    ),
                )

            for ref in references:
                cur.execute(
                    """
                    INSERT INTO parsed_references(
                        work_id,
                        document_id,
                        ref_index,
                        doi,
                        title,
                        publication_year,
                        authors,
                        raw_citation,
                        raw_tei
                    )
                    VALUES (
                        %s, %s, %s, %s, %s,
                        %s, %s::jsonb, %s, %s
                    )
                    """,
                    (
                        work_id,
                        document_id,
                        ref["ref_index"],
                        ref.get("doi"),
                        ref.get("title"),
                        ref.get(
                            "publication_year"
                        ),
                        json.dumps(
                            ref.get(
                                "authors",
                                [],
                            )
                        ),
                        ref.get(
                            "raw_citation"
                        ),
                        ref.get(
                            "raw_tei"
                        ),
                    ),
                )

        conn.commit()


def get_structure_summary(
    work_id: int,
):
    with connect() as conn:
        with conn.cursor() as cur:

            cur.execute(
                """
                SELECT COUNT(*) AS count
                FROM paper_sections
                WHERE work_id = %s
                """,
                (work_id,),
            )

            section_count = (
                cur.fetchone()["count"]
            )

            cur.execute(
                """
                SELECT
                    COUNT(*) AS total,
                    COUNT(doi) AS with_doi
                FROM parsed_references
                WHERE work_id = %s
                """,
                (work_id,),
            )

            refs = cur.fetchone()

            return {
                "sections":
                    section_count,
                "references":
                    refs["total"],
                "references_with_doi":
                    refs["with_doi"],
            }
