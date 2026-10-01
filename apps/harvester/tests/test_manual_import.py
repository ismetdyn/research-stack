from __future__ import annotations

import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from research_harvester.manual_import import import_pdf


class ImportPdfTests(unittest.TestCase):
    def test_copies_pdf_and_records_unknown_license(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            source_path = temporary_path / "article.pdf"
            pdf_bytes = b"%PDF-1.7\nexample"
            source_path.write_bytes(pdf_bytes)

            paper_directory = temporary_path / "papers"

            with patch(
                "research_harvester.manual_import.PAPER_DIR",
                paper_directory,
            ), patch(
                "research_harvester.manual_import.db.save_document",
                return_value=42,
            ) as save_document:
                result = import_pdf(
                    {"id": 7, "title": "A Test Paper"},
                    source_path,
                )

            destination = paper_directory / "7_a-test-paper.pdf"
            self.assertEqual(destination.read_bytes(), pdf_bytes)
            self.assertEqual(result["document_id"], 42)
            self.assertEqual(
                result["sha256"],
                hashlib.sha256(pdf_bytes).hexdigest(),
            )
            save_document.assert_called_once_with(
                work_id=7,
                source_name="Manual import",
                local_path=str(destination),
                document_url=source_path.resolve().as_uri(),
                media_type="application/pdf",
                sha256=hashlib.sha256(pdf_bytes).hexdigest(),
                legally_open=None,
            )

    def test_rejects_non_pdf_input(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            source_path = Path(temporary_directory) / "not-a-pdf.txt"
            source_path.write_text("not a PDF")

            with self.assertRaisesRegex(RuntimeError, "PDF"):
                import_pdf(
                    {"id": 7, "title": "A Test Paper"},
                    source_path,
                )
