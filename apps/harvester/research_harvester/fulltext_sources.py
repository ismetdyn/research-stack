from __future__ import annotations

import httpx

from research_harvester import config


OPENALEX_URL = "https://api.openalex.org/works"
CORE_URL = "https://api.core.ac.uk/v3/search/works/"


def normalize_doi(value: str | None) -> str | None:
    if not value:
        return None

    value = value.strip().lower()

    for prefix in (
        "https://doi.org/",
        "http://doi.org/",
        "doi:",
    ):
        if value.startswith(prefix):
            value = value[len(prefix):]

    return value


async def openalex_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    params = {
        "filter": f"doi:{doi}",
        "per-page": 1,
    }

    if config.OPENALEX_API_KEY:
        params["api_key"] = (
            config.OPENALEX_API_KEY
        )

    response = await client.get(
        OPENALEX_URL,
        params=params,
    )

    response.raise_for_status()

    results = (
        response.json()
        .get("results", [])
    )

    if not results:
        return []

    work = results[0]

    candidates = []

    locations = (
        work.get("locations")
        or []
    )

    for location in locations:
        if not location.get("is_oa"):
            continue

        pdf_url = location.get("pdf_url")

        landing = (
            location
            .get("landing_page_url")
        )

        if not pdf_url and not landing:
            continue

        source = (
            location.get("source")
            or {}
        )

        candidates.append(
            {
                "source": "OpenAlex",
                "pdf_url": pdf_url,
                "url": landing,
                "license":
                    location.get("license"),
                "version":
                    location.get("version"),
                "host_type":
                    source.get("type"),
                "repository":
                    source.get("display_name"),
            }
        )

    return candidates


async def core_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    headers = {}

    if config.CORE_API_KEY:
        headers["Authorization"] = (
            f"Bearer {config.CORE_API_KEY}"
        )

    params = {
        "q": f'doi:"{doi}"',
        "limit": 5,
    }

    response = await client.get(
        CORE_URL,
        params=params,
        headers=headers,
    )

    # CORE kullanılamıyorsa bütün pipeline
    # çökmesin.
    if response.status_code in {
        401,
        403,
        429,
    }:
        return []

    response.raise_for_status()

    payload = response.json()

    candidates = []

    for item in payload.get(
        "results",
        [],
    ):
        item_doi = normalize_doi(
            item.get("doi")
        )

        if (
            item_doi
            and item_doi != doi.lower()
        ):
            continue

        download_url = (
            item.get("downloadUrl")
        )

        if not download_url:
            continue

        candidates.append(
            {
                "source": "CORE",
                "pdf_url": download_url,
                "url": download_url,
                "license":
                    item.get("license"),
                "version": None,
                "host_type": "repository",
                "repository": "CORE",
            }
        )

    return candidates


async def doi_candidate(
    doi: str,
) -> list[dict]:

    return [
        {
            "source": "DOI",
            "pdf_url": None,
            "url":
                f"https://doi.org/{doi}",
            "license": None,
            "version": None,
            "host_type": "publisher",
            "repository": None,
        }
    ]


OPENAIRE_URL = (
    "https://api.openaire.eu/"
    "graph/v3/research-products"
)

EUROPE_PMC_URL = (
    "https://www.ebi.ac.uk/"
    "europepmc/webservices/rest/search"
)

ZENODO_URL = (
    "https://zenodo.org/api/records"
)


def _as_list(value):
    if value is None:
        return []

    if isinstance(value, list):
        return value

    return [value]


async def openaire_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    response = await client.get(
        OPENAIRE_URL,
        params={
            "pid": doi,
            "pageSize": 5,
        },
    )

    response.raise_for_status()

    results = (
        response.json()
        .get("results", [])
    )

    candidates = []

    for item in results:

        instances = (
            item.get("instances")
            or item.get("instance")
            or []
        )

        for instance in instances:

            access = (
                instance.get("accessRight")
                or instance.get("accessright")
                or {}
            )

            label = str(
                access.get("label")
                or ""
            ).lower()

            # Closed / restricted bir manifestation'ı
            # açık PDF adayı olarak değerlendirmeyelim.
            if (
                label
                and "open" not in label
                and "embargo" not in label
            ):
                continue

            urls = (
                instance.get("urls")
                or instance.get("url")
                or []
            )

            urls = _as_list(urls)

            license_value = (
                instance.get("license")
            )

            version = (
                instance.get("version")
                or instance.get("type")
            )

            for url in urls:
                if not isinstance(url, str):
                    continue

                if not url.startswith(
                    ("http://", "https://")
                ):
                    continue

                candidates.append(
                    {
                        "source": "OpenAIRE",
                        "pdf_url": (
                            url
                            if ".pdf" in url.lower()
                            else None
                        ),
                        "url": url,
                        "license":
                            license_value,
                        "version":
                            version,
                        "host_type":
                            "repository",
                        "repository":
                            "OpenAIRE",
                    }
                )

    return candidates


