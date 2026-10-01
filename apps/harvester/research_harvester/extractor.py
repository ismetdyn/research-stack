from __future__ import annotations

from pathlib import Path

from research_harvester import db
from research_harvester.tei_parser import (
    parse_tei,
)


def extract_work_structure(
    work: dict,
):
    artifact = db.get_artifact(
        work["id"],
        "grobid_tei",
    )

    if not artifact:
        raise RuntimeError(
            "GROBID TEI bulunamadı. "
            f"Önce çalıştır:\n"
            f"research-harvest process "
            f"{work['id']}"
        )

    path = Path(
        artifact["local_path"]
    )

    if not path.exists():
        raise RuntimeError(
            f"TEI dosyası bulunamadı: "
            f"{path}"
        )

    parsed = parse_tei(
        path
    )

    db.replace_document_structure(
        work_id=work["id"],
        document_id=artifact[
            "document_id"
        ],
        sections=parsed[
            "sections"
        ],
        references=parsed[
            "references"
        ],
    )

    return {
        "work_id":
            work["id"],
        "tei":
            str(path),
        "sections":
            len(
                parsed["sections"]
            ),
        "references":
            len(
                parsed["references"]
            ),
        "references_with_doi":
            sum(
                1
                for item
                in parsed["references"]
                if item.get("doi")
            ),
    }
