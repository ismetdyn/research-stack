CREATE TABLE IF NOT EXISTS paper_sections (
    id              BIGSERIAL PRIMARY KEY,
    work_id         BIGINT NOT NULL
                        REFERENCES works(id)
                        ON DELETE CASCADE,
    document_id     BIGINT NOT NULL
                        REFERENCES documents(id)
                        ON DELETE CASCADE,

    section_index   INTEGER NOT NULL,
    section_type    TEXT,
    heading         TEXT,
    text_content    TEXT NOT NULL,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE(document_id, section_index)
);


CREATE INDEX IF NOT EXISTS idx_paper_sections_work
    ON paper_sections(work_id);


CREATE TABLE IF NOT EXISTS parsed_references (
    id              BIGSERIAL PRIMARY KEY,
    work_id         BIGINT NOT NULL
                        REFERENCES works(id)
                        ON DELETE CASCADE,
    document_id     BIGINT NOT NULL
                        REFERENCES documents(id)
                        ON DELETE CASCADE,

    ref_index       INTEGER NOT NULL,

    doi             TEXT,
    title           TEXT,
    publication_year INTEGER,

    authors         JSONB NOT NULL DEFAULT '[]'::jsonb,

    raw_citation    TEXT,
    raw_tei         TEXT,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE(document_id, ref_index)
);


CREATE INDEX IF NOT EXISTS idx_parsed_references_work
    ON parsed_references(work_id);

CREATE INDEX IF NOT EXISTS idx_parsed_references_doi
    ON parsed_references(doi);


CREATE INDEX IF NOT EXISTS idx_citation_links_work_direction
    ON citation_links(work_id, direction);
