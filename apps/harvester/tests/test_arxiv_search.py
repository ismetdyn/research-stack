import unittest

from research_harvester.sources import arxiv


class FakeResponse:
    def __init__(self, text: str):
        self.text = text

    def raise_for_status(self):
        return None


class FakeClient:
    def __init__(self, response: FakeResponse):
        self.response = response
        self.params = None

    async def get(self, url, params):
        self.params = (url, params)
        return self.response


class ArxivSearchTests(unittest.IsolatedAsyncioTestCase):
    async def test_search_maps_atom_entries_to_work_candidates(self):
        client = FakeClient(
            FakeResponse(
                """<?xml version='1.0'?>
                <feed xmlns='http://www.w3.org/2005/Atom'
                      xmlns:arxiv='http://arxiv.org/schemas/atom'>
                  <entry>
                    <id>http://arxiv.org/abs/2401.12345v2</id>
                    <title> A reproducible\n map </title>
                    <published>2024-01-12T00:00:00Z</published>
                    <author><name>Ada Lovelace</name></author>
                    <author><name>Grace Hopper</name></author>
                    <arxiv:doi>10.1000/example</arxiv:doi>
                  </entry>
                </feed>"""
            )
        )

        works = await arxiv.search(client, "rendering", limit=5)

        self.assertEqual(
            client.params,
            (
                arxiv.BASE_URL,
                {"search_query": 'all:"rendering"', "start": 0, "max_results": 5},
            ),
        )
        self.assertEqual(len(works), 1)
        self.assertEqual(works[0].source, "arXiv")
        self.assertEqual(works[0].source_id, "2401.12345v2")
        self.assertEqual(works[0].title, "A reproducible map")
        self.assertEqual(works[0].authors, ["Ada Lovelace", "Grace Hopper"])
        self.assertEqual(works[0].year, 2024)
        self.assertEqual(works[0].doi, "10.1000/example")

    async def test_search_treats_malformed_atom_as_an_empty_result(self):
        client = FakeClient(FakeResponse("not Atom"))

        works = await arxiv.search(client, "rendering", limit=1)

        self.assertEqual(works, [])


if __name__ == "__main__":
    unittest.main()
