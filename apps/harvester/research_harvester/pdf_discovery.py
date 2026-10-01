from __future__ import annotations

from dataclasses import dataclass
from urllib.parse import urljoin, urlparse

import httpx
from bs4 import BeautifulSoup


@dataclass
class PdfCandidate:
    url: str
    score: int
    reason: str


def same_host(a: str, b: str) -> bool:
    return (
        urlparse(a).netloc.lower()
        == urlparse(b).netloc.lower()
    )


def score_link(
    text: str,
    href: str,
) -> tuple[int, str]:

    text_l = (text or "").strip().lower()
    href_l = href.lower()

    score = 0
    reasons: list[str] = []

    if "pdf" in text_l:
        score += 100
        reasons.append("link text contains PDF")

    if "download pdf" in text_l:
        score += 50
        reasons.append("download PDF text")

    if ".pdf" in href_l:
        score += 100
        reasons.append(".pdf URL")

    if "/download/" in href_l:
        score += 80
        reasons.append("download path")

    if "/article/download/" in href_l:
        score += 100
        reasons.append("OJS article download")

    if "/article/view/" in href_l:
        score += 25
        reasons.append("article view/galley")

    if "galley" in href_l:
        score += 40
        reasons.append("galley")

    if "download" in text_l:
        score += 30
        reasons.append("download link")

    return score, ", ".join(reasons)


def extract_candidates(
    page_url: str,
    html: str,
) -> list[PdfCandidate]:

    soup = BeautifulSoup(
        html,
        "html.parser",
    )

    candidates: dict[str, PdfCandidate] = {}

    # Citation metadata çok değerlidir.
    for meta_name in (
        "citation_pdf_url",
        "dc.identifier",
        "eprints.document_url",
    ):
        for meta in soup.find_all(
            "meta",
            attrs={"name": meta_name},
        ):
            content = meta.get("content")

            if not content:
                continue

            url = urljoin(page_url, content)

            score = 200

            candidates[url] = PdfCandidate(
                url=url,
                score=score,
                reason=f"meta:{meta_name}",
            )

    # Normal <a href=...> linkleri.
    for anchor in soup.find_all(
        "a",
        href=True,
    ):
        href = anchor.get("href")

        if not href:
            continue

        url = urljoin(
            page_url,
            href,
        )

        if not url.startswith(
            ("http://", "https://")
        ):
            continue

        text = anchor.get_text(
            " ",
            strip=True,
        )

        score, reason = score_link(
            text,
            url,
        )

        if score <= 0:
            continue

        existing = candidates.get(url)

        candidate = PdfCandidate(
            url=url,
            score=score,
            reason=reason,
        )

        if (
            existing is None
            or score > existing.score
        ):
            candidates[url] = candidate

    return sorted(
        candidates.values(),
        key=lambda x: x.score,
        reverse=True,
    )


async def fetch_kind(
    client: httpx.AsyncClient,
    url: str,
) -> tuple[
    str,
    httpx.Response,
]:

    response = await client.get(url)
    response.raise_for_status()

    content_type = (
        response.headers
        .get("content-type", "")
        .lower()
    )

    if (
        "application/pdf" in content_type
        or response.content.startswith(b"%PDF-")
    ):
        return "pdf", response

    if (
        "text/html" in content_type
        or content_type == ""
    ):
        return "html", response

    return "other", response


async def discover_pdf(
    client: httpx.AsyncClient,
    start_url: str,
    max_depth: int = 2,
) -> dict | None:
    """
    Landing page -> galley -> PDF gibi akışları keşfeder.

    Güvenlik/performans nedeniyle:
    - maksimum birkaç HTML seviyesi
    - aynı yayıncı host'una öncelik
    - ziyaret edilen URL tekrar edilmez
    """

    queue: list[tuple[str, int]] = [
        (start_url, 0)
    ]

    visited: set[str] = set()

    while queue:
        url, depth = queue.pop(0)

        if url in visited:
            continue

        visited.add(url)

        try:
            kind, response = await fetch_kind(
                client,
                url,
            )
        except Exception:
            continue

        if kind == "pdf":
            return {
                "url": str(response.url),
                "response": response,
                "depth": depth,
                "visited": list(visited),
            }

        if (
            kind != "html"
            or depth >= max_depth
        ):
            continue

        candidates = extract_candidates(
            str(response.url),
            response.text,
        )

        # Aynı host'taki linklere öncelik ver.
        candidates.sort(
            key=lambda c: (
                same_host(
                    str(response.url),
                    c.url,
                ),
                c.score,
            ),
            reverse=True,
        )

        # Her sayfada bütün web'i crawl etmeyelim.
        for candidate in candidates[:12]:
            if candidate.url not in visited:
                queue.append(
                    (
                        candidate.url,
                        depth + 1,
                    )
                )

    return None
