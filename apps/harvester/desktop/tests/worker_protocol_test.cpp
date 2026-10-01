#include <cstdlib>
#include <iostream>
#include <string_view>

#include <harvester/app/worker_protocol.hpp>

namespace {

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "worker_protocol_test: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    const harvester::app::SearchSnapshot successful =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V1\n"
            "WORK\td29yazox\tVGl0bGU=\tQWRh\tT3BlbkFsZXg=\t2026\t9\n"
            "END\n"
        );
    expect(
        successful.status == harvester::app::SearchStatus::Ready,
        "valid worker output must be ready"
    );
    expect(successful.works.size() == 1U, "one work must be decoded");
    expect(successful.works[0].reference == "work:1", "reference must decode");
    expect(successful.works[0].title == "Title", "title must decode");
    expect(successful.works[0].authors == "Ada", "authors must decode");
    expect(successful.works[0].source == "OpenAlex", "source must decode");
    expect(successful.works[0].year == 2026U, "year must decode");
    expect(successful.works[0].citation_count == 9U, "citations must decode");

    const harvester::app::SearchSnapshot v2_successful =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V2\n"
            "WORK\td29yazox\tVGl0bGU=\tQWRh\tT3BlbkFsZXg=\t2026\t9\tMTAuMTAwMC9leGFtcGxl\taHR0cHM6Ly9yZXBvc2l0b3J5LmV4YW1wbGUvcGFwZXIucGRm\n"
            "END\n"
        );
    expect(
        v2_successful.status == harvester::app::SearchStatus::Ready,
        "v2 worker output must be ready"
    );
    expect(
        v2_successful.works[0].doi == "10.1000/example",
        "v2 DOI metadata must decode"
    );
    expect(
        v2_successful.works[0].open_access_url ==
            "https://repository.example/paper.pdf",
        "v2 open-access URL metadata must decode"
    );

    const harvester::app::SearchSnapshot optional_empty_fields =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V1\n"
            "WORK\t\tVGl0bGU=\t\tT3BlbkFsZXg=\t0\t0\n"
            "END\n"
        );
    expect(
        optional_empty_fields.status == harvester::app::SearchStatus::Ready,
        "an absent source identifier or author list must remain valid"
    );

    const harvester::app::SearchSnapshot partial =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V1\n"
            "NOTICE\tQ3Jvc3NyZWYgZGlkIG5vdCByZXNwb25kLg==\n"
            "WORK\td29yazox\tVGl0bGU=\tQWRh\tT3BlbkFsZXg=\t2026\t9\n"
            "END\n"
        );
    expect(
        partial.status == harvester::app::SearchStatus::Ready,
        "partial source responses must remain ready"
    );
    expect(
        partial.notice == "Crossref did not respond.",
        "a validated partial-result notice must be decoded"
    );

    const harvester::app::SearchSnapshot failure =
        harvester::app::parse_python_worker_response(
            "ERROR\tU2VhcmNoIHJlcXVlc3QgZmFpbGVkLg==\n"
        );
    expect(
        failure.status == harvester::app::SearchStatus::Failed,
        "worker error must be surfaced as failed"
    );
    expect(
        failure.error == "Search request failed.",
        "worker error must decode"
    );

    const harvester::app::SearchSnapshot malformed =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V1\n"
            "WORK\td29yazox\tVGl0bGU=\tQWRh\tT3BlbkFsZXg=\t2026\t9\n"
        );
    expect(
        malformed.status == harvester::app::SearchStatus::Failed,
        "unfinished output must fail"
    );
    expect(
        malformed.error == "Python worker returned an invalid response.",
        "malformed output must not expose raw worker content"
    );

    const harvester::app::SearchSnapshot nul_field =
        harvester::app::parse_python_worker_response(
            "HARVESTER_RESEARCH_V1\n"
            "WORK\td29yazox\tAA==\tQWRh\tT3BlbkFsZXg=\t2026\t9\n"
            "END\n"
        );
    expect(
        nul_field.status == harvester::app::SearchStatus::Failed,
        "embedded NUL must be rejected"
    );

    const harvester::app::WorkerFullTextDiscoveryResponse discovery =
        harvester::app::parse_python_fulltext_discovery_response(
            "HARVESTER_RESEARCH_V2\n"
            "NOTICE\tT3BlbkFsZXggZGlkIG5vdCByZXNwb25kLg==\n"
            "CANDIDATE\taHR0cHM6Ly9yZXBvc2l0b3J5LmV4YW1wbGUvcGFwZXIucGRm\tT3BlbkFsZXg=\tRXhhbXBsZSBSZXBvc2l0b3J5\tQ0MtQlktNC4w\tYWNjZXB0ZWRWZXJzaW9u\tcmVwb3NpdG9yeQ==\t1\n"
            "END\n"
        );
    expect(
        discovery.status == harvester::app::SearchStatus::Ready,
        "valid full-text discovery output must be ready"
    );
    expect(discovery.candidates.size() == 1U, "one candidate must decode");
    expect(
        discovery.candidates[0].remote_url ==
            "https://repository.example/paper.pdf",
        "the service-only URL must decode"
    );
    expect(
        discovery.candidates[0].direct_pdf,
        "direct-PDF flag must decode"
    );

    const harvester::app::WorkerFullTextDownloadResponse download =
        harvester::app::parse_python_fulltext_download_response(
            "HARVESTER_RESEARCH_V2\n"
            "DOWNLOAD\tcGFwZXItYTFiMmMzZDRlNWY2LnBkZg==\t42\t"
            "a1b2c3d4e5f60718293a4b5c6d7e8f90123456789abcdef0123456789abcdef0\n"
            "END\n"
        );
    expect(
        download.status == harvester::app::SearchStatus::Ready,
        "valid full-text download output must be ready"
    );
    expect(
        download.local_filename == "paper-a1b2c3d4e5f6.pdf" &&
            download.byte_count == 42U,
        "download metadata must decode"
    );
    expect(
        harvester::app::parse_python_fulltext_discovery_response(
            "HARVESTER_RESEARCH_V1\nEND\n"
        ).status == harvester::app::SearchStatus::Failed,
        "full-text operations must reject a v1 response"
    );
    return 0;
}
