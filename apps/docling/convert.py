import os
from pathlib import Path
from docling.document_converter import DocumentConverter

# Ayrı venv: config import edilemez, kök burada ayrıca çözülür.
root = Path(os.getenv("RESEARCH_STACK_ROOT") or Path(__file__).resolve().parents[2])

source = root / "data/papers/test.pdf"
output_md = root / "data/docling/test.md"
output_json = root / "data/docling/test.json"

if not source.exists():
    raise FileNotFoundError(f"PDF bulunamadı: {source}")

print(f"İşleniyor: {source}")

converter = DocumentConverter()
result = converter.convert(source)
doc = result.document

output_md.parent.mkdir(parents=True, exist_ok=True)

output_md.write_text(
    doc.export_to_markdown(),
    encoding="utf-8"
)

output_json.write_text(
    doc.model_dump_json(indent=2),
    encoding="utf-8"
)

print(f"Markdown: {output_md}")
print(f"JSON    : {output_json}")
print("Docling işlemi tamamlandı.")
