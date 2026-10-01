#ifndef HARVESTER_APP_RESEARCH_SERVICE_HPP
#define HARVESTER_APP_RESEARCH_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace harvester::app {

enum class ResearchSource : std::uint8_t {
    OpenAlex,
    Crossref,
    Arxiv,
};

struct SearchRequest {
    std::string query;
    std::uint32_t result_limit = 20U;
    std::vector<ResearchSource> sources;
};

struct WorkSummary {
    std::string reference;
    std::string title;
    std::string authors;
    std::string source;
    std::uint32_t year = 0U;
    std::uint32_t citation_count = 0U;
    std::string doi;
    std::string open_access_url;
};

enum class SearchStatus : std::uint8_t {
    Pending,
    Ready,
    Failed,
};

struct SearchSnapshot {
    SearchStatus status = SearchStatus::Pending;
    std::vector<WorkSummary> works;
    std::string error;
    std::string notice;
};

using SearchTicket = std::uint64_t;

using FullTextCandidateId = std::uint64_t;
using FullTextDiscoveryTicket = std::uint64_t;
using FullTextDownloadTicket = std::uint64_t;

struct FullTextDiscoveryRequest {
    WorkSummary work;
};

/* Deliberately excludes the remote URL; only the service owns that detail. */
struct FullTextCandidate {
    FullTextCandidateId id = 0U;
    std::string source;
    std::string repository;
    std::string license;
    std::string version;
    std::string host_type;
    bool direct_pdf = false;
};

struct FullTextDiscoverySnapshot {
    SearchStatus status = SearchStatus::Pending;
    std::vector<FullTextCandidate> candidates;
    std::string error;
    std::string notice;
};

struct FullTextDownloadSnapshot {
    SearchStatus status = SearchStatus::Pending;
    std::string local_filename;
    std::uint64_t byte_count = 0U;
    std::string sha256;
    std::string error;
};

/*
 * Application-service boundary for desktop workflows. Implementations may use
 * the current Python core, a remote worker, or a deterministic fake, but none
 * of them receive ImGui/RHI/graphics objects.
 */
class ResearchService {
  public:
    virtual ~ResearchService() = default;

    virtual SearchTicket begin_search(const SearchRequest &request) = 0;
    [[nodiscard]] virtual SearchSnapshot poll_search(SearchTicket ticket) = 0;
};

/*
 * Explicitly selected full-text operations. The screen can request discovery
 * for a selected work and download a candidate ID, but can never submit a
 * remote URL or output path.
 */
class FullTextService {
  public:
    virtual ~FullTextService() = default;

    virtual FullTextDiscoveryTicket begin_fulltext_discovery(
        const FullTextDiscoveryRequest &request
    ) = 0;
    [[nodiscard]] virtual FullTextDiscoverySnapshot poll_fulltext_discovery(
        FullTextDiscoveryTicket ticket
    ) = 0;
    virtual FullTextDownloadTicket begin_fulltext_download(
        FullTextCandidateId candidate_id
    ) = 0;
    [[nodiscard]] virtual FullTextDownloadSnapshot poll_fulltext_download(
        FullTextDownloadTicket ticket
    ) = 0;
};

class FakeResearchService final : public ResearchService {
  public:
    SearchTicket begin_search(const SearchRequest &request) override;
    [[nodiscard]] SearchSnapshot poll_search(SearchTicket ticket) override;

  private:
    struct PendingSearch {
        SearchRequest request;
        bool observed_pending = false;
    };

    std::vector<std::pair<SearchTicket, PendingSearch>> searches_;
    SearchTicket next_ticket_ = 1U;
};

}  // namespace harvester::app

#endif
