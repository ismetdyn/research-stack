from __future__ import annotations

import unittest

from research_harvester.sources import arxiv, biorxiv, datacite, osf, zenodo


class FakeResponse:
    def __init__(
        self,
        payload: dict,
        status_code: int = 200,
        text: str = "",
    ):
        self.payload = payload
        self.status_code = status_code
        self.text = text

    def json(self):
        return self.payload

    def raise_for_status(self):
        if self.status_code >= 400:
            raise RuntimeError(f"HTTP {self.status_code}")


class FakeClient:
    def __init__(self, responses: list[FakeResponse]):
        self.responses = iter(responses)
        self.requests: list[dict] = []

    async def get(self, url: str, **kwargs):
        self.requests.append({"url": url, **kwargs})
        return next(self.responses)


class RelatedOutputSourceTests(unittest.IsolatedAsyncioTestCase):
    async def test_biorxiv_links_a_published_doi_to_its_preprint(self):
        client = FakeClient(
            [
                FakeResponse({"collection": []}),
                FakeResponse(
                    {
                        "collection": [
                            {
                                "biorxiv_doi": "10.1101/2024.01.01.123456",
                                "published_doi": "10.1000/published",
                                "preprint_title": "A preprint",
                                "preprint_platform": "bioRxiv",
                            }
                        ]
                    }
                ),
                FakeResponse({"collection": []}),
                FakeResponse({"collection": []}),
            ]
        )

        relations = await biorxiv.resolve(client, "10.1000/published")

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "incoming")
        self.assertEqual(relations[0]["relation_type"], "is_preprint_of")
        self.assertEqual(
            relations[0]["related_identifier"],
            "10.1101/2024.01.01.123456",
        )

    async def test_datacite_preserves_declared_related_identifier(self):
        client = FakeClient(
            [
                FakeResponse(
                    {
                        "data": {
                            "attributes": {
                                "relatedIdentifiers": [
                                    {
                                        "relatedIdentifier": "10.1000/article",
                                        "relatedIdentifierType": "DOI",
                                        "relationType": "IsSupplementTo",
                                        "resourceTypeGeneral": "JournalArticle",
                                    }
                                ]
                            }
                        }
                    }
                )
            ]
        )

        relations = await datacite.resolve(client, "10.5281/zenodo.123")

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "outgoing")
        self.assertEqual(relations[0]["relation_type"], "is_supplement_to")
        self.assertEqual(relations[0]["related_resource_type"], "JournalArticle")

    async def test_datacite_finds_a_dataset_that_references_the_work(self):
        client = FakeClient(
            [
                FakeResponse({}, status_code=404),
                FakeResponse(
                    {
                        "data": [
                            {
                                "id": "10.5281/zenodo.123",
                                "attributes": {
                                    "titles": [{"title": "Research data"}],
                                    "types": {
                                        "resourceTypeGeneral": "Dataset"
                                    },
                                    "relatedIdentifiers": [
                                        {
                                            "relatedIdentifier": "10.1000/article",
                                            "relatedIdentifierType": "DOI",
                                            "relationType": "IsSupplementTo",
                                        }
                                    ],
                                },
                            }
                        ]
                    }
                ),
            ]
        )

        relations = await datacite.resolve(client, "10.1000/article")

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "incoming")
        self.assertEqual(relations[0]["related_identifier"], "10.5281/zenodo.123")
        self.assertEqual(relations[0]["related_resource_type"], "Dataset")

    async def test_zenodo_links_related_software_back_to_the_work(self):
        client = FakeClient(
            [
                FakeResponse(
                    {
                        "hits": {
                            "hits": [
                                {
                                    "id": 17,
                                    "metadata": {
                                        "doi": "10.5281/zenodo.17",
                                        "title": "Research software",
                                        "upload_type": "software",
                                        "related_identifiers": [
                                            {
                                                "identifier": "10.1000/article",
                                                "relation": "isSupplementTo",
                                            }
                                        ],
                                    },
                                    "links": {
                                        "html": "https://zenodo.org/records/17"
                                    },
                                }
                            ]
                        }
                    }
                )
            ]
        )

        relations = await zenodo.resolve(client, "10.1000/article")

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "incoming")
        self.assertEqual(relations[0]["relation_type"], "is_supplement_to")
        self.assertEqual(relations[0]["related_resource_type"], "software")
        self.assertEqual(relations[0]["related_identifier"], "10.5281/zenodo.17")

    async def test_zenodo_retries_a_server_error_with_a_broad_doi_query(self):
        client = FakeClient(
            [
                FakeResponse({}, status_code=504),
                FakeResponse(
                    {
                        "hits": {
                            "hits": [
                                {
                                    "id": 17,
                                    "metadata": {
                                        "doi": "10.5281/zenodo.17",
                                        "related_identifiers": [
                                            {
                                                "identifier": "10.1000/article",
                                                "relation": "isSupplementTo",
                                            }
                                        ],
                                    },
                                }
                            ]
                        }
                    }
                ),
            ]
        )

        relations = await zenodo.resolve(client, "10.1000/article")

        self.assertEqual(len(relations), 1)
        self.assertEqual(client.requests[1]["params"]["q"], '"10.1000/article"')

    async def test_zenodo_ignores_malformed_records(self):
        client = FakeClient(
            [FakeResponse({"hits": {"hits": ["not a record"]}})]
        )

        relations = await zenodo.resolve(client, "10.1000/article")

        self.assertEqual(relations, [])

    async def test_arxiv_links_a_doi_to_its_preprint(self):
        client = FakeClient(
            [
                FakeResponse(
                    {},
                    text="""<?xml version='1.0'?>
                    <feed xmlns='http://www.w3.org/2005/Atom'
                          xmlns:arxiv='http://arxiv.org/schemas/atom'>
                      <entry>
                        <id>http://arxiv.org/abs/2401.12345v2</id>
                        <title> A matching preprint </title>
                        <arxiv:doi>10.1000/article</arxiv:doi>
                      </entry>
                    </feed>""",
                )
            ]
        )

        relations = await arxiv.resolve(client, "10.1000/article")

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "incoming")
        self.assertEqual(relations[0]["related_identifier"], "arxiv:2401.12345v2")
        self.assertEqual(relations[0]["related_resource_type"], "preprint")

    async def test_osf_links_a_preprint_to_its_public_project(self):
        client = FakeClient(
            [
                FakeResponse(
                    {
                        "data": {
                            "relationships": {
                                "referent": {
                                    "links": {
                                        "related": {
                                            "meta": {"type": "preprints"}
                                        }
                                    }
                                }
                            }
                        }
                    }
                ),
                FakeResponse(
                    {
                        "data": {
                            "id": "abc12",
                            "attributes": {
                                "doi": "10.17605/osf.io/abc12"
                            },
                        }
                    }
                ),
                FakeResponse({"data": {"id": "node1"}}),
                FakeResponse(
                    {
                        "data": {
                            "attributes": {"title": "OSF project"},
                            "links": {"html": "https://osf.io/node1/"},
                        }
                    }
                ),
            ]
        )

        relations = await osf.resolve(
            client,
            "10.17605/osf.io/abc12",
        )

        self.assertEqual(len(relations), 1)
        self.assertEqual(relations[0]["direction"], "outgoing")
        self.assertEqual(relations[0]["relation_type"], "is_part_of")
        self.assertEqual(relations[0]["related_identifier"], "osf:node1")
        self.assertEqual(relations[0]["related_title"], "OSF project")
