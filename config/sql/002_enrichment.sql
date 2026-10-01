ALTER TABLE works
    ADD COLUMN IF NOT EXISTS integrity_status TEXT NOT NULL DEFAULT 'unknown';

CREATE TABLE IF NOT EXISTS work_updates (
    id              BIGSERIAL PRIMARY KEY,
    work_id         BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    update_type     TEXT,
    label           TEXT,
    source_name     TEXT,
    update_doi      TEXT,
    updated_date    TIMESTAMPTZ,
    raw             JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_work_updates_work
    ON work_updates(work_id);

CREATE TABLE IF NOT EXISTS work_metrics (
    work_id          BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    source_name      TEXT NOT NULL,
    citation_count   INTEGER,
    reference_count  INTEGER,
    raw              JSONB NOT NULL DEFAULT '{}'::jsonb,
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT now(),

    PRIMARY KEY (work_id, source_name)
);

CREATE TABLE IF NOT EXISTS oa_locations (
    id            BIGSERIAL PRIMARY KEY,
    work_id       BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    source_name   TEXT NOT NULL,
    url           TEXT,
    pdf_url       TEXT,
    host_type     TEXT,
    version       TEXT,
    license       TEXT,
    is_best       BOOLEAN NOT NULL DEFAULT FALSE,
    raw           JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS uq_oa_location_work_url
    ON oa_locations(work_id, COALESCE(url, ''), COALESCE(pdf_url, ''));

CREATE TABLE IF NOT EXISTS citation_links (
    id                    BIGSERIAL PRIMARY KEY,
    work_id               BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    direction             TEXT NOT NULL CHECK (direction IN ('incoming', 'outgoing')),
    related_doi           TEXT,
    related_pid           TEXT,
    oci                   TEXT,
    creation_date         DATE,
    author_self_citation  BOOLEAN,
    journal_self_citation BOOLEAN,
    source_name           TEXT NOT NULL,
    raw                   JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at            TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_citation_links_work
    ON citation_links(work_id);

CREATE INDEX IF NOT EXISTS idx_citation_links_related_doi
    ON citation_links(related_doi);
