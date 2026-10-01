CREATE TABLE IF NOT EXISTS sources (
    id              BIGSERIAL PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    source_type     TEXT NOT NULL,
    base_url        TEXT,
    is_foss         BOOLEAN NOT NULL DEFAULT FALSE,
    notes           TEXT
);

CREATE TABLE IF NOT EXISTS works (
    id                BIGSERIAL PRIMARY KEY,
    doi               TEXT UNIQUE,
    title             TEXT NOT NULL,
    abstract          TEXT,
    publication_year  INTEGER,
    work_type         TEXT,
    language          TEXT,
    is_open_access    BOOLEAN,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS external_ids (
    id          BIGSERIAL PRIMARY KEY,
    work_id     BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    source_id   BIGINT NOT NULL REFERENCES sources(id),
    external_id TEXT NOT NULL,
    raw_url     TEXT,
    UNIQUE(source_id, external_id)
);

CREATE TABLE IF NOT EXISTS citations (
    citing_work_id BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    cited_work_id  BIGINT NOT NULL REFERENCES works(id) ON DELETE CASCADE,
    source_id      BIGINT REFERENCES sources(id),
    PRIMARY KEY(citing_work_id, cited_work_id)
);

CREATE TABLE IF NOT EXISTS documents (
    id            BIGSERIAL PRIMARY KEY,
    work_id       BIGINT REFERENCES works(id) ON DELETE SET NULL,
    source_id     BIGINT REFERENCES sources(id),
    local_path    TEXT,
    document_url  TEXT,
    media_type    TEXT,
    sha256        TEXT,
    legally_open  BOOLEAN,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS evidence (
    id              BIGSERIAL PRIMARY KEY,
    work_id         BIGINT REFERENCES works(id) ON DELETE CASCADE,
    source_id       BIGINT REFERENCES sources(id),
    section         TEXT,
    quote_or_claim  TEXT NOT NULL,
    evidence_type   TEXT,
    page_number     INTEGER,
    confidence      REAL,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS source_queries (
    id            BIGSERIAL PRIMARY KEY,
    source_id     BIGINT REFERENCES sources(id),
    query_text    TEXT NOT NULL,
    requested_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    raw_response_path TEXT
);

INSERT INTO sources(name, source_type, is_foss)
VALUES
('OpenAlex', 'academic_graph', TRUE),
('OpenAIRE', 'academic_graph', TRUE),
('Crossref', 'metadata', TRUE),
('OpenCitations', 'citation_graph', TRUE),
('Unpaywall', 'fulltext_resolver', TRUE),
('CORE', 'fulltext', TRUE),
('Semantic Scholar', 'discovery', FALSE),
('Sci-Bot', 'external_ai_research', FALSE)
ON CONFLICT (name) DO NOTHING;
