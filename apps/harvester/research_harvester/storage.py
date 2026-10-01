from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from research_harvester import config
from research_harvester.models import WorkCandidate


RAW_DIR = (
    config.ROOT
    / "data"
    / "raw-api"
)


def save_raw(
    query: str,
    candidates: list[WorkCandidate],
) -> Path:

    RAW_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    timestamp = datetime.now(
        timezone.utc
    ).strftime("%Y%m%dT%H%M%SZ")

    safe_query = "".join(
        c if c.isalnum() else "_"
        for c in query[:50]
    )

    path = (
        RAW_DIR
        / f"{timestamp}_{safe_query}.json"
    )

    payload = {
        "query": query,
        "timestamp": timestamp,
        "count": len(candidates),
        "results": [
            {
                "normalized":
                    item.model_dump(
                        exclude={"raw"}
                    ),
                "raw": item.raw,
            }
            for item in candidates
        ],
    }

    path.write_text(
        json.dumps(
            payload,
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    return path
