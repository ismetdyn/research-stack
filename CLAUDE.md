# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A personal open-science research workstation: harvest paper metadata from public APIs into Postgres, download
open-access PDFs, and turn them into structured text (GROBID TEI + Docling Markdown/JSON). Almost all code lives in
`apps/harvester/`; the repository root only holds infrastructure (`compose/`, `config/`) and runtime data.

Only `.gitignore` and `exports/.gitkeep` are committed so far — `apps/`, `compose/`, `config/` are untracked.

## Layout gotchas

- **Root path.** `config.ROOT` (also used by `processor.py`, `fulltext.py`, `storage.py`) is `RESEARCH_STACK_ROOT`
  if set (optional), else the repo root derived from the file location; `apps/docling/convert.py` does the same on
  its own. `config` no longer raises on import; a missing `POSTGRES_PASSWORD` raises at the first `db.connect()`.
- **`apps/harvester/` is its own "repository root".** Its README and `desktop/scripts/verify.sh` use paths
  relative to `apps/harvester/` (`cmake -S desktop`, `-s tests`). Run harvester commands from there. CI lives at
  the git root (`.github/workflows/desktop-rhi.yml`) and sets `working-directory: apps/harvester`.
- **Secrets:** `compose/core/.env` (Postgres) and `config/secrets/apis.env` (API keys/mailto) are gitignored and
  loaded by `research_harvester/config.py` via `python-dotenv`. Do not read or print them.
- `data/`, `backups/`, `*.pdf` are gitignored runtime output. `apps/harvester/backups/` and `*.py.bak` are manual
  snapshots, not live code.
- Python is pinned to 3.12 (`>=3.12,<3.13`); each app has its own `uv` project and `.venv`.

## Commands

Infrastructure (Postgres pgvector on `127.0.0.1:5433`, GROBID on `127.0.0.1:8070`):

```sh
cd compose/core && docker compose up -d     # or podman compose
```

Schema: there is no migration runner. Apply in order with `psql`: `config/sql/001..004_*.sql`, then
`apps/harvester/migrations/005_related_outputs.sql`. All statements are `IF NOT EXISTS`.

Harvester (from `apps/harvester/`):

```sh
uv sync
uv run research-harvest --help          # search, list, show, enrich, fulltext, import-pdf, process,
                                        # artifacts, citations, citation-links, related, extract
uv run python -m unittest discover -s tests -v                       # all Python tests
uv run python -m unittest discover -s tests -p test_arxiv_search.py  # one file
uv sync --frozen                        # what CI runs (`.github/workflows/desktop-rhi.yml`); fails if uv.lock is stale
```

Last run (2026-10-01): 37 tests, OK. There is no lint, formatter or
type-checker config.

Native desktop client (from `apps/harvester/`):

```sh
cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Debug -DHARVESTER_DEPENDENCY_PROVIDER=BUNDLED
cmake --build build/desktop --parallel
ctest --test-dir build/desktop --output-on-failure
ctest --test-dir build/desktop -R rhi_loader_test --output-on-failure   # one test
./build/desktop/harvester_desktop --renderer=opengl|vulkan [--frames N]
./desktop/scripts/verify.sh     # build + ctest + unittest + boundary check + Xvfb/Lavapipe smokes
```

`BUNDLED` is the only supported dependency provider; the first configure fetches pinned SDL 3.4.10 and
Dear ImGui 1.92.9 (tags per `desktop/README.md:21`; pinned by commit SHA in `desktop/cmake/HarvesterDependencies.cmake`). `verify.sh` needs `rg`;
windowed smokes are skipped without `xvfb-run`. `verify.sh:13` calls bare `python -m unittest`, so it needs the
harvester `.venv` active.

