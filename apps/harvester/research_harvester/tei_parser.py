from __future__ import annotations

import re
from pathlib import Path

from lxml import etree


NS = {
    "tei":
        "http://www.tei-c.org/ns/1.0"
}


def clean_text(
    value: str | None,
) -> str | None:
    if not value:
        return None

    value = re.sub(
        r"\s+",
        " ",
        value,
    ).strip()

    return value or None


def element_text(element) -> str | None:
    if element is None:
        return None

    text = " ".join(
        element.itertext()
    )

    return clean_text(text)


def parse_year(value: str | None):
    if not value:
        return None

    match = re.search(
        r"\b(18|19|20|21)\d{2}\b",
        value,
    )

    if not match:
        return None

    return int(
        match.group(0)
    )


def parse_person_name(
    pers_name,
) -> str | None:
    forenames = [
        clean_text(x.text)
        for x in pers_name.xpath(
            "./tei:forename",
            namespaces=NS,
        )
    ]

    surname_node = pers_name.find(
        "tei:surname",
        namespaces=NS,
    )

    surname = (
        clean_text(surname_node.text)
        if surname_node is not None
        else None
    )

    pieces = [
        x
        for x in (
            forenames
            + [surname]
        )
        if x
    ]

    return (
        " ".join(pieces)
        if pieces
        else element_text(pers_name)
    )


def parse_sections(root):
    sections = []

    body = root.find(
        ".//tei:text/tei:body",
        namespaces=NS,
    )

    if body is None:
        return sections

    index = 0

    for div in body.xpath(
        ".//tei:div",
        namespaces=NS,
    ):
        heading = element_text(
            div.find(
                "tei:head",
                namespaces=NS,
            )
        )

        chunks = []

        # Yalnız doğrudan çocukları alıyoruz;
        # nested div içeriğini üst bölümde tekrar etmiyoruz.
        for child in div:
            local = etree.QName(
                child
            ).localname

            if local in {
                "p",
                "list",
                "formula",
            }:
                value = element_text(
                    child
                )

                if value:
                    chunks.append(
                        value
                    )

        text = "\n\n".join(
            chunks
        ).strip()

        if not text and not heading:
            continue

        sections.append(
            {
                "section_index": index,
                "section_type":
                    div.get("type"),
                "heading": heading,
                "text_content": text,
            }
        )

        index += 1

    return sections


def parse_references(root):
    references = []

    nodes = root.xpath(
        ".//tei:listBibl/tei:biblStruct",
        namespaces=NS,
    )

    for index, node in enumerate(nodes):

        doi_nodes = node.xpath(
            ".//tei:idno["
            "translate(@type,"
            "'abcdefghijklmnopqrstuvwxyz',"
            "'ABCDEFGHIJKLMNOPQRSTUVWXYZ')"
            "='DOI'"
            "]",
            namespaces=NS,
        )

        doi = None

        if doi_nodes:
            doi = clean_text(
                doi_nodes[0].text
            )

            if doi:
                doi = (
                    doi
                    .lower()
                    .replace(
                        "https://doi.org/",
                        "",
                    )
                    .replace(
                        "http://doi.org/",
                        "",
                    )
                    .strip()
                )

        title_nodes = node.xpath(
            ".//tei:analytic/"
            "tei:title[@level='a']",
            namespaces=NS,
        )

        if not title_nodes:
            title_nodes = node.xpath(
                ".//tei:monogr/tei:title",
                namespaces=NS,
            )

        title = (
            element_text(
                title_nodes[0]
            )
            if title_nodes
            else None
        )

        date_nodes = node.xpath(
            ".//tei:date",
            namespaces=NS,
        )

        year = None

        for date in date_nodes:
            year = parse_year(
                date.get("when")
                or element_text(date)
            )

            if year:
                break

        authors = []

        person_nodes = node.xpath(
            ".//tei:analytic/"
            "tei:author/tei:persName",
            namespaces=NS,
        )

        if not person_nodes:
            person_nodes = node.xpath(
                ".//tei:monogr/"
                "tei:author/tei:persName",
                namespaces=NS,
            )

        for person in person_nodes:
            name = parse_person_name(
                person
            )

            if name:
                authors.append(
                    name
                )

        raw_citation = element_text(
            node
        )

        raw_tei = etree.tostring(
            node,
            encoding="unicode",
            pretty_print=False,
        )

        references.append(
            {
                "ref_index": index,
                "doi": doi,
                "title": title,
                "publication_year":
                    year,
                "authors": authors,
                "raw_citation":
                    raw_citation,
                "raw_tei": raw_tei,
            }
        )

    return references


def parse_tei(
    path: Path,
):
    parser = etree.XMLParser(
        recover=True,
        huge_tree=True,
    )

    tree = etree.parse(
        str(path),
        parser,
    )

    root = tree.getroot()

    return {
        "sections":
            parse_sections(root),
        "references":
            parse_references(root),
    }