async def europe_pmc_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    response = await client.get(
        EUROPE_PMC_URL,
        params={
            "query": f'DOI:"{doi}"',
            "format": "json",
            "resultType": "core",
            "pageSize": 5,
        },
    )

    response.raise_for_status()

    results = (
        response.json()
        .get("resultList", {})
        .get("result", [])
    )

    candidates = []

    for item in results:

        item_doi = normalize_doi(
            item.get("doi")
        )

        if (
            item_doi
            and item_doi != doi.lower()
        ):
            continue

        is_oa = (
            str(
                item.get(
                    "isOpenAccess",
                    "",
                )
            ).upper()
            == "Y"
        )

        #
        # fullTextUrlList bazen publisher,
        # repository, XML veya PDF adresleri
        # döndürüyor.
        #
        fulltext = (
            item.get("fullTextUrlList")
            or {}
        )

        urls = (
            fulltext.get("fullTextUrl")
            or []
        )

        for entry in urls:

            if isinstance(entry, str):
                url = entry
                style = None

            elif isinstance(entry, dict):
                url = entry.get("url")

                style = (
                    entry.get(
                        "documentStyle"
                    )
                    or entry.get("style")
                )

            else:
                continue

            if not url:
                continue

            candidates.append(
                {
                    "source":
                        "Europe PMC",
                    "pdf_url": (
                        url
                        if (
                            str(style).lower()
                            == "pdf"
                            or ".pdf"
                            in url.lower()
                        )
                        else None
                    ),
                    "url": url,
                    "license":
                        item.get("license"),
                    "version": None,
                    "host_type":
                        "repository",
                    "repository":
                        "Europe PMC",
                }
            )

        #
        # Europe PMC'de bir PMCID varsa,
        # landing page'i de keşif kaynağı
        # olarak kullanabiliriz.
        #
        pmcid = item.get("pmcid")

        if pmcid and is_oa:
            candidates.append(
                {
                    "source":
                        "Europe PMC",
                    "pdf_url": None,
                    "url":
                        f"https://europepmc.org/"
                        f"articles/{pmcid}",
                    "license":
                        item.get("license"),
                    "version": None,
                    "host_type":
                        "repository",
                    "repository":
                        "Europe PMC",
                }
            )

    return candidates


def _zenodo_file_candidates(
    record: dict,
):
    candidates = []

    files = record.get("files")

    #
    # Güncel InvenioRDM biçimi:
    #
    # "files": {
    #   "entries": {
    #      "foo.pdf": {...}
    #   }
    # }
    #
    if isinstance(files, dict):

        entries = (
            files.get("entries")
            or {}
        )

        for filename, info in (
            entries.items()
        ):

            if not isinstance(
                info,
                dict,
            ):
                continue

            links = (
                info.get("links")
                or {}
            )

            url = (
                links.get("content")
                or links.get("download")
                or links.get("self")
            )

            media_type = (
                info.get("mimetype")
                or info.get("media_type")
            )

            if (
                not url
                or (
                    not filename
                    .lower()
                    .endswith(".pdf")
                    and media_type
                    != "application/pdf"
                )
            ):
                continue

            candidates.append(url)

    #
    # Eski Zenodo API biçimi.
    #
    elif isinstance(files, list):

        for info in files:

            if not isinstance(
                info,
                dict,
            ):
                continue

            filename = (
                info.get("key")
                or info.get("filename")
                or ""
            )

            links = (
                info.get("links")
                or {}
            )

            url = (
                links.get("download")
                or links.get("content")
                or links.get("self")
            )

            if (
                url
                and filename
                .lower()
                .endswith(".pdf")
            ):
                candidates.append(url)

    return candidates


async def zenodo_candidates(
    client: httpx.AsyncClient,
    doi: str,
) -> list[dict]:

    response = await client.get(
        ZENODO_URL,
        params={
            "q": f'"{doi}"',
            "size": 10,
        },
    )

    response.raise_for_status()

    payload = response.json()

    hits = payload.get("hits", {})

    if isinstance(hits, dict):
        records = hits.get(
            "hits",
            [],
        )
    else:
        records = hits or []

    candidates = []

    for record in records:

        metadata = (
            record.get("metadata")
            or {}
        )

        access = (
            metadata.get("access_right")
            or record.get("access", {})
            .get("record")
        )

        # Explicit restricted kayıtları atla.
        if str(access).lower() in {
            "restricted",
            "closed",
        }:
            continue

        file_urls = (
            _zenodo_file_candidates(
                record
            )
        )

        for url in file_urls:
            candidates.append(
                {
                    "source": "Zenodo",
                    "pdf_url": url,
                    "url": url,
                    "license": (
                        metadata.get("license")
                        if isinstance(
                            metadata.get(
                                "license"
                            ),
                            str,
                        )
                        else None
                    ),
                    "version":
                        metadata.get(
                            "version"
                        ),
                    "host_type":
                        "repository",
                    "repository":
                        "Zenodo",
                }
            )

    return candidates
