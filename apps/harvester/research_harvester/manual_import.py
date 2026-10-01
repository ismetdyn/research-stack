from __future__ import annotations

import shutil
from pathlib import Path

from research_harvester import db
from research_harvester.fulltext import PAPER_DIR, sha256_file, slugify


def import_pdf(
    work: dict,
    source_path: Path,
) -> dict:
    """Copy a user-supplied PDF into the local research library."""
    source_path = source_path.expanduser().resolve()

    if not source_path.is_file():
        raise FileNotFoundError(source_path)

    with source_path.open("rb") as source_file:
        if source_file.read(5) != b"%PDF-":
            raise RuntimeError(
                "Dosya geçerli bir PDF gibi görünmüyor."
            )

    checksum = sha256_file(source_path)
    PAPER_DIR.mkdir(parents=True, exist_ok=True)

    filename = f"{work['id']}_{slugify(work['title'])}.pdf"
    destination = PAPER_DIR / filename

    if destination.exists() and sha256_file(destination) != checksum:
        destination = PAPER_DIR / (
            f"{destination.stem}_{checksum[:12]}.pdf"
        )

    if source_path != destination:
        shutil.copy2(source_path, destination)

    document_id = db.save_document(
        work_id=work["id"],
        source_name="Manual import",
        local_path=str(destination),
        document_url=source_path.as_uri(),
        media_type="application/pdf",
        sha256=checksum,
        legally_open=None,
    )

    return {
        "success": True,
        "access_status": "local_import",
        "document_id": document_id,
        "path": str(destination),
        "sha256": checksum,
        "bytes": destination.stat().st_size,
        "source": "Manual import",
        "source_url": source_path.as_uri(),
        "license": None,
    }
