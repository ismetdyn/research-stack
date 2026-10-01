from __future__ import annotations

import unittest
from unittest.mock import AsyncMock, patch

from research_harvester.related_graph import resolve_related_outputs


class RelatedGraphTests(unittest.IsolatedAsyncioTestCase):
    async def test_groups_successful_source_results_for_replacement(self):
        preprint_relation = {
            "source": "bioRxiv/medRxiv",
            "direction": "incoming",
            "relation_type": "is_preprint_of",
            "related_identifier": "10.1101/example",
            "related_identifier_type": "DOI",
            "related_title": "Example preprint",
            "related_resource_type": "preprint",
            "related_url": "https://www.biorxiv.org/content/10.1101/example",
            "raw": {},
        }

        with patch(
            "research_harvester.related_graph.biorxiv.resolve",
            new_callable=AsyncMock,
            return_value=[preprint_relation],
        ), patch(
            "research_harvester.related_graph.arxiv.resolve",
            new_callable=AsyncMock,
            return_value=[],
        ), patch(
            "research_harvester.related_graph.osf.resolve",
            new_callable=AsyncMock,
            return_value=[],
        ), patch(
            "research_harvester.related_graph.datacite.resolve",
            new_callable=AsyncMock,
            return_value=[],
        ), patch(
            "research_harvester.related_graph.zenodo.resolve",
            new_callable=AsyncMock,
            return_value=[],
        ), patch(
            "research_harvester.related_graph.db.replace_work_relationships"
        ) as replace_work_relationships:
            result = await resolve_related_outputs(
                {"id": 12, "doi": "10.1000/article"}
            )

        self.assertEqual(result["relationships"], 1)
        self.assertEqual(result["source_errors"], [])
        replace_work_relationships.assert_called_once_with(
            12,
            {"bioRxiv/medRxiv": [preprint_relation], "arXiv": [], "OSF": [], "DataCite": [], "Zenodo": []},
        )
