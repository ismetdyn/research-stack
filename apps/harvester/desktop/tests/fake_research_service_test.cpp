#include <cassert>

#include <harvester/app/research_service.hpp>

int main() {
    harvester::app::FakeResearchService service{};
    harvester::app::SearchRequest request{};
    request.query = "open science";
    request.result_limit = 10U;
    request.sources = {
        harvester::app::ResearchSource::OpenAlex,
        harvester::app::ResearchSource::Arxiv,
    };

    const harvester::app::SearchTicket ticket = service.begin_search(request);
    assert(service.poll_search(ticket).status == harvester::app::SearchStatus::Pending);
    const harvester::app::SearchSnapshot ready = service.poll_search(ticket);
    assert(ready.status == harvester::app::SearchStatus::Ready);
    assert(ready.works.size() == 3U);
    assert(ready.works.front().source == "OpenAlex");

    harvester::app::SearchRequest invalid{};
    invalid.query = "";
    invalid.sources = {harvester::app::ResearchSource::Crossref};
    const harvester::app::SearchTicket invalid_ticket = service.begin_search(invalid);
    (void)service.poll_search(invalid_ticket);
    const harvester::app::SearchSnapshot failed = service.poll_search(invalid_ticket);
    assert(failed.status == harvester::app::SearchStatus::Failed);
    assert(!failed.error.empty());
    return 0;
}
