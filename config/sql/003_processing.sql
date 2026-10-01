CREATE TABLE IF NOT EXISTS document_artifacts (
    id                  BIGSERIAL PRIMARY KEY,
    document_id         BIGINT NOT NULL
                            REFERENCES documents(id)
                            ON DELETE CASCADE,

    artifact_type       TEXT NOT NULL,
    local_path          TEXT NOT NULL,
    sha256              TEXT NOT NULL,

    processor           TEXT NOT NULL,
    processor_version   TEXT,

    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now(),

    UNIQUE(document_id, artifact_type)
);

CREATE INDEX IF NOT EXISTS idx_document_artifacts_document
    ON document_artifacts(document_id);

CREATE INDEX IF NOT EXISTS idx_document_artifacts_type
    ON document_artifacts(artifact_type);
