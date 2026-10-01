#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include <harvester/app/python_research_service.hpp>

namespace {

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "python_research_service_test: " << message << '\n';
        std::exit(1);
    }
}

harvester::app::SearchSnapshot wait_for_completion(
    harvester::app::ResearchService &service,
    const harvester::app::SearchTicket ticket
) {
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        harvester::app::SearchSnapshot snapshot = service.poll_search(ticket);
        if (snapshot.status != harvester::app::SearchStatus::Pending) {
            return snapshot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {
        harvester::app::SearchStatus::Failed,
        {},
        "worker fixture timed out",
        {},
    };
}

harvester::app::SearchRequest request_for(const std::string_view query) {
    return {
        std::string{query},
        5U,
        {harvester::app::ResearchSource::OpenAlex},
    };
}

harvester::app::FullTextDiscoverySnapshot wait_for_discovery(
    harvester::app::FullTextService &service,
    const harvester::app::FullTextDiscoveryTicket ticket
) {
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        harvester::app::FullTextDiscoverySnapshot snapshot =
            service.poll_fulltext_discovery(ticket);
        if (snapshot.status != harvester::app::SearchStatus::Pending) {
            return snapshot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {
        harvester::app::SearchStatus::Failed,
        {},
        "worker fixture timed out",
        {},
    };
}

harvester::app::FullTextDownloadSnapshot wait_for_download(
    harvester::app::FullTextService &service,
    const harvester::app::FullTextDownloadTicket ticket
) {
    for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
        harvester::app::FullTextDownloadSnapshot snapshot =
            service.poll_fulltext_download(ticket);
        if (snapshot.status != harvester::app::SearchStatus::Pending) {
            return snapshot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {
        harvester::app::SearchStatus::Failed,
        {},
        0U,
        {},
        "worker fixture timed out",
    };
}

}  // namespace

int main() {
    const harvester::app::PythonWorkerLaunch launch{
        PYTHON_WORKER_FIXTURE_WORKDIR,
        {"uv", "run", "python", PYTHON_WORKER_FIXTURE_PATH},
    };
    harvester::app::PythonResearchService service{launch};

    const harvester::app::SearchSnapshot successful = wait_for_completion(
        service,
        service.begin_search(request_for("valid"))
    );
    expect(
        successful.status == harvester::app::SearchStatus::Ready,
        "a valid worker response must reach ready"
    );
    expect(successful.works.size() == 1U, "one fixture work must reach the service");
    expect(successful.works[0].title == "Title", "fixture work must decode");

    const harvester::app::SearchSnapshot malformed = wait_for_completion(
        service,
        service.begin_search(request_for("malformed"))
    );
    expect(
        malformed.status == harvester::app::SearchStatus::Failed,
        "malformed worker output must fail"
    );
    expect(
        malformed.error == "Python worker returned an invalid response.",
        "malformed output must not reach the UI"
    );

    const harvester::app::SearchSnapshot oversized = wait_for_completion(
        service,
        service.begin_search(request_for("oversized"))
    );
    expect(
        oversized.status == harvester::app::SearchStatus::Failed,
        "oversized worker output must fail"
    );
    expect(
        oversized.error == "Python worker response exceeded safe size.",
        "output size limit must be reported safely"
    );

    const harvester::app::SearchSnapshot worker_error = wait_for_completion(
        service,
        service.begin_search(request_for("worker-error"))
    );
    expect(
        worker_error.status == harvester::app::SearchStatus::Failed,
        "a validated worker error must fail"
    );
    expect(
        worker_error.error == "Selected sources did not respond.",
        "validated worker errors must remain useful without exposing stderr"
    );

    harvester::app::SearchRequest invalid = request_for("valid");
    invalid.query.clear();
    const harvester::app::SearchSnapshot invalid_snapshot = wait_for_completion(
        service,
        service.begin_search(invalid)
    );
    expect(
        invalid_snapshot.status == harvester::app::SearchStatus::Failed,
        "invalid native input must fail without a worker"
    );
    expect(
        invalid_snapshot.error == "Enter a query, choose one or more sources, and use a limit from 1 to 100.",
        "native input validation must stay user-facing"
    );

    const harvester::app::FullTextDiscoverySnapshot discovery = wait_for_discovery(
        service,
        service.begin_fulltext_discovery(
            {
                {
                    "work:1",
                    "Full text title",
                    "Ada",
                    "OpenAlex",
                    2026U,
                    1U,
                    "10.1000/example",
                    "https://repository.example/paper.pdf",
                },
            }
        )
    );
    expect(
        discovery.status == harvester::app::SearchStatus::Ready &&
            discovery.candidates.size() == 1U,
        "discovery must complete with a typed candidate"
    );
    expect(
        discovery.candidates[0].id != 0U &&
            discovery.candidates[0].source == "OpenAlex" &&
            discovery.candidates[0].direct_pdf,
        "the service must create an opaque candidate ID"
    );

    const harvester::app::FullTextDownloadSnapshot download = wait_for_download(
        service,
        service.begin_fulltext_download(discovery.candidates[0].id)
    );
    expect(
        download.status == harvester::app::SearchStatus::Ready &&
            download.local_filename == "paper-a1b2c3d4e5f6.pdf" &&
            download.byte_count == 42U,
        "a selected opaque candidate must download through the worker"
    );

    const harvester::app::FullTextDownloadSnapshot unknown_download = wait_for_download(
        service,
        service.begin_fulltext_download(999999U)
    );
    expect(
        unknown_download.status == harvester::app::SearchStatus::Failed &&
            unknown_download.error == "Unknown desktop full-text candidate.",
        "an unknown candidate must never start a worker with a caller URL"
    );
    return 0;
}
