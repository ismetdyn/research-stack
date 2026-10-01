from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv


_ROOT_OVERRIDE = os.getenv("RESEARCH_STACK_ROOT")
ROOT = (
    Path(_ROOT_OVERRIDE).expanduser().resolve()
    if _ROOT_OVERRIDE
    else Path(__file__).resolve().parents[3]
)

# Editable olmayan kurulumda __file__ site-packages'tadır; yanlış köke sessizce
# veri yazmak yerine dur.
if not _ROOT_OVERRIDE and not (ROOT / "compose").is_dir():
    raise RuntimeError(
        f"Repo kökü bulunamadı: {ROOT} — RESEARCH_STACK_ROOT ayarlayın."
    )

API_ENV_FILE = ROOT / "config" / "secrets" / "apis.env"

DB_ENV_FILE = ROOT / "compose" / "core" / ".env"

load_dotenv(API_ENV_FILE)
load_dotenv(DB_ENV_FILE)


OPENALEX_API_KEY = os.getenv("OPENALEX_API_KEY") or None

SEMANTIC_SCHOLAR_API_KEY = (
    os.getenv("SEMANTIC_SCHOLAR_API_KEY") or None
)

OPENCITATIONS_ACCESS_TOKEN = (
    os.getenv("OPENCITATIONS_ACCESS_TOKEN") or None
)

CROSSREF_MAILTO = (
    os.getenv("CROSSREF_MAILTO") or None
)

UNPAYWALL_EMAIL = (
    os.getenv("UNPAYWALL_EMAIL") or None
)

CORE_API_KEY = (
    os.getenv("CORE_API_KEY") or None
)


POSTGRES_HOST = "127.0.0.1"
POSTGRES_PORT = 5433
POSTGRES_USER = os.getenv("POSTGRES_USER", "research")
POSTGRES_DB = os.getenv("POSTGRES_DB", "research")
POSTGRES_PASSWORD = os.getenv("POSTGRES_PASSWORD")

