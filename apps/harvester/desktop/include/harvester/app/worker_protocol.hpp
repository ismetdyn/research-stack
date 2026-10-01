#ifndef HARVESTER_APP_WORKER_PROTOCOL_HPP
#define HARVESTER_APP_WORKER_PROTOCOL_HPP

#include <string_view>
#include <cstdint>
#include <string>
#include <vector>

#include <harvester/app/research_service.hpp>

namespace harvester::app {

/*
 * Parses the bounded, base64-encoded stdout protocol emitted by
 * research_harvester.desktop_bridge. Invalid worker data never reaches the UI
 * as raw text; it becomes a generic failed snapshot instead.
 */
[[nodiscard]] SearchSnapshot parse_python_worker_response(
    std::string_view output
);

/* Internal worker records. Their remote URL is never exposed through FullTextService. */
struct WorkerFullTextCandidate {
    std::string remote_url;
    std::string source;
    std::string repository;
    std::string license;
    std::string version;
    std::string host_type;
    bool direct_pdf = false;
};

struct WorkerFullTextDiscoveryResponse {
    SearchStatus status = SearchStatus::Failed;
    std::vector<WorkerFullTextCandidate> candidates;
    std::string error;
    std::string notice;
};

struct WorkerFullTextDownloadResponse {
    SearchStatus status = SearchStatus::Failed;
    std::string local_filename;
    std::uint64_t byte_count = 0U;
    std::string sha256;
    std::string error;
};

[[nodiscard]] WorkerFullTextDiscoveryResponse
parse_python_fulltext_discovery_response(std::string_view output);
[[nodiscard]] WorkerFullTextDownloadResponse
parse_python_fulltext_download_response(std::string_view output);

}  // namespace harvester::app

#endif
