CREATE TABLE IF NOT EXISTS work_relationships (
    id                      BIGSERIAL PRIMARY KEY,
    work_id                 BIGINT NOT NULL
                                REFERENCES works(id)
                                ON DELETE CASCADE,
    source_name             TEXT NOT NULL,
    direction               TEXT NOT NULL
                                CHECK (direction IN ('incoming', 'outgoing')),
    relation_type           TEXT NOT NULL,
    related_identifier      TEXT NOT NULL,
    related_identifier_type TEXT NOT NULL,
    related_title           TEXT,
    related_resource_type   TEXT,
    related_url             TEXT,
    raw                     JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at              TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at              TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE (
        work_id,
        source_name,
        direction,
        relation_type,
        related_identifier
    )
);

CREATE INDEX IF NOT EXISTS idx_work_relationships_work
    ON work_relationships(work_id);

CREATE INDEX IF NOT EXISTS idx_work_relationships_related_identifier
    ON work_relationships(related_identifier);