Host prerequisites: SDL3 is built X11-only, so configure fails with "SDL could not find X11 or Wayland
development libraries" unless the X11/GL dev packages are installed (Fedora: `libX11-devel libXext-devel
mesa-libGL-devel vulkan-loader-devel`; CI's Ubuntu list is in `.github/workflows/desktop-rhi.yml`).
Last run (2026-10-01, fresh `-B` dir): configure + build OK, CTest 12/12 passed.
`apps/harvester/build/desktop` was configured under an old home path — use a fresh `-B` directory; `verify.sh:6`
always uses `build/desktop`, so delete that directory before running it.

Docling (from `apps/docling/`, CPU-only torch index): `uv sync`. It is not run directly — see below.
Docling and `apps/asreview/` have no tests; asreview status is in `docs/ai/project-profile.md` → unknowns.

Verified stack, team map and known failures: `docs/ai/project-profile.md`. Stack decision:
`apps/harvester/docs/decisions/ADR-003-research-workstation-stack.md`.

## Architecture

### Python pipeline (`apps/harvester/research_harvester/`)

`cli.py` (Typer) is a thin dispatcher; each command maps to one module, and `db.py` (raw `psycopg`, no ORM) is the
only persistence layer. The stages, in the order a paper moves through them:

1. **search** — `search.py` fans out concurrently to `sources/*` adapters, each returning `models.WorkCandidate`;
   `dedup.py` merges them (DOI, then fuzzy title via rapidfuzz); `storage.py` writes the raw API responses to
   `data/raw-api/`; `db.save_works` upserts.
2. **enrich** — `enrichment.py`: updates/retractions, metrics, OA locations (Crossref integrity, Unpaywall, …).
3. **fulltext / import-pdf** — `fulltext.py` + `fulltext_sources.py` + `pdf_discovery.py` locate and download an
   OA PDF to `data/papers/<work_id>_<slug>.pdf`; `manual_import.py` registers a local PDF the same way.
4. **process** — `processor.py` posts the PDF to GROBID (→ `data/tei/*.tei.xml`) and shells out to
   `apps/docling/.venv/bin/python apps/docling/process_file.py` (→ `data/docling/*.md|json`). Docling lives in a
   separate venv because of its torch dependency; the harvester never imports it. Outputs are recorded as
   `document_artifacts` with checksums and skipped when present unless `--force`.
5. **extract** — `extractor.py` + `tei_parser.py` turn TEI into `paper_sections` and `parsed_references`.
6. **citations / related** — `citation_graph.py` (OpenCitations, Semantic Scholar) and `related_graph.py`
   (preprint/dataset/version links from arXiv, bioRxiv, DataCite, OSF, Zenodo → `work_relationships`).

Adding a source means adding a `sources/<name>.py` adapter and wiring it into the stage module that uses it.

### Desktop client (`apps/harvester/desktop/`, C++20)

Not a web or Qt UI. SDL3 provides window/input, Dear ImGui produces draw data, and a project-owned, versioned
**RHI** with a C plugin ABI renders it. Backends are shared modules (`rhi_null`, `rhi_opengl`, `rhi_vulkan`)
loaded from an allowlist at startup; renderer choice is startup-only. `null` is the deterministic test oracle, not
a runtime fallback. Rationale: `docs/decisions/ADR-001`, `ADR-002`; ABI policy: `docs/rhi-abi-compatibility.md`.
Progress is tracked in `tasks/plan.md` and `tasks/todo.md`.

Two boundaries are enforced and must not be broken:

- **Renderer neutrality.** `desktop/include/harvester/{app,ui}` and `desktop/src/{desktop_app,ui_renderer_rhi}`
  must not `#include <GL/…>`, `<SDL3/…>` or `<vulkan/…>`. `verify.sh` and CI fail on a match.
- **Worker isolation.** The UI never calls Python directly. `src/harvester_bridge` spawns
  `python -m research_harvester.desktop_bridge` with fixed, validated `argv` (no shell) and schema-validates its
  bounded, versioned stdout protocol. `desktop_bridge.py` and `desktop_fulltext.py` deliberately do **not** import
  `cli`, `db` or `config`: the desktop path is read-only against the database and stores downloads under
  `$XDG_DATA_HOME/research-harvester/papers`, not `data/papers/`. `desktop_fulltext.py` carries the download
  hardening (proxy disabled, per-redirect HTTPS validation, connection pinned to the validated public IP, 64 MiB
  cap, PDF header check, atomic write); remote URLs stay behind opaque candidate IDs.

Windowed Vulkan tests (`*_smoke`) are intentionally outside CTest because they need an X11 display.
