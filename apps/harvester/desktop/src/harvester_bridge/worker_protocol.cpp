#include <harvester/app/worker_protocol.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace harvester::app {

namespace {

constexpr std::string_view protocol_header_v1{"HARVESTER_RESEARCH_V1"};
constexpr std::string_view protocol_header_v2{"HARVESTER_RESEARCH_V2"};
constexpr std::size_t maximum_output_bytes = 1024U * 1024U;
constexpr std::size_t maximum_field_bytes = 4096U;
constexpr std::size_t maximum_works = 300U;

SearchSnapshot invalid_response() {
    return {
        SearchStatus::Failed,
        {},
        "Python worker returned an invalid response.",
        {},
    };
}

std::optional<int> base64_value(const char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return std::nullopt;
}

std::optional<std::string> decode_base64(const std::string_view encoded) {
    if (encoded.size() > ((maximum_field_bytes + 2U) / 3U) * 4U ||
        encoded.size() % 4U != 0U) {
        return std::nullopt;
    }
    std::string decoded;
    decoded.reserve((encoded.size() / 4U) * 3U);
    for (std::size_t index = 0U; index < encoded.size(); index += 4U) {
        const std::optional<int> first = base64_value(encoded[index]);
        const std::optional<int> second = base64_value(encoded[index + 1U]);
        const bool third_padding = encoded[index + 2U] == '=';
        const bool fourth_padding = encoded[index + 3U] == '=';
        const std::optional<int> third =
            third_padding ? std::optional<int>{0} : base64_value(encoded[index + 2U]);
        const std::optional<int> fourth =
            fourth_padding ? std::optional<int>{0} : base64_value(encoded[index + 3U]);
        const bool final_block = index + 4U == encoded.size();
        if (!first.has_value() || !second.has_value() || !third.has_value() ||
            !fourth.has_value() || (third_padding && !fourth_padding) ||
            (third_padding && !final_block) || (fourth_padding && !final_block)) {
            return std::nullopt;
        }
        const uint32_t aggregate =
            (static_cast<uint32_t>(*first) << 18U) |
            (static_cast<uint32_t>(*second) << 12U) |
            (static_cast<uint32_t>(*third) << 6U) |
            static_cast<uint32_t>(*fourth);
        decoded.push_back(static_cast<char>((aggregate >> 16U) & 0xffU));
        if (!third_padding) {
            decoded.push_back(static_cast<char>((aggregate >> 8U) & 0xffU));
        }
        if (!fourth_padding) {
            decoded.push_back(static_cast<char>(aggregate & 0xffU));
        }
    }
    if (decoded.size() > maximum_field_bytes ||
        decoded.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    return decoded;
}

std::vector<std::string_view> split_tabs(const std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const std::size_t end = line.find('\t', start);
        if (end == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, end - start));
        start = end + 1U;
    }
    return fields;
}

bool parse_unsigned(const std::string_view text, uint32_t *const value) {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        *value
    );
    return error == std::errc{} && end == text.data() + text.size();
}

bool parse_unsigned64(const std::string_view text, uint64_t *const value) {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        *value
    );
    return error == std::errc{} && end == text.data() + text.size();
}

bool next_line(
    const std::string_view input,
    std::size_t *const cursor,
    std::string_view *const line
) {
    if (*cursor >= input.size()) {
        return false;
    }
    const std::size_t newline = input.find('\n', *cursor);
    if (newline == std::string_view::npos) {
        *line = input.substr(*cursor);
        *cursor = input.size();
    } else {
        *line = input.substr(*cursor, newline - *cursor);
        *cursor = newline + 1U;
    }
    return !line->empty() && line->back() != '\r';
}

bool valid_checksum(const std::string_view value) {
    return value.size() == 64U && std::all_of(
        value.begin(),
        value.end(),
        [](const unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        }
    );
}

bool valid_local_filename(const std::string_view value) {
    return !value.empty() && value.size() <= maximum_field_bytes &&
        value.find('/') == std::string_view::npos &&
        value.find('\\') == std::string_view::npos &&
        value.ends_with(".pdf");
}

}  // namespace

SearchSnapshot parse_python_worker_response(const std::string_view output) {
    if (output.empty() || output.size() > maximum_output_bytes) {
        return invalid_response();
    }

    std::size_t cursor = 0U;
    std::string_view line;
    if (!next_line(output, &cursor, &line)) {
        return invalid_response();
    }
    const std::vector<std::string_view> first_fields = split_tabs(line);
    if (first_fields.size() == 2U && first_fields[0] == "ERROR" &&
        cursor == output.size()) {
        const std::optional<std::string> message = decode_base64(first_fields[1]);
        if (!message.has_value() || message->empty()) {
            return invalid_response();
        }
        return {SearchStatus::Failed, {}, *message, {}};
    }
    const bool version_two = line == protocol_header_v2;
    if (!version_two && line != protocol_header_v1) {
        return invalid_response();
    }

    std::vector<WorkSummary> works;
    std::string notice;
    while (next_line(output, &cursor, &line)) {
        if (line == "END") {
            if (cursor != output.size()) {
                return invalid_response();
            }
            return {SearchStatus::Ready, std::move(works), {}, std::move(notice)};
        }
        const std::vector<std::string_view> fields = split_tabs(line);
        if (fields[0] == "NOTICE") {
            if (fields.size() != 2U || !works.empty() || !notice.empty()) {
                return invalid_response();
            }
            const std::optional<std::string> parsed_notice = decode_base64(fields[1]);
            if (!parsed_notice.has_value() || parsed_notice->empty()) {
                return invalid_response();
            }
            notice = *parsed_notice;
            continue;
        }
        const std::size_t expected_work_fields = version_two ? 9U : 7U;
        if (fields.size() != expected_work_fields || fields[0] != "WORK" ||
            works.size() == maximum_works) {
            return invalid_response();
        }
        const std::optional<std::string> reference = decode_base64(fields[1]);
        const std::optional<std::string> title = decode_base64(fields[2]);
        const std::optional<std::string> authors = decode_base64(fields[3]);
        const std::optional<std::string> source = decode_base64(fields[4]);
        const std::optional<std::string> doi = version_two
            ? decode_base64(fields[7]) : std::optional<std::string>{std::string{}};
        const std::optional<std::string> open_access_url = version_two
            ? decode_base64(fields[8]) : std::optional<std::string>{std::string{}};
        uint32_t year = 0U;
        uint32_t citation_count = 0U;
        if (!reference.has_value() || !title.has_value() || !authors.has_value() ||
            !source.has_value() || !doi.has_value() || !open_access_url.has_value() ||
            title->empty() || source->empty() ||
            !parse_unsigned(fields[5], &year) ||
            !parse_unsigned(fields[6], &citation_count)) {
            return invalid_response();
        }
        works.push_back(
            {
                *reference,
                *title,
                *authors,
                *source,
                year,
                citation_count,
                *doi,
                *open_access_url,
            }
        );
    }
    return invalid_response();
}

