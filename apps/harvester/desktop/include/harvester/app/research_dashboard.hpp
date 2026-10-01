#ifndef HARVESTER_APP_RESEARCH_DASHBOARD_HPP
#define HARVESTER_APP_RESEARCH_DASHBOARD_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <harvester/app/research_service.hpp>

namespace harvester::app {

/* Plain application data mapped by the composition root; no RHI types leak here. */
struct RendererDiagnostics {
    std::string backend_name = "Renderer";
    std::string backend_version;
    std::uint32_t maximum_texture_dimension = 0U;
    bool dynamic_textures_enabled = false;
};

struct ResearchDashboardState {
    std::array<char, 256> query{"machine learning"};
    int result_limit = 20;
    bool include_openalex = true;
    bool include_crossref = true;
    bool include_arxiv = true;
    SearchTicket active_search = 0U;
    SearchSnapshot search_snapshot{};
    std::size_t selected_work = 0U;
    FullTextDiscoveryTicket active_fulltext_discovery = 0U;
    FullTextDiscoverySnapshot fulltext_discovery{};
    FullTextCandidateId selected_fulltext_candidate = 0U;
    FullTextDownloadTicket active_fulltext_download = 0U;
    FullTextDownloadSnapshot fulltext_download{};
};

void draw_research_dashboard(
    ResearchDashboardState &state,
    ResearchService &service,
    FullTextService &fulltext_service,
    const RendererDiagnostics &renderer_diagnostics
);

}  // namespace harvester::app

#endif
