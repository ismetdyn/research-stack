#include <harvester/app/research_dashboard.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

#include <imgui.h>

namespace harvester::app {

namespace {

void draw_source_toggle(
    const char *const label,
    bool *const selected,
    const char *const detail
) {
    ImGui::Checkbox(label, selected);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", detail);
}

std::vector<ResearchSource> selected_sources(
    const ResearchDashboardState &state
) {
    std::vector<ResearchSource> sources;
    if (state.include_openalex) {
        sources.push_back(ResearchSource::OpenAlex);
    }
    if (state.include_crossref) {
        sources.push_back(ResearchSource::Crossref);
    }
    if (state.include_arxiv) {
        sources.push_back(ResearchSource::Arxiv);
    }
    return sources;
}

void reset_fulltext_state(ResearchDashboardState *const state) {
    state->active_fulltext_discovery = 0U;
    state->fulltext_discovery = {};
    state->selected_fulltext_candidate = 0U;
    state->active_fulltext_download = 0U;
    state->fulltext_download = {};
}

void draw_fulltext_section(
    ResearchDashboardState *const state,
    FullTextService *const service,
    const WorkSummary &work
) {
    const bool discovery_pending = state->active_fulltext_discovery != 0U &&
        state->fulltext_discovery.status == SearchStatus::Pending;
    const bool download_pending = state->active_fulltext_download != 0U &&
        state->fulltext_download.status == SearchStatus::Pending;
    if (discovery_pending || download_pending) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Find full text")) {
        state->active_fulltext_discovery = service->begin_fulltext_discovery({work});
        state->fulltext_discovery = {};
        state->selected_fulltext_candidate = 0U;
        state->active_fulltext_download = 0U;
        state->fulltext_download = {};
        if (discovery_pending || download_pending) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Finding open-access locations without downloading files...");
        return;
    }
    if (discovery_pending || download_pending) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Discovery is read-only; download remains explicit.");

    if (discovery_pending) {
        ImGui::TextDisabled("Finding open-access locations without downloading files...");
        return;
    }
    if (state->active_fulltext_discovery == 0U) {
        return;
    }
    if (state->fulltext_discovery.status == SearchStatus::Failed) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.45F, 0.45F, 1.0F),
            "%s",
            state->fulltext_discovery.error.c_str()
        );
        return;
    }
    if (!state->fulltext_discovery.notice.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.76F, 0.31F, 1.0F),
            "%s",
            state->fulltext_discovery.notice.c_str()
        );
    }
    if (state->fulltext_discovery.candidates.empty()) {
        ImGui::TextDisabled("No safe open-access location was found for this work.");
        return;
    }

    ImGui::TextDisabled("OPEN-ACCESS CANDIDATES");
    ImGui::BeginChild("FullTextCandidates", ImVec2(0.0F, 132.0F), true);
    if (ImGui::BeginTable(
            "FullTextCandidateTable",
            4,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_SizingStretchProp
        )) {
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 1.8F);
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthStretch, 1.1F);
        ImGui::TableSetupColumn("License", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableHeadersRow();
        for (const FullTextCandidate &candidate : state->fulltext_discovery.candidates) {
            ImGui::PushID(static_cast<int>(candidate.id));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = state->selected_fulltext_candidate == candidate.id;
            const std::string source_label = candidate.source +
                (candidate.direct_pdf ? " / PDF" : " / landing page");
            if (ImGui::Selectable(
                    source_label.c_str(),
                    selected,
                    ImGuiSelectableFlags_SpanAllColumns
                )) {
                state->selected_fulltext_candidate = candidate.id;
                state->active_fulltext_download = 0U;
                state->fulltext_download = {};
            }
            ImGui::TableSetColumnIndex(1);
            const std::string &location = candidate.repository.empty()
                ? candidate.host_type : candidate.repository;
            ImGui::TextUnformatted(location.empty() ? "Open-access host" : location.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(candidate.version.empty() ? "Unspecified" : candidate.version.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(candidate.license.empty() ? "Unspecified" : candidate.license.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    const bool can_download = state->selected_fulltext_candidate != 0U &&
        !download_pending;
    if (!can_download) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Download selected PDF")) {
        state->active_fulltext_download = service->begin_fulltext_download(
            state->selected_fulltext_candidate
        );
        state->fulltext_download = {};
    }
    if (!can_download) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (state->selected_fulltext_candidate == 0U) {
        ImGui::TextDisabled("Select one candidate to enable download.");
    } else if (download_pending) {
        ImGui::TextDisabled("Downloading selected PDF with safety checks...");
    } else if (state->active_fulltext_download != 0U &&
               state->fulltext_download.status == SearchStatus::Failed) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.45F, 0.45F, 1.0F),
            "%s",
            state->fulltext_download.error.c_str()
        );
    } else if (state->active_fulltext_download != 0U &&
               state->fulltext_download.status == SearchStatus::Ready) {
        ImGui::TextColored(
            ImVec4(0.35F, 0.91F, 0.61F, 1.0F),
            "Saved %s (%llu bytes)",
            state->fulltext_download.local_filename.c_str(),
            static_cast<unsigned long long>(state->fulltext_download.byte_count)
        );
        ImGui::TextDisabled(
            "SHA-256: %s",
            state->fulltext_download.sha256.c_str()
        );
    }
}

}  // namespace

