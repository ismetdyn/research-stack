from __future__ import annotations

import asyncio
import json
from pathlib import Path

import typer

from rich.console import Console
from rich.table import Table

from research_harvester import db
from research_harvester.dedup import deduplicate
from research_harvester.enrichment import enrich_work
from research_harvester.search import search_all
from research_harvester.storage import save_raw
from research_harvester.fulltext import download_fulltext
from research_harvester.manual_import import import_pdf
from research_harvester.processor import process_work
from research_harvester.citation_graph import fetch_citation_graph
from research_harvester.extractor import extract_work_structure
from research_harvester.related_graph import resolve_related_outputs


app = typer.Typer(
    no_args_is_help=True,
    help="Open-science multi-source research harvester.",
)

console = Console()


@app.command("search")
def search_command(
    query: str,
    limit: int = typer.Option(
        10,
        "--limit",
        "-n",
    ),
    json_output: bool = typer.Option(
        False,
        "--json",
    ),
):
    """
    Birden fazla akademik kaynağı paralel ara.
    """

    candidates = asyncio.run(
        search_all(
            query=query,
            limit=limit,
        )
    )

    raw_path = save_raw(
        query,
        candidates,
    )

    merged = deduplicate(
        candidates
    )

    ids = db.save_works(
        merged
    )

    if json_output:
        output = []

        for work_id, item in zip(
            ids,
            merged,
        ):
            row = item.model_dump()
            row["local_id"] = work_id
            output.append(row)

        print(
            json.dumps(
                output,
                ensure_ascii=False,
                indent=2,
            )
        )

        return

    table = Table(
        title=f"Research results: {query}"
    )

    table.add_column(
        "ID",
        justify="right",
    )

    table.add_column(
        "Year",
        width=6,
    )

    table.add_column(
        "Title",
        max_width=65,
    )

    table.add_column(
        "DOI",
        max_width=30,
    )

    table.add_column(
        "Sources",
        max_width=30,
    )

    table.add_column(
        "Cites",
        justify="right",
    )

    for work_id, work in zip(
        ids,
        merged,
    ):
        table.add_row(
            str(work_id),
            str(work.year or ""),
            work.title,
            work.doi or "",
            ", ".join(work.sources),
            (
                str(work.citation_count)
                if work.citation_count
                is not None
                else ""
            ),
        )

    console.print(table)

    console.print(
        f"\nRaw data: {raw_path}"
    )

    console.print(
        f"Raw candidates: {len(candidates)}"
    )

    console.print(
        f"After deduplication: {len(merged)}"
    )

    console.print(
        f"Saved to PostgreSQL: {len(ids)}"
    )


@app.command("list")
def list_command(
    limit: int = typer.Option(
        20,
        "--limit",
        "-n",
    ),
):
    """
    Yerel PostgreSQL kütüphanesini listele.
    """

    rows = db.list_works(
        limit=limit
    )

    table = Table(
        title="Local research database"
    )

    table.add_column(
        "ID",
        justify="right",
    )

    table.add_column("Year")
    table.add_column(
        "Title",
        max_width=70,
    )
    table.add_column(
        "DOI",
        max_width=30,
    )
    table.add_column("OA")
    table.add_column("Integrity")

    for row in rows:
        table.add_row(
            str(row["id"]),
            str(
                row["publication_year"]
                or ""
            ),
            row["title"],
            row["doi"] or "",
            (
                "yes"
                if row["is_open_access"]
                else ""
            ),
            row["integrity_status"],
        )

    console.print(table)


@app.command("show")
def show_command(
    ref: str,
):
    """
    ID veya DOI ile yerel kaydı göster.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )

        raise typer.Exit(1)

    console.print_json(
        json.dumps(
            dict(work),
            default=str,
            ensure_ascii=False,
        )
    )


@app.command("enrich")
def enrich_command(
    ref: str,
):
    """
    DOI kaydını Crossref, OpenCitations
    ve Unpaywall ile zenginleştir.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )

        raise typer.Exit(1)

    if not work.get("doi"):
        console.print(
            "[red]Bu kaydın DOI'si yok.[/red]"
        )

        raise typer.Exit(1)

    console.print(
        f"Enriching DOI: {work['doi']}"
    )

    result = asyncio.run(
        enrich_work(work)
    )

    console.print_json(
        json.dumps(
            result,
            default=str,
            ensure_ascii=False,
        )
    )


