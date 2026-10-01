# Project profile — research-stack

Department spec §11.1 format. Every value is verified from a file (path:line) or a command run on
2026-10-01; anything unverified is `unknown`. Commands live in `CLAUDE.md` → "Commands"; this file
does not repeat them.

```yaml
project_profile:
  worker:                                   # apps/harvester/
    language: python
    version: ">=3.12,<3.13"                 # apps/harvester/pyproject.toml:5
    framework: typer 0.27.2 CLI, no web framework, no ORM   # uv.lock, pyproject.toml:19-20
    packaging: uv + hatchling               # pyproject.toml:22-24
    key_deps: [psycopg, httpx, pydantic, rapidfuzz, lxml, beautifulsoup4, python-dotenv]   # versions: uv.lock
    modules: [research_harvester (20 modules), research_harvester/sources (12 adapters)]
  docling:                                  # apps/docling/
    language: python ">=3.12,<3.13"
    deps: [docling, torch (CPU index)]   # versions: apps/docling/uv.lock
    entry: process_file.py, spawned by research_harvester/processor.py (separate venv)
  desktop:                                  # apps/harvester/desktop/
    language: C17 + C++20, -Werror          # CMakeLists.txt:5-31
    build: cmake >=3.28, FetchContent BUNDLED only
    libs: [SDL3, Dear ImGui, Vulkan-Headers, volk]   # commit-SHA pins: cmake/HarvesterDependencies.cmake:70,82,101,113
          # release tags (SDL 3.4.10, ImGui 1.92.9) stated only in desktop/README.md:21
    renderers: [rhi_null (test oracle), rhi_opengl, rhi_vulkan]   # ADR-001, ADR-002
  data:
    engines: [postgresql 17]                # compose/core/compose.yml:3 (pgvector/pgvector:pg17)
    extensions: pgvector shipped in image, unused by schema or code
    migrations: plain SQL, no runner, hand-applied: config/sql/001-004, apps/harvester/migrations/005
  external_services:
    grobid: grobid/grobid:0.9.1-crf on 127.0.0.1:8070 (+8071)   # compose/core/compose.yml:19-24
    apis: [OpenAlex, Crossref, Semantic Scholar, OpenAIRE, Unpaywall, OpenCitations,
           bioRxiv, OSF, DataCite, Zenodo, arXiv]   # research_harvester/sources/*.py
  identity: none (local single-user; no auth in any manifest)
  delivery:
    ci: .github/workflows/desktop-rhi.yml (working-directory: apps/harvester)
    lint_format_typecheck: none configured
  tests:
    python: stdlib unittest, 6 files (current result: CLAUDE.md → Commands)
    ctest: 12 tests (CMakeLists.txt:265-432); Vulkan *_smoke binaries outside CTest
  evidence:
    files: [apps/harvester/pyproject.toml, apps/harvester/uv.lock, apps/docling/pyproject.toml,
            apps/harvester/desktop/CMakeLists.txt, apps/harvester/desktop/cmake/HarvesterDependencies.cmake,
            apps/harvester/desktop/scripts/verify.sh, compose/core/compose.yml, config/sql/*.sql]
  unknowns:
    - apps/asreview/: only a .venv (asreview 3.0.8) + synergy_cache.sqlite; no manifest, unreferenced.
      Its .venv/pyvenv.cfg points to /home/ismetdayan/... (old home path) — likely broken.
    - Vulkan-Headers and volk release versions (only commit pins).
```

## Known failures

Tracked in `CLAUDE.md` → "Commands" (test result, desktop host prerequisites, stale build dir).

## Team map (department spec §11.3)

| Role | Agent | Owns |
|---|---|---|
| Discovery | codebase-explorer | — (read-only) |
| Architecture | solution-architect, python-architect, cpp-architect | — (read-only) |
| Implementation | python-developer | `apps/harvester/research_harvester/**`, `apps/docling/**` |
| Implementation | cpp-developer | `apps/harvester/desktop/**` |
| Data | postgresql-specialist | `config/sql/**`, `apps/harvester/migrations/**` |
| Quality | python-test-engineer, cpp-test-engineer | `apps/harvester/tests/**`, `apps/harvester/desktop/tests/**` |
| Review | python-reviewer, cpp-reviewer, postgresql-reviewer | — (read-only) |
| Security | security-reviewer | — for `desktop_fulltext.py` download hardening, secrets loading |
| Operations | devops-specialist | `compose/**`, `.github/**` |

Shared contracts with a single owner: the C++↔Python worker protocol (`desktop/src/harvester_bridge` ↔
`research_harvester/desktop_bridge.py`) — change on both sides only after the protocol version is agreed;
SQL schema ↔ `db.py` — postgresql-specialist fixes the SQL first. No IAM/Keycloak, no frontend web stack.
