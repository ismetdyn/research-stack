from __future__ import annotations

import argparse
import json
from pathlib import Path

from docling.document_converter import DocumentConverter


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--input",
        required=True,
    )

    parser.add_argument(
        "--markdown",
        required=True,
    )

    parser.add_argument(
        "--json",
        required=True,
    )

    args = parser.parse_args()

    input_path = Path(args.input).expanduser().resolve()
    markdown_path = Path(args.markdown).expanduser().resolve()
    json_path = Path(args.json).expanduser().resolve()

    if not input_path.exists():
        raise FileNotFoundError(
            f"PDF bulunamadı: {input_path}"
        )

    markdown_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    json_path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    print(
        f"[Docling] Processing: {input_path}",
        flush=True,
    )

    converter = DocumentConverter()

    result = converter.convert(
        input_path
    )

    doc = result.document

    markdown = doc.export_to_markdown()

    markdown_path.write_text(
        markdown,
        encoding="utf-8",
    )

    structured = doc.export_to_dict()

    json_path.write_text(
        json.dumps(
            structured,
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    print(
        f"[Docling] Markdown: {markdown_path}",
        flush=True,
    )

    print(
        f"[Docling] JSON: {json_path}",
        flush=True,
    )


if __name__ == "__main__":
    main()
