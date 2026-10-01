# ADR-003: Keep the Python + PostgreSQL + GROBID/Docling worker stack behind a native desktop

## Status

Accepted (retroactive)

## Date

2026-10-01

## Context

The research workstation already runs on a stack chosen incrementally; nothing recorded why. This ADR records
the stack as it stands so later readers do not re-open settled choices. The desktop renderer is out of scope:
see ADR-001 and ADR-002. Facts below come from `docs/ai/project-profile.md` (repo root) and two independent
reviews (solution-architect, python-architect) on 2026-10-01.

- Single user, local machine. Postgres and GROBID bind to loopback only (`compose/core/compose.yml:9,23-24`).
- Pipeline: Python 3.12, Typer CLI, async `httpx` fan-out over 12 source adapters
  (`search.py:34,66`, `gather(return_exceptions=True)`), raw `psycopg` with parametrised `ON CONFLICT` upserts
  in `db.py` as the only persistence layer, pydantic only for boundary DTOs (`models.py`).
- Schema: 5 plain SQL files (`config/sql/001-004`, `migrations/005`), `IF NOT EXISTS`, applied by hand.
- PDF → text: GROBID 0.9.1-crf over HTTP; Docling 2.126.0 with CPU torch in its own uv venv, spawned as a
  subprocess with argv, files and one env var `TESSDATA_PREFIX` (`processor.py:250-272`, run off the event loop via `asyncio.to_thread`, `processor.py:366`).
- Desktop ↔ worker: fixed argv + bounded, versioned stdout protocol (`desktop_bridge.py:35-38`).

## Decision

Keep the stack as it is:

1. Python 3.12 + uv (`uv.lock` is the single version source; CI runs `uv sync --frozen`).
2. asyncio + an `httpx.AsyncClient` per stage for network fan-out; sync `psycopg` for the database.
3. PostgreSQL 17 (pgvector image) with schema owned by SQL files; no ORM.
4. GROBID as a container, Docling in a separate venv behind a subprocess boundary (argv, files, env).
5. The native desktop talks to Python only through the worker protocol (ADR-001/002 unchanged).

pgvector stays in the image as a reserve for future embedding work; it costs nothing today.

## Alternatives considered

### SQLite instead of PostgreSQL

Would hold one user's data and drop the container. Rejected because it loses what Postgres gives here:
pgvector for planned embedding work, safe concurrent writers (CLI and future workers), and the existing
Postgres-specific DDL, compose healthcheck and `psql` workflow. Revisit if: the workstation must run without
a container runtime.

### SQLAlchemy (or another ORM)

Rejected: adds a dependency and splits the schema between SQL files and models. Queries are simple and
parametrised. Revisit if: the schema changes often enough that hand-written SQL causes repeated bugs.

### Docling in the harvester process

Rejected: torch and its CPU index would enter every harvester dependency resolution, and a Docling crash would
take down the CLI. Revisit if: Docling drops the torch requirement.

### psycopg `AsyncConnection` (fully async DB)

Would match the async HTTP layer. Rejected: the CLI runs one task at a time, so an async driver adds
complexity without concurrency to serve. Revisit if: several pipeline stages must write concurrently.

### Web UI instead of the native desktop

Excluded as a product constraint (ADR-001, Context), not weighed there. Trade-off on record: a web UI would
add an HTTP server and an auth surface; the stdout protocol has neither.

## Consequences

- Good: few moving parts, each boundary (HTTP, subprocess, stdout protocol, SQL) is testable in isolation.
- Good: no new dependency is needed to fix any defect below.
- Bad — follow-ups, ranked by risk (none changes this decision):
  1. High: hard-coded `Path.home()/"research-stack"` in `config.py`, `processor.py`, `fulltext.py`,
     `storage.py`, `apps/docling/convert.py`, and `config.py` raises at import time without `.env`.
     Breaks test imports on this checkout (current result: `CLAUDE.md` → Commands). Direction: one root from env with a repo-relative
     default; move the password check into `db.connect()`.
  2. Medium: no migration runner or applied-version table; SQL lives in two directories.
  3. Medium: Docling `subprocess.run` has no `timeout` (`processor.py:268`); a bad PDF can hang `process`.
  4. Medium: desktop downloads go to `$XDG_DATA_HOME/research-harvester/papers` and never reach the DB, so
     they skip `process`/`extract`. Bridge, if wanted, is the existing `import-pdf` path.
  5. Low: no lint/format/type-check config; `db.py` has no test against a real Postgres;
     `TESSDATA_PREFIX` is a hard-coded Fedora path (`processor.py:254`); `apps/asreview/` (venv only) and
     `apps/docling/convert.py` (test script) have no stated purpose.