void draw_research_dashboard(
    ResearchDashboardState &state,
    ResearchService &service,
    FullTextService &fulltext_service,
    const RendererDiagnostics &renderer_diagnostics
) {
    if (state.active_search != 0U &&
        state.search_snapshot.status == SearchStatus::Pending) {
        state.search_snapshot = service.poll_search(state.active_search);
    }
    if (state.active_fulltext_discovery != 0U &&
        state.fulltext_discovery.status == SearchStatus::Pending) {
        state.fulltext_discovery = fulltext_service.poll_fulltext_discovery(
            state.active_fulltext_discovery
        );
    }
    if (state.active_fulltext_download != 0U &&
        state.fulltext_download.status == SearchStatus::Pending) {
        state.fulltext_download = fulltext_service.poll_fulltext_download(
            state.active_fulltext_download
        );
    }
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(io.DisplaySize);
    constexpr ImGuiWindowFlags workspace_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Research Harvester##workspace", nullptr, workspace_flags);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.77F, 0.91F, 1.0F, 1.0F));
    ImGui::TextUnformatted("RESEARCH HARVESTER");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("native desktop client / RHI baseline");
    ImGui::Separator();

    ImGui::BeginChild("Navigation", ImVec2(188.0F, 0.0F), true);
    ImGui::TextDisabled("WORKSPACE");
    ImGui::Selectable("Collect", true);
    ImGui::Selectable("Library", false);
    ImGui::Selectable("PDF imports", false);
    ImGui::Selectable("Citation graph", false);
    ImGui::Spacing();
    ImGui::TextDisabled("RENDERER");
    ImGui::TextUnformatted(renderer_diagnostics.backend_name.c_str());
    if (!renderer_diagnostics.backend_version.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", renderer_diagnostics.backend_version.c_str());
    }
    ImGui::TextColored(
        ImVec4(0.35F, 0.91F, 0.61F, 1.0F),
        "Online"
    );
    if (renderer_diagnostics.dynamic_textures_enabled) {
        ImGui::TextDisabled("Dynamic textures");
    }
    if (renderer_diagnostics.maximum_texture_dimension != 0U) {
        ImGui::TextDisabled(
            "Max: %u px",
            renderer_diagnostics.maximum_texture_dimension
        );
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Collection", ImVec2(0.0F, 0.0F), false);
    ImGui::Text("Search the research graph");
    ImGui::TextDisabled(
        "The native view uses a typed service; the on-demand Python worker never receives UI or graphics objects."
    );
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint(
        "##query",
        "Research question or keywords",
        state.query.data(),
        state.query.size()
    );
    ImGui::SetNextItemWidth(130.0F);
    ImGui::SliderInt("Results per source", &state.result_limit, 1, 100);
    ImGui::Spacing();
    ImGui::TextDisabled("SOURCES");
    draw_source_toggle("OpenAlex", &state.include_openalex, "works + citations");
    draw_source_toggle("Crossref", &state.include_crossref, "DOI metadata");
    draw_source_toggle("arXiv", &state.include_arxiv, "preprints");
    ImGui::Spacing();
    const bool search_in_flight =
        state.active_search != 0U &&
        state.search_snapshot.status == SearchStatus::Pending;
    if (search_in_flight) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Search sources", ImVec2(160.0F, 36.0F))) {
        SearchRequest request{};
        request.query = state.query.data();
        request.result_limit = static_cast<std::uint32_t>(state.result_limit);
        request.sources = selected_sources(state);
        state.active_search = service.begin_search(request);
        state.search_snapshot = {};
        state.selected_work = 0U;
        reset_fulltext_state(&state);
    }
    if (search_in_flight) {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Searching without blocking the render loop...");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("RESULTS");
    if (state.active_search == 0U) {
        ImGui::TextDisabled("Choose sources and start a search.");
    } else if (state.search_snapshot.status == SearchStatus::Pending) {
        ImGui::TextDisabled("Query accepted; waiting for the service result.");
    } else if (state.search_snapshot.status == SearchStatus::Failed) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.45F, 0.45F, 1.0F),
            "%s",
            state.search_snapshot.error.c_str()
        );
    } else {
        if (!state.search_snapshot.notice.empty()) {
            ImGui::TextColored(
                ImVec4(1.0F, 0.76F, 0.31F, 1.0F),
                "%s",
                state.search_snapshot.notice.c_str()
            );
        }
        ImGui::BeginChild("SearchResults", ImVec2(0.0F, 180.0F), true);
        if (ImGui::BeginTable(
                "Works",
                4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_SizingStretchProp
            )) {
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 2.7F);
            ImGui::TableSetupColumn("Year", ImGuiTableColumnFlags_WidthFixed, 62.0F);
            ImGui::TableSetupColumn("Cites", ImGuiTableColumnFlags_WidthFixed, 62.0F);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 105.0F);
            ImGui::TableHeadersRow();
            for (std::size_t index = 0U;
                 index < state.search_snapshot.works.size();
                 ++index) {
                const WorkSummary &work = state.search_snapshot.works[index];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool selected = state.selected_work == index;
                if (ImGui::Selectable(
                        work.title.c_str(),
                        selected,
                        ImGuiSelectableFlags_SpanAllColumns
                    )) {
                state.selected_work = index;
                reset_fulltext_state(&state);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", work.year);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u", work.citation_count);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(work.source.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        if (!state.search_snapshot.works.empty()) {
            const WorkSummary &selected = state.search_snapshot.works.at(
                std::min(
                    state.selected_work,
                    state.search_snapshot.works.size() - 1U
                )
            );
            ImGui::Spacing();
            ImGui::TextDisabled("SELECTED WORK");
            ImGui::TextUnformatted(selected.title.c_str());
            ImGui::TextDisabled(
                "%s  /  %s  /  %s",
                selected.authors.c_str(),
                selected.reference.c_str(),
                selected.source.c_str()
            );
            ImGui::BeginDisabled();
            ImGui::Button("Queue enrichment");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Enrichment is not available in this increment.");
            if (!selected.doi.empty()) {
                ImGui::TextDisabled("DOI: %s", selected.doi.c_str());
            }
            draw_fulltext_section(&state, &fulltext_service, selected);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace harvester::app
