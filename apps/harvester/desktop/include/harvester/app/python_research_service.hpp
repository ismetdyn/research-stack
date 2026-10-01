#ifndef HARVESTER_APP_PYTHON_RESEARCH_SERVICE_HPP
#define HARVESTER_APP_PYTHON_RESEARCH_SERVICE_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <harvester/app/research_service.hpp>

namespace harvester::app {

/*
 * Composition-root configuration for the trusted Python worker. The command
 * and its fixed arguments are never derived from a search request; request
 * fields are supplied to execvp as individual argv values, never a shell.
 */
struct PythonWorkerLaunch {
    std::filesystem::path working_directory;
    std::vector<std::string> executable_and_fixed_arguments;
};

class PythonResearchService final : public ResearchService, public FullTextService {
  public:
    explicit PythonResearchService(PythonWorkerLaunch launch);
    ~PythonResearchService() override;

    PythonResearchService(const PythonResearchService &) = delete;
    PythonResearchService &operator=(const PythonResearchService &) = delete;
    PythonResearchService(PythonResearchService &&) = delete;
    PythonResearchService &operator=(PythonResearchService &&) = delete;

    SearchTicket begin_search(const SearchRequest &request) override;
    [[nodiscard]] SearchSnapshot poll_search(SearchTicket ticket) override;
    FullTextDiscoveryTicket begin_fulltext_discovery(
        const FullTextDiscoveryRequest &request
    ) override;
    [[nodiscard]] FullTextDiscoverySnapshot poll_fulltext_discovery(
        FullTextDiscoveryTicket ticket
    ) override;
    FullTextDownloadTicket begin_fulltext_download(
        FullTextCandidateId candidate_id
    ) override;
    [[nodiscard]] FullTextDownloadSnapshot poll_fulltext_download(
        FullTextDownloadTicket ticket
    ) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace harvester::app

#endif
