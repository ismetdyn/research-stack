from __future__ import annotations

import asyncio
import hashlib
import os
import subprocess
from pathlib import Path

import httpx

from research_harvester import config, db


TEI_DIR = (
    config.ROOT
    / "data"
    / "tei"
)

DOCLING_DIR = (
    config.ROOT
    / "data"
    / "docling"
)

DOCLING_PYTHON = (
    config.ROOT
    / "apps"
    / "docling"
    / ".venv"
    / "bin"
    / "python"
)

DOCLING_SCRIPT = (
    config.ROOT
    / "apps"
    / "docling"
    / "process_file.py"
)

GROBID_URL = (
    "http://127.0.0.1:8070"
    "/api/processFulltextDocument"
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as f:
        while True:
            chunk = f.read(
                1024 * 1024
            )

            if not chunk:
                break

            digest.update(chunk)

    return digest.hexdigest()


def output_stem(
    work_id: int,
    pdf_path: Path,
) -> str:
    # Fulltext downloader zaten:
    # 6_some-paper.pdf
    # biçiminde kaydediyor.

    stem = pdf_path.stem

    if stem.startswith(
        f"{work_id}_"
    ):
        return stem

    return f"{work_id}_{stem}"


async def process_grobid(
    document: dict,
    work_id: int,
    force: bool = False,
):
    pdf_path = Path(
        document["local_path"]
    )

    stem = output_stem(
        work_id,
        pdf_path,
    )

    TEI_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    output_path = (
        TEI_DIR
        / f"{stem}.tei.xml"
    )

    if (
        output_path.exists()
        and not force
    ):
        checksum = sha256_file(
            output_path
        )

        artifact_id = db.save_artifact(
            document_id=document["id"],
            artifact_type="grobid_tei",
            local_path=str(output_path),
            sha256=checksum,
            processor="GROBID",
            processor_version="0.9.1-crf",
        )

        return {
            "status": "existing",
            "artifact_id": artifact_id,
            "path": str(output_path),
            "sha256": checksum,
        }

    timeout = httpx.Timeout(
        300.0,
        connect=10.0,
    )

    async with httpx.AsyncClient(
        timeout=timeout,
    ) as client:

        with pdf_path.open("rb") as f:
            response = await client.post(
                GROBID_URL,
                files={
                    "input": (
                        pdf_path.name,
                        f,
                        "application/pdf",
                    )
                },
                data={
                    # Kendi metadata enrichment
                    # sistemimiz olduğu için GROBID'in
                    # harici consolidation işlemlerini
                    # burada kapatıyoruz.
                    "consolidateHeader": "0",
                    "consolidateCitations": "0",
                    "consolidateFunders": "0",
                },
            )

        response.raise_for_status()

    data = response.content

    if not data.strip():
        raise RuntimeError(
            "GROBID boş yanıt döndürdü."
        )

    output_path.write_bytes(
        data
    )

    checksum = sha256_file(
        output_path
    )

    artifact_id = db.save_artifact(
        document_id=document["id"],
        artifact_type="grobid_tei",
        local_path=str(output_path),
        sha256=checksum,
        processor="GROBID",
        processor_version="0.9.1-crf",
    )

    return {
        "status": "processed",
        "artifact_id": artifact_id,
        "path": str(output_path),
        "sha256": checksum,
        "bytes": len(data),
    }


def process_docling(
    document: dict,
    work_id: int,
    force: bool = False,
):
    pdf_path = Path(
        document["local_path"]
    )

    stem = output_stem(
        work_id,
        pdf_path,
    )

    DOCLING_DIR.mkdir(
        parents=True,
        exist_ok=True,
    )

    markdown_path = (
        DOCLING_DIR
        / f"{stem}.md"
    )

    json_path = (
        DOCLING_DIR
        / f"{stem}.json"
    )

    if not DOCLING_PYTHON.exists():
        raise RuntimeError(
            f"Docling Python bulunamadı: "
            f"{DOCLING_PYTHON}"
        )

    if not DOCLING_SCRIPT.exists():
        raise RuntimeError(
            f"Docling processor bulunamadı: "
            f"{DOCLING_SCRIPT}"
        )

    need_processing = (
        force
        or not markdown_path.exists()
        or not json_path.exists()
    )

    if need_processing:
        env = os.environ.copy()

        env.setdefault(
            "TESSDATA_PREFIX",
            "/usr/share/tesseract/tessdata/",
        )

        command = [
            str(DOCLING_PYTHON),
            str(DOCLING_SCRIPT),
            "--input",
            str(pdf_path),
            "--markdown",
            str(markdown_path),
            "--json",
            str(json_path),
        ]

        subprocess.run(
            command,
            check=True,
            env=env,
        )

    markdown_checksum = (
        sha256_file(
            markdown_path
        )
    )

    json_checksum = (
        sha256_file(
            json_path
        )
    )

    markdown_artifact_id = (
        db.save_artifact(
            document_id=document["id"],
            artifact_type="docling_markdown",
            local_path=str(markdown_path),
            sha256=markdown_checksum,
            processor="Docling",
        )
    )

    json_artifact_id = (
        db.save_artifact(
            document_id=document["id"],
            artifact_type="docling_json",
            local_path=str(json_path),
            sha256=json_checksum,
            processor="Docling",
        )
    )

    return {
        "status": (
            "processed"
            if need_processing
            else "existing"
        ),
        "markdown": {
            "artifact_id":
                markdown_artifact_id,
            "path":
                str(markdown_path),
            "sha256":
                markdown_checksum,
        },
        "json": {
            "artifact_id":
                json_artifact_id,
            "path":
                str(json_path),
            "sha256":
                json_checksum,
        },
    }


async def process_work(
    work: dict,
    force: bool = False,
):
    document = db.get_latest_pdf_document(
        work["id"]
    )

    if not document:
        raise RuntimeError(
            "Bu çalışma için yerel PDF bulunamadı. "
            f"Önce şu komutu çalıştır:\n"
            f"research-harvest fulltext {work['id']}"
        )

    pdf_path = Path(
        document["local_path"]
    )

    if not pdf_path.exists():
        raise RuntimeError(
            f"DB'de PDF kaydı var fakat dosya "
            f"bulunamadı: {pdf_path}"
        )

    grobid_result = (
        await process_grobid(
            document,
            work["id"],
            force=force,
        )
    )

    # Docling CPU üzerinde çalışacağı ve subprocess
    # blocking olduğu için thread'e gönderiyoruz.
    docling_result = await asyncio.to_thread(
        process_docling,
        document,
        work["id"],
        force,
    )

    return {
        "work_id": work["id"],
        "document_id": document["id"],
        "pdf": str(pdf_path),
        "grobid": grobid_result,
        "docling": docling_result,
    }