@app.command("fulltext")
def fulltext_command(
    ref: str,
):
    """
    ID veya DOI için legal açık erişimli PDF bul ve indir.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )
        raise typer.Exit(1)

    console.print(
        f"[bold]Full text aranıyor:[/bold] "
        f"{work['title']}"
    )

    if work.get("doi"):
        console.print(
            f"DOI: {work['doi']}"
        )

    result = asyncio.run(
        download_fulltext(work)
    )

    if not result.get("success"):
        console.print(
            "[yellow]PDF indirilemedi.[/yellow]"
        )

        console.print_json(
            json.dumps(
                result,
                ensure_ascii=False,
                default=str,
            )
        )

        raise typer.Exit(1)

    console.print(
        "[green]PDF başarıyla indirildi.[/green]"
    )

    console.print(
        f"Document ID : {result['document_id']}"
    )

    console.print(
        f"Path        : {result['path']}"
    )

    console.print(
        f"Size        : "
        f"{result['bytes'] / 1024 / 1024:.2f} MiB"
    )

    console.print(
        f"SHA-256     : {result['sha256']}"
    )

    console.print(
        f"Source      : {result['source_url']}"
    )

    if result.get("license"):
        console.print(
            f"License     : {result['license']}"
        )


@app.command("import-pdf")
def import_pdf_command(
    ref: str,
    pdf: Path = typer.Argument(
        ...,
        exists=True,
        file_okay=True,
        dir_okay=False,
        readable=True,
    ),
):
    """Yasal olarak elde edilmiş yerel PDF'yi kütüphaneye al."""
    work = db.get_work(ref)

    if not work:
        console.print("[red]Kayıt bulunamadı.[/red]")
        raise typer.Exit(1)

    try:
        result = import_pdf(work, pdf)
    except (FileNotFoundError, RuntimeError) as exc:
        console.print(f"[red]PDF ithali başarısız:[/red] {exc}")
        raise typer.Exit(1)

    console.print("[green]PDF içe aktarıldı.[/green]")
    console.print_json(
        json.dumps(result, ensure_ascii=False, default=str)
    )


@app.command("process")
def process_command(
    ref: str,
    force: bool = typer.Option(
        False,
        "--force",
        help="Mevcut çıktıları yeniden üret.",
    ),
):
    """
    Yerel PDF'yi GROBID ve Docling ile işle.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )

        raise typer.Exit(1)

    console.print(
        f"[bold]Processing:[/bold] "
        f"{work['title']}"
    )

    console.print(
        f"Work ID: {work['id']}"
    )

    try:
        result = asyncio.run(
            process_work(
                work,
                force=force,
            )
        )

    except Exception as exc:
        console.print(
            f"[red]Processing başarısız:[/red] "
            f"{type(exc).__name__}: {exc}"
        )

        raise typer.Exit(1)

    console.print(
        "[green]Processing tamamlandı.[/green]"
    )

    console.print_json(
        json.dumps(
            result,
            ensure_ascii=False,
            default=str,
        )
    )


@app.command("artifacts")
def artifacts_command(
    ref: str,
):
    """
    Çalışma için üretilen TEI/Markdown/JSON dosyalarını göster.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )

        raise typer.Exit(1)

    rows = db.get_artifacts_for_work(
        work["id"]
    )

    if not rows:
        console.print(
            "[yellow]Henüz artefakt yok.[/yellow]"
        )

        return

    table = Table(
        title=f"Artifacts — Work {work['id']}"
    )

    table.add_column("ID")
    table.add_column("Type")
    table.add_column("Processor")
    table.add_column(
        "Path",
        max_width=80,
    )

    for row in rows:
        table.add_row(
            str(row["id"]),
            row["artifact_type"],
            row["processor"],
            row["local_path"],
        )

    console.print(table)


