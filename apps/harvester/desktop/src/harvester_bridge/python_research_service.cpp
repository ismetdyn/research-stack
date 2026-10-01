#include <harvester/app/python_research_service.hpp>

#include <harvester/app/worker_protocol.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <fcntl.h>
#include <optional>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace harvester::app {

namespace {

constexpr std::size_t maximum_concurrent_workers = 4U;
constexpr std::size_t maximum_cached_candidates = 1200U;
constexpr std::size_t maximum_stdout_bytes = 1024U * 1024U;
constexpr std::size_t maximum_stderr_bytes = 16U * 1024U;
constexpr std::size_t maximum_work_field_bytes = 4096U;
constexpr auto worker_timeout = std::chrono::seconds(45);
constexpr auto termination_grace = std::chrono::seconds(2);

SearchSnapshot search_failure(const std::string_view message) {
    return {SearchStatus::Failed, {}, std::string{message}, {}};
}

FullTextDiscoverySnapshot discovery_failure(const std::string_view message) {
    return {SearchStatus::Failed, {}, std::string{message}, {}};
}

FullTextDownloadSnapshot download_failure(const std::string_view message) {
    return {SearchStatus::Failed, {}, 0U, {}, std::string{message}};
}

SearchSnapshot invalid_search_request() {
    return search_failure(
        "Enter a query, choose one or more sources, and use a limit from 1 to 100."
    );
}

void close_descriptor(int *const descriptor) {
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

bool set_nonblocking(const int descriptor) {
    const int flags = fcntl(descriptor, F_GETFL);
    return flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool is_blank(const std::string &value) {
    return std::all_of(
        value.begin(), value.end(),
        [](const unsigned char character) { return std::isspace(character) != 0; }
    );
}

bool valid_text(
    const std::string &value,
    const std::size_t maximum_size,
    const bool require_nonblank
) {
    return value.size() <= maximum_size && value.find('\0') == std::string::npos &&
        (!require_nonblank || (!value.empty() && !is_blank(value)));
}

std::optional<std::vector<std::string>> source_arguments(
    const SearchRequest &request
) {
    std::vector<std::string> sources;
    for (const ResearchSource source : request.sources) {
        std::string name;
        switch (source) {
            case ResearchSource::OpenAlex:
                name = "openalex";
                break;
            case ResearchSource::Crossref:
                name = "crossref";
                break;
            case ResearchSource::Arxiv:
                name = "arxiv";
                break;
            default:
                return std::nullopt;
        }
        if (std::find(sources.begin(), sources.end(), name) == sources.end()) {
            sources.push_back(std::move(name));
        }
    }
    return sources.empty() ? std::nullopt
                           : std::optional<std::vector<std::string>>{std::move(sources)};
}

bool valid_search_request(const SearchRequest &request) {
    return valid_text(request.query, 240U, true) && request.result_limit >= 1U &&
        request.result_limit <= 100U && source_arguments(request).has_value();
}

bool valid_discovery_request(const FullTextDiscoveryRequest &request) {
    return valid_text(request.work.title, maximum_work_field_bytes, true) &&
        valid_text(request.work.doi, maximum_work_field_bytes, false) &&
        valid_text(request.work.open_access_url, maximum_work_field_bytes, false);
}

void close_child_descriptors(
    const int input_descriptor,
    const int stdout_read,
    const int stdout_write,
    const int stderr_read,
    const int stderr_write
) {
    const std::array<int, 5U> descriptors{
        input_descriptor, stdout_read, stdout_write, stderr_read, stderr_write,
    };
    for (const int descriptor : descriptors) {
        if (descriptor > STDERR_FILENO) {
            (void)close(descriptor);
        }
    }
}

std::uint64_t next_nonzero(std::uint64_t *const value) {
    const std::uint64_t result = *value;
    ++(*value);
    if (*value == 0U) {
        ++(*value);
    }
    return result;
}

}  // namespace

struct PythonResearchService::Impl {
    enum class WorkerOutcome : std::uint8_t {
        Succeeded,
        OutputLimitExceeded,
        TimedOut,
        ProcessFailed,
    };

    struct PendingWorker {
        pid_t process_id = -1;
        int stdout_descriptor = -1;
        int stderr_descriptor = -1;
        std::string stdout_data;
        std::string stderr_data;
        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point termination_requested_at{};
        std::optional<WorkerOutcome> outcome;
        bool termination_requested = false;
        bool response_exceeded_limit = false;
        bool timed_out = false;
    };

    template <typename Snapshot>
    struct PendingOperation {
        PendingWorker worker;
        std::optional<Snapshot> completion;
        std::string title;
    };

    struct CachedCandidate {
        WorkerFullTextCandidate worker_candidate;
        std::string title;
    };

    using PendingSearch = PendingOperation<SearchSnapshot>;
    using PendingDiscovery = PendingOperation<FullTextDiscoverySnapshot>;
    using PendingDownload = PendingOperation<FullTextDownloadSnapshot>;

    explicit Impl(PythonWorkerLaunch requested_launch)
        : launch(std::move(requested_launch)) {}

    PythonWorkerLaunch launch;
    std::vector<std::pair<SearchTicket, PendingSearch>> searches;
    std::vector<std::pair<FullTextDiscoveryTicket, PendingDiscovery>> discoveries;
    std::vector<std::pair<FullTextDownloadTicket, PendingDownload>> downloads;
    std::vector<std::pair<FullTextCandidateId, CachedCandidate>> candidates;
    SearchTicket next_search_ticket = 1U;
    FullTextDiscoveryTicket next_discovery_ticket = 1U;
    FullTextDownloadTicket next_download_ticket = 1U;
    FullTextCandidateId next_candidate_id = 1U;

    [[nodiscard]] bool configured() const {
        return !launch.working_directory.empty() &&
            !launch.executable_and_fixed_arguments.empty() &&
            !launch.executable_and_fixed_arguments.front().empty();
    }

    [[nodiscard]] std::size_t operation_count() const {
        return searches.size() + discoveries.size() + downloads.size();
    }

    void request_termination(PendingWorker *const pending) {
        if (pending->process_id > 0 && !pending->termination_requested &&
            (kill(pending->process_id, SIGTERM) == 0 || errno == ESRCH)) {
            pending->termination_requested = true;
            pending->termination_requested_at = std::chrono::steady_clock::now();
        }
    }

    void read_available(
        PendingWorker *const pending,
        int *const descriptor,
        std::string *const destination,
        const std::size_t maximum_size
    ) {
        std::array<char, 4096U> buffer{};
        while (*descriptor >= 0) {
            const ssize_t bytes_read = read(*descriptor, buffer.data(), buffer.size());
            if (bytes_read > 0) {
                const std::size_t byte_count = static_cast<std::size_t>(bytes_read);
                if (destination->size() > maximum_size - byte_count) {
                    pending->response_exceeded_limit = true;
                    request_termination(pending);
                    return;
                }
                destination->append(buffer.data(), byte_count);
                continue;
            }
            if (bytes_read == 0) {
                close_descriptor(descriptor);
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close_descriptor(descriptor);
            }
            return;
        }
    }

    bool launch_worker(
        const std::vector<std::string> &request_arguments,
        PendingWorker *const pending
    ) {
        std::vector<std::string> arguments = launch.executable_and_fixed_arguments;
        arguments.insert(arguments.end(), request_arguments.begin(), request_arguments.end());
        std::vector<char *> raw_arguments;
        raw_arguments.reserve(arguments.size() + 1U);
        for (std::string &argument : arguments) {
            raw_arguments.push_back(argument.data());
        }
        raw_arguments.push_back(nullptr);

        int stdout_pipe[2] = {-1, -1};
        int stderr_pipe[2] = {-1, -1};
        const int input_descriptor = open("/dev/null", O_RDONLY);
        if (input_descriptor < 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
            if (input_descriptor >= 0) {
                int closable_input = input_descriptor;
                close_descriptor(&closable_input);
            }
            close_child_descriptors(
                -1, stdout_pipe[0], stdout_pipe[1], stderr_pipe[0], stderr_pipe[1]
            );
            return false;
        }
        const pid_t process_id = fork();
        if (process_id == 0) {
            if (chdir(launch.working_directory.c_str()) != 0 ||
                dup2(input_descriptor, STDIN_FILENO) < 0 ||
                dup2(stdout_pipe[1], STDOUT_FILENO) < 0 ||
                dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
                _exit(127);
            }
            close_child_descriptors(
                input_descriptor,
                stdout_pipe[0],
                stdout_pipe[1],
                stderr_pipe[0],
                stderr_pipe[1]
            );
            execvp(raw_arguments.front(), raw_arguments.data());
            _exit(127);
        }

        int closable_input = input_descriptor;
        int closable_stdout_write = stdout_pipe[1];
        int closable_stderr_write = stderr_pipe[1];
        close_descriptor(&closable_input);
        close_descriptor(&closable_stdout_write);
        close_descriptor(&closable_stderr_write);
        if (process_id < 0 || !set_nonblocking(stdout_pipe[0]) ||
            !set_nonblocking(stderr_pipe[0])) {
            int closable_stdout_read = stdout_pipe[0];
            int closable_stderr_read = stderr_pipe[0];
            close_descriptor(&closable_stdout_read);
            close_descriptor(&closable_stderr_read);
            if (process_id > 0) {
                (void)kill(process_id, SIGKILL);
                int status = 0;
                while (waitpid(process_id, &status, 0) < 0 && errno == EINTR) {
                }
            }
            return false;
        }
        pending->process_id = process_id;
        pending->stdout_descriptor = stdout_pipe[0];
        pending->stderr_descriptor = stderr_pipe[0];
        pending->started_at = std::chrono::steady_clock::now();
        return true;
    }

    void finish_worker(PendingWorker *const pending, const WorkerOutcome outcome) {
        close_descriptor(&pending->stdout_descriptor);
        close_descriptor(&pending->stderr_descriptor);
        pending->outcome = outcome;
        pending->process_id = -1;
    }

    void update_worker(PendingWorker *const pending) {
        if (pending->outcome.has_value() || pending->process_id <= 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!pending->timed_out && now - pending->started_at > worker_timeout) {
            pending->timed_out = true;
            request_termination(pending);
        }
        if (pending->termination_requested &&
            now - pending->termination_requested_at > termination_grace) {
            (void)kill(pending->process_id, SIGKILL);
        }
        read_available(
            pending, &pending->stdout_descriptor, &pending->stdout_data, maximum_stdout_bytes
        );
        read_available(
            pending, &pending->stderr_descriptor, &pending->stderr_data, maximum_stderr_bytes
        );
        int status = 0;
        const pid_t waited = waitpid(pending->process_id, &status, WNOHANG);
        if (waited == 0) {
            return;
        }
        if (waited < 0) {
            if (errno != EINTR) {
                finish_worker(pending, WorkerOutcome::ProcessFailed);
            }
            return;
        }
        read_available(
            pending, &pending->stdout_descriptor, &pending->stdout_data, maximum_stdout_bytes
        );
        read_available(
            pending, &pending->stderr_descriptor, &pending->stderr_data, maximum_stderr_bytes
        );
        if (pending->response_exceeded_limit) {
            finish_worker(pending, WorkerOutcome::OutputLimitExceeded);
        } else if (pending->timed_out) {
            finish_worker(pending, WorkerOutcome::TimedOut);
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            finish_worker(pending, WorkerOutcome::Succeeded);
        } else {
            finish_worker(pending, WorkerOutcome::ProcessFailed);
        }
    }

    void stop(PendingWorker *const pending) {
        if (pending->process_id > 0) {
            (void)kill(pending->process_id, SIGTERM);
            int status = 0;
            pid_t waited = -1;
            do {
                waited = waitpid(pending->process_id, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == 0) {
                (void)kill(pending->process_id, SIGKILL);
                while (waitpid(pending->process_id, &status, 0) < 0 && errno == EINTR) {
                }
            }
        }
        close_descriptor(&pending->stdout_descriptor);
        close_descriptor(&pending->stderr_descriptor);
        pending->process_id = -1;
    }

    SearchSnapshot completed_search(const PendingWorker &worker) const {
        if (!worker.outcome.has_value()) {
            return search_failure("Python search worker failed to complete.");
        }
        if (*worker.outcome == WorkerOutcome::Succeeded) {
            return parse_python_worker_response(worker.stdout_data);
        }
        if (*worker.outcome == WorkerOutcome::OutputLimitExceeded) {
            return search_failure("Python worker response exceeded safe size.");
        }
        if (*worker.outcome == WorkerOutcome::TimedOut) {
            return search_failure("Python search worker timed out.");
        }
        const SearchSnapshot worker_report = parse_python_worker_response(worker.stdout_data);
        return worker_report.status == SearchStatus::Failed &&
                worker_report.error != "Python worker returned an invalid response."
            ? worker_report
            : search_failure("Python search worker failed to complete.");
    }

    WorkerFullTextDiscoveryResponse discovery_response(const PendingWorker &worker) const {
        if (!worker.outcome.has_value()) {
            return {SearchStatus::Failed, {}, "Python full-text worker failed to complete.", {}};
        }
        if (*worker.outcome == WorkerOutcome::Succeeded) {
            return parse_python_fulltext_discovery_response(worker.stdout_data);
        }
        if (*worker.outcome == WorkerOutcome::OutputLimitExceeded) {
            return {SearchStatus::Failed, {}, "Python worker response exceeded safe size.", {}};
        }
        if (*worker.outcome == WorkerOutcome::TimedOut) {
            return {SearchStatus::Failed, {}, "Python full-text worker timed out.", {}};
        }
        const WorkerFullTextDiscoveryResponse worker_report =
            parse_python_fulltext_discovery_response(worker.stdout_data);
        return worker_report.status == SearchStatus::Failed &&
                worker_report.error != "Python worker returned an invalid response."
            ? worker_report
            : WorkerFullTextDiscoveryResponse{
                  SearchStatus::Failed,
                  {},
                  "Python full-text worker failed to complete.",
                  {},
              };
    }

    WorkerFullTextDownloadResponse download_response(const PendingWorker &worker) const {
        if (!worker.outcome.has_value()) {
            return {SearchStatus::Failed, {}, 0U, {}, "Python full-text worker failed to complete."};
        }
        if (*worker.outcome == WorkerOutcome::Succeeded) {
            return parse_python_fulltext_download_response(worker.stdout_data);
        }
        if (*worker.outcome == WorkerOutcome::OutputLimitExceeded) {
            return {SearchStatus::Failed, {}, 0U, {}, "Python worker response exceeded safe size."};
        }
        if (*worker.outcome == WorkerOutcome::TimedOut) {
            return {SearchStatus::Failed, {}, 0U, {}, "Python full-text worker timed out."};
        }
        const WorkerFullTextDownloadResponse worker_report =
            parse_python_fulltext_download_response(worker.stdout_data);
        return worker_report.status == SearchStatus::Failed &&
                worker_report.error != "Python worker returned an invalid response."
            ? worker_report
            : WorkerFullTextDownloadResponse{
                  SearchStatus::Failed,
                  {},
                  0U,
                  {},
                  "Python full-text worker failed to complete.",
              };
    }

    FullTextDiscoverySnapshot completed_discovery(
        const PendingWorker &worker,
        const std::string &title
    ) {
        WorkerFullTextDiscoveryResponse response = discovery_response(worker);
        if (response.status != SearchStatus::Ready) {
            return discovery_failure(response.error);
        }
        if (response.candidates.size() > maximum_cached_candidates - candidates.size()) {
            return discovery_failure("Full-text candidates exceeded safe size.");
        }
        std::vector<FullTextCandidate> public_candidates;
        public_candidates.reserve(response.candidates.size());
        for (WorkerFullTextCandidate &candidate : response.candidates) {
            const FullTextCandidateId id = next_nonzero(&next_candidate_id);
            public_candidates.push_back(
                {
                    id,
                    candidate.source,
                    candidate.repository,
                    candidate.license,
                    candidate.version,
                    candidate.host_type,
                    candidate.direct_pdf,
                }
            );
            candidates.push_back({id, {std::move(candidate), title}});
        }
        return {SearchStatus::Ready, std::move(public_candidates), {}, std::move(response.notice)};
    }

    FullTextDownloadSnapshot completed_download(const PendingWorker &worker) const {
        WorkerFullTextDownloadResponse response = download_response(worker);
        return response.status == SearchStatus::Ready
            ? FullTextDownloadSnapshot{
                  SearchStatus::Ready,
                  std::move(response.local_filename),
                  response.byte_count,
                  std::move(response.sha256),
                  {},
              }
            : download_failure(response.error);
    }
};

PythonResearchService::PythonResearchService(PythonWorkerLaunch launch)
    : impl_(std::make_unique<Impl>(std::move(launch))) {}

PythonResearchService::~PythonResearchService() {
    for (auto &entry : impl_->searches) {
        impl_->stop(&entry.second.worker);
    }
    for (auto &entry : impl_->discoveries) {
        impl_->stop(&entry.second.worker);
    }
    for (auto &entry : impl_->downloads) {
        impl_->stop(&entry.second.worker);
    }
}

SearchTicket PythonResearchService::begin_search(const SearchRequest &request) {
    const SearchTicket ticket = next_nonzero(&impl_->next_search_ticket);
    Impl::PendingSearch pending{};
    const std::optional<std::vector<std::string>> sources = source_arguments(request);
    if (!valid_search_request(request) || !sources.has_value()) {
        pending.completion = invalid_search_request();
    } else if (!impl_->configured()) {
        pending.completion = search_failure("Python search worker is not configured.");
    } else if (impl_->operation_count() >= maximum_concurrent_workers) {
        pending.completion = search_failure("Python search worker could not start.");
    } else {
        std::vector<std::string> arguments{
            "--query", request.query, "--limit", std::to_string(request.result_limit),
        };
        for (const std::string &source : *sources) {
            arguments.push_back("--source");
            arguments.push_back(source);
        }
        if (!impl_->launch_worker(arguments, &pending.worker)) {
            pending.completion = search_failure("Python search worker could not start.");
        }
    }
    impl_->searches.push_back({ticket, std::move(pending)});
    return ticket;
}

SearchSnapshot PythonResearchService::poll_search(const SearchTicket ticket) {
    const auto found = std::find_if(
        impl_->searches.begin(), impl_->searches.end(),
        [ticket](const auto &entry) { return entry.first == ticket; }
    );
    if (found == impl_->searches.end()) {
        return search_failure("Unknown desktop search request.");
    }
    if (!found->second.completion.has_value()) {
        impl_->update_worker(&found->second.worker);
        if (found->second.worker.outcome.has_value()) {
            found->second.completion = impl_->completed_search(found->second.worker);
        }
    }
    if (!found->second.completion.has_value()) {
        return {};
    }
    SearchSnapshot completed = std::move(*found->second.completion);
    impl_->searches.erase(found);
    return completed;
}

FullTextDiscoveryTicket PythonResearchService::begin_fulltext_discovery(
    const FullTextDiscoveryRequest &request
) {
    const FullTextDiscoveryTicket ticket = next_nonzero(&impl_->next_discovery_ticket);
    Impl::PendingDiscovery pending{};
    pending.title = request.work.title;
    if (!valid_discovery_request(request)) {
        pending.completion = discovery_failure("Selected work cannot be used for full-text discovery.");
    } else if (!impl_->configured()) {
        pending.completion = discovery_failure("Python full-text worker is not configured.");
    } else if (impl_->operation_count() >= maximum_concurrent_workers) {
        pending.completion = discovery_failure("Python full-text worker could not start.");
    } else {
        std::vector<std::string> arguments{"--operation", "discover", "--title", request.work.title};
        if (!request.work.doi.empty()) {
            arguments.push_back("--doi");
            arguments.push_back(request.work.doi);
        }
        if (!request.work.open_access_url.empty()) {
            arguments.push_back("--open-access-url");
            arguments.push_back(request.work.open_access_url);
        }
        if (!impl_->launch_worker(arguments, &pending.worker)) {
            pending.completion = discovery_failure("Python full-text worker could not start.");
        }
    }
    impl_->discoveries.push_back({ticket, std::move(pending)});
    return ticket;
}

FullTextDiscoverySnapshot PythonResearchService::poll_fulltext_discovery(
    const FullTextDiscoveryTicket ticket
) {
    const auto found = std::find_if(
        impl_->discoveries.begin(), impl_->discoveries.end(),
        [ticket](const auto &entry) { return entry.first == ticket; }
    );
    if (found == impl_->discoveries.end()) {
        return discovery_failure("Unknown desktop full-text discovery request.");
    }
    if (!found->second.completion.has_value()) {
        impl_->update_worker(&found->second.worker);
        if (found->second.worker.outcome.has_value()) {
            found->second.completion = impl_->completed_discovery(
                found->second.worker, found->second.title
            );
        }
    }
    if (!found->second.completion.has_value()) {
        return {};
    }
    FullTextDiscoverySnapshot completed = std::move(*found->second.completion);
    impl_->discoveries.erase(found);
    return completed;
}

FullTextDownloadTicket PythonResearchService::begin_fulltext_download(
    const FullTextCandidateId candidate_id
) {
    const FullTextDownloadTicket ticket = next_nonzero(&impl_->next_download_ticket);
    Impl::PendingDownload pending{};
    const auto candidate = std::find_if(
        impl_->candidates.begin(), impl_->candidates.end(),
        [candidate_id](const auto &entry) { return entry.first == candidate_id; }
    );
    if (candidate == impl_->candidates.end()) {
        pending.completion = download_failure("Unknown desktop full-text candidate.");
    } else if (!impl_->configured()) {
        pending.completion = download_failure("Python full-text worker is not configured.");
    } else if (impl_->operation_count() >= maximum_concurrent_workers) {
        pending.completion = download_failure("Python full-text worker could not start.");
    } else {
        const Impl::CachedCandidate &cached = candidate->second;
        const std::vector<std::string> arguments{
            "--operation", "download", "--title", cached.title, "--candidate-url",
            cached.worker_candidate.remote_url, "--candidate-source", cached.worker_candidate.source,
        };
        if (!impl_->launch_worker(arguments, &pending.worker)) {
            pending.completion = download_failure("Python full-text worker could not start.");
        }
    }
    impl_->downloads.push_back({ticket, std::move(pending)});
    return ticket;
}

FullTextDownloadSnapshot PythonResearchService::poll_fulltext_download(
    const FullTextDownloadTicket ticket
) {
    const auto found = std::find_if(
        impl_->downloads.begin(), impl_->downloads.end(),
        [ticket](const auto &entry) { return entry.first == ticket; }
    );
    if (found == impl_->downloads.end()) {
        return download_failure("Unknown desktop full-text download request.");
    }
    if (!found->second.completion.has_value()) {
        impl_->update_worker(&found->second.worker);
        if (found->second.worker.outcome.has_value()) {
            found->second.completion = impl_->completed_download(found->second.worker);
        }
    }
    if (!found->second.completion.has_value()) {
        return {};
    }
    FullTextDownloadSnapshot completed = std::move(*found->second.completion);
    impl_->downloads.erase(found);
    return completed;
}

}  // namespace harvester::app