WorkerFullTextDiscoveryResponse parse_python_fulltext_discovery_response(
    const std::string_view output
) {
    const auto invalid = []() {
        return WorkerFullTextDiscoveryResponse{
            SearchStatus::Failed,
            {},
            "Python worker returned an invalid response.",
            {},
        };
    };
    if (output.empty() || output.size() > maximum_output_bytes) {
        return invalid();
    }

    std::size_t cursor = 0U;
    std::string_view line;
    if (!next_line(output, &cursor, &line)) {
        return invalid();
    }
    const std::vector<std::string_view> first_fields = split_tabs(line);
    if (first_fields.size() == 2U && first_fields[0] == "ERROR" &&
        cursor == output.size()) {
        const std::optional<std::string> message = decode_base64(first_fields[1]);
        if (!message.has_value() || message->empty()) {
            return invalid();
        }
        return {SearchStatus::Failed, {}, *message, {}};
    }
    if (line != protocol_header_v2) {
        return invalid();
    }

    std::vector<WorkerFullTextCandidate> candidates;
    std::string notice;
    while (next_line(output, &cursor, &line)) {
        if (line == "END") {
            if (cursor != output.size()) {
                return invalid();
            }
            return {SearchStatus::Ready, std::move(candidates), {}, std::move(notice)};
        }
        const std::vector<std::string_view> fields = split_tabs(line);
        if (fields[0] == "NOTICE") {
            if (fields.size() != 2U || !candidates.empty() || !notice.empty()) {
                return invalid();
            }
            const std::optional<std::string> parsed_notice = decode_base64(fields[1]);
            if (!parsed_notice.has_value() || parsed_notice->empty()) {
                return invalid();
            }
            notice = *parsed_notice;
            continue;
        }
        if (fields.size() != 8U || fields[0] != "CANDIDATE" ||
            candidates.size() == maximum_works ||
            (fields[7] != "0" && fields[7] != "1")) {
            return invalid();
        }
        const std::optional<std::string> remote_url = decode_base64(fields[1]);
        const std::optional<std::string> source = decode_base64(fields[2]);
        const std::optional<std::string> repository = decode_base64(fields[3]);
        const std::optional<std::string> license = decode_base64(fields[4]);
        const std::optional<std::string> version = decode_base64(fields[5]);
        const std::optional<std::string> host_type = decode_base64(fields[6]);
        if (!remote_url.has_value() || !source.has_value() ||
            !repository.has_value() || !license.has_value() ||
            !version.has_value() || !host_type.has_value() || remote_url->empty() ||
            source->empty()) {
            return invalid();
        }
        candidates.push_back(
            {
                *remote_url,
                *source,
                *repository,
                *license,
                *version,
                *host_type,
                fields[7] == "1",
            }
        );
    }
    return invalid();
}

WorkerFullTextDownloadResponse parse_python_fulltext_download_response(
    const std::string_view output
) {
    const auto invalid = []() {
        return WorkerFullTextDownloadResponse{
            SearchStatus::Failed,
            {},
            0U,
            {},
            "Python worker returned an invalid response.",
        };
    };
    if (output.empty() || output.size() > maximum_output_bytes) {
        return invalid();
    }

    std::size_t cursor = 0U;
    std::string_view line;
    if (!next_line(output, &cursor, &line)) {
        return invalid();
    }
    const std::vector<std::string_view> first_fields = split_tabs(line);
    if (first_fields.size() == 2U && first_fields[0] == "ERROR" &&
        cursor == output.size()) {
        const std::optional<std::string> message = decode_base64(first_fields[1]);
        if (!message.has_value() || message->empty()) {
            return invalid();
        }
        return {SearchStatus::Failed, {}, 0U, {}, *message};
    }
    if (line != protocol_header_v2 || !next_line(output, &cursor, &line)) {
        return invalid();
    }
    const std::vector<std::string_view> fields = split_tabs(line);
    if (fields.size() != 4U || fields[0] != "DOWNLOAD") {
        return invalid();
    }
    const std::optional<std::string> filename = decode_base64(fields[1]);
    uint64_t byte_count = 0U;
    if (!filename.has_value() || !valid_local_filename(*filename) ||
        !parse_unsigned64(fields[2], &byte_count) || !valid_checksum(fields[3]) ||
        !next_line(output, &cursor, &line) || line != "END" ||
        cursor != output.size()) {
        return invalid();
    }
    return {SearchStatus::Ready, *filename, byte_count, std::string{fields[3]}, {}};
}

}  // namespace harvester::app