@app.command("citations")
def citations_command(
    ref: str,
):
    """
    OpenCitations üzerinden references/cited-by ağını indir.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )
        raise typer.Exit(1)

    if not work.get("doi"):
        console.print(
            "[red]Bu çalışma için DOI yok.[/red]"
        )
        raise typer.Exit(1)

    console.print(
        f"[bold]Citation graph:[/bold] "
        f"{work['title']}"
    )

    console.print(
        f"DOI: {work['doi']}"
    )

    try:
        result = asyncio.run(
            fetch_citation_graph(work)
        )

    except Exception as exc:
        console.print(
            f"[red]OpenCitations hatası:[/red] "
            f"{type(exc).__name__}: {exc}"
        )
        raise typer.Exit(1)

    console.print(
        f"References : "
        f"{result['references']}"
    )

    console.print(
        f"Cited by   : "
        f"{result['citations']}"
    )

    console.print(
        f"Reference DOI coverage : "
        f"{result['references_with_doi']}"
    )

    console.print(
        f"Citation DOI coverage  : "
        f"{result['citations_with_doi']}"
    )


@app.command("citation-links")
def citation_links_command(
    ref: str,
    direction: str = typer.Option(
        "all",
        "--direction",
        "-d",
        help="all, incoming veya outgoing",
    ),
):
    """
    Yerel citation graph ilişkilerini göster.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )
        raise typer.Exit(1)

    if direction not in {
        "all",
        "incoming",
        "outgoing",
    }:
        console.print(
            "[red]direction: all, incoming "
            "veya outgoing olmalı.[/red]"
        )
        raise typer.Exit(1)

    db_direction = (
        None
        if direction == "all"
        else direction
    )

    rows = db.get_citation_links(
        work["id"],
        db_direction,
    )

    table = Table(
        title=f"Citation links — Work {work['id']}"
    )

    table.add_column("Direction")
    table.add_column("DOI", max_width=45)
    table.add_column("Date")
    table.add_column("Author self")
    table.add_column("Journal self")

    for row in rows:
        table.add_row(
            row["direction"],
            row["related_doi"] or "",
            str(row["creation_date"] or ""),
            (
                "yes"
                if row["author_self_citation"]
                else ""
            ),
            (
                "yes"
                if row["journal_self_citation"]
                else ""
            ),
        )

    console.print(table)

    console.print(
        f"\nTotal: {len(rows)}"
    )


@app.command("related")
def related_command(
    ref: str,
):
    """Preprint, OSF, DataCite ve Zenodo ilişkilerini çöz ve göster."""
    work = db.get_work(ref)

    if not work:
        console.print("[red]Kayıt bulunamadı.[/red]")
        raise typer.Exit(1)

    if not work.get("doi"):
        console.print("[red]Bu çalışma için DOI yok.[/red]")
        raise typer.Exit(1)

    try:
        result = asyncio.run(resolve_related_outputs(work))
    except Exception as exc:
        console.print(
            f"[red]İlişki çözümleme başarısız:[/red] "
            f"{type(exc).__name__}: {exc}"
        )
        raise typer.Exit(1)

    rows = db.get_work_relationships(work["id"])
    table = Table(title=f"Related outputs — Work {work['id']}")
    table.add_column("Source")
    table.add_column("Direction")
    table.add_column("Relation")
    table.add_column("Type")
    table.add_column("Identifier", max_width=48)
    table.add_column("Title", max_width=48)

    for row in rows:
        table.add_row(
            row["source_name"],
            "→" if row["direction"] == "outgoing" else "←",
            row["relation_type"],
            row["related_resource_type"] or "",
            row["related_identifier"],
            row["related_title"] or "",
        )

    console.print(table)
    console.print(f"\nResolved this run: {result['relationships']}")

    for error in result["source_errors"]:
        console.print(f"[yellow]Warning:[/yellow] {error}")


@app.command("extract")
def extract_command(
    ref: str,
):
    """
    GROBID TEI'den bölümleri ve kaynakçayı çıkar.
    """

    work = db.get_work(ref)

    if not work:
        console.print(
            "[red]Kayıt bulunamadı.[/red]"
        )
        raise typer.Exit(1)

    try:
        result = (
            extract_work_structure(
                work
            )
        )

    except Exception as exc:
        console.print(
            f"[red]Extraction başarısız:[/red] "
            f"{type(exc).__name__}: {exc}"
        )
        raise typer.Exit(1)

    console.print(
        "[green]Structure extraction tamamlandı.[/green]"
    )

    console.print(
        f"Sections       : "
        f"{result['sections']}"
    )

    console.print(
        f"References     : "
        f"{result['references']}"
    )

    console.print(
        f"References DOI : "
        f"{result['references_with_doi']}"
    )


if __name__ == "__main__":
    app()
