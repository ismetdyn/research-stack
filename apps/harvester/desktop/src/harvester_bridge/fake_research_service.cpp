#include <harvester/app/research_service.hpp>

#include <algorithm>
#include <utility>

namespace harvester::app {

namespace {

bool has_source(
    const std::vector<ResearchSource> &sources,
    const ResearchSource expected
) {
    return std::find(sources.begin(), sources.end(), expected) != sources.end();
}

std::string first_source_name(const std::vector<ResearchSource> &sources) {
    if (has_source(sources, ResearchSource::OpenAlex)) {
        return "OpenAlex";
    }
    if (has_source(sources, ResearchSource::Crossref)) {
        return "Crossref";
    }
    return "arXiv";
}

}  // namespace

SearchTicket FakeResearchService::begin_search(const SearchRequest &request) {
    const SearchTicket ticket = next_ticket_++;
    searches_.push_back({ticket, PendingSearch{request, false}});
    return ticket;
}

SearchSnapshot FakeResearchService::poll_search(const SearchTicket ticket) {
    const auto found = std::find_if(
        searches_.begin(),
        searches_.end(),
        [ticket](const auto &entry) { return entry.first == ticket; }
    );
    if (found == searches_.end()) {
        return {
            SearchStatus::Failed,
            {},
            "Unknown desktop search request.",
            {},
        };
    }

    PendingSearch &pending = found->second;
    if (!pending.observed_pending) {
        pending.observed_pending = true;
        return {};
    }
    if (pending.request.query.empty() || pending.request.query.size() > 240U ||
        pending.request.result_limit == 0U ||
        pending.request.result_limit > 100U || pending.request.sources.empty()) {
        return {
            SearchStatus::Failed,
            {},
            "Enter a query, choose one or more sources, and use a limit from 1 to 100.",
            {},
        };
    }

    const std::string source = first_source_name(pending.request.sources);
    const std::string query = pending.request.query;
    SearchSnapshot completed{
        SearchStatus::Ready,
        {
            {
                "work:2026:001",
                "A reproducible map of " + query,
                "A. Researcher, B. Analyst",
                source,
                2026U,
                42U,
                {},
                {},
            },
            {
                "work:2025:014",
                "Methods and evidence for " + query,
                "C. Scholar, D. Curator",
                source,
                2025U,
                18U,
                {},
                {},
            },
            {
                "work:2024:031",
                "Open data practices in " + query,
                "E. Investigator",
                source,
                2024U,
                9U,
                {},
                {},
            },
        },
        {},
        {},
    };
    searches_.erase(found);
    return completed;
}

}  // namespace harvester::app
