#include "xuyan/domain/source_document.h"

#include <algorithm>
#include <cctype>

namespace xuyan::domain {
namespace {

std::size_t sequenceLength(unsigned char first) {
    if (first <= 0x7f) return 1;
    if ((first & 0xe0) == 0xc0) return 2;
    if ((first & 0xf0) == 0xe0) return 3;
    if ((first & 0xf8) == 0xf0) return 4;
    return 0;
}

bool validateSequence(std::string_view text, std::size_t offset, std::size_t length) {
    if (length == 0 || offset + length > text.size()) return false;
    const auto first = static_cast<unsigned char>(text[offset]);
    std::uint32_t codepoint = length == 1 ? first : first & ((1u << (7 - length)) - 1);
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xc0) != 0x80) return false;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800)
        || (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff
        || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    return true;
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return std::string{text};
}

bool isHeading(std::string_view line, std::string& title) {
    auto clean = trim(line);
    if (clean.empty()) return false;
    std::size_t hashes = 0;
    while (hashes < clean.size() && clean[hashes] == '#' && hashes < 6) ++hashes;
    if (hashes > 0 && hashes < clean.size() && clean[hashes] == ' ') {
        title = trim(std::string_view(clean).substr(hashes + 1));
        return !title.empty();
    }
    constexpr std::string_view prefix = "第";
    if (clean.rfind(prefix, 0) == 0 && clean.size() <= 96
        && (clean.find("章") != std::string::npos || clean.find("回") != std::string::npos
            || clean.find("节") != std::string::npos || clean.find("卷") != std::string::npos)) {
        title = clean;
        return true;
    }
    return false;
}

std::size_t byteAtCodepoint(std::string_view text, std::size_t target) {
    std::size_t byte = 0;
    std::size_t codepoint = 0;
    while (byte < text.size() && codepoint < target) {
        byte += sequenceLength(static_cast<unsigned char>(text[byte]));
        ++codepoint;
    }
    return byte;
}

} // namespace

Result<std::string> normalizeUtf8Text(std::string_view input) {
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xef
        && static_cast<unsigned char>(input[1]) == 0xbb && static_cast<unsigned char>(input[2]) == 0xbf) {
        input.remove_prefix(3);
    }
    std::string output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const auto first = static_cast<unsigned char>(input[index]);
        if (first == '\r') {
            if (index + 1 < input.size() && input[index + 1] == '\n') ++index;
            output.push_back('\n');
            ++index;
            continue;
        }
        const auto length = sequenceLength(first);
        if (!validateSequence(input, index, length)) {
            return Result<std::string>::failure(
                {ErrorCode::validation_failed, "来源文件不是合法 UTF-8，或字符在文件末尾被截断", false,
                 "将文件转换为 UTF-8 后重新导入"});
        }
        output.append(input.substr(index, length));
        index += length;
    }
    return Result<std::string>::success(std::move(output));
}

std::size_t utf8CodepointCount(std::string_view utf8) {
    std::size_t count = 0;
    for (std::size_t byte = 0; byte < utf8.size();) {
        const auto length = sequenceLength(static_cast<unsigned char>(utf8[byte]));
        if (!validateSequence(utf8, byte, length)) return 0;
        byte += length;
        ++count;
    }
    return count;
}

Result<std::vector<SourceChapter>> detectChapters(std::string_view text, std::string_view document_id) {
    std::vector<std::pair<std::size_t, std::string>> headings;
    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        const auto newline = text.find('\n', line_start);
        const auto line_end = newline == std::string_view::npos ? text.size() : newline;
        std::string title;
        if (isHeading(text.substr(line_start, line_end - line_start), title)) {
            headings.emplace_back(line_start, std::move(title));
        }
        if (newline == std::string_view::npos) break;
        line_start = newline + 1;
    }

    if (headings.empty()) headings.emplace_back(0, "正文");
    else if (headings.front().first != 0) headings.insert(headings.begin(), {0, "序章"});
    std::vector<SourceChapter> chapters;
    chapters.reserve(headings.size());
    std::size_t cumulative_codepoints = 0;
    std::size_t previous_byte = 0;
    for (std::size_t index = 0; index < headings.size(); ++index) {
        const auto start = headings[index].first;
        cumulative_codepoints += utf8CodepointCount(text.substr(previous_byte, start - previous_byte));
        const auto end = index + 1 < headings.size() ? headings[index + 1].first : text.size();
        SourceChapter chapter;
        chapter.id = std::string(document_id) + "-chapter-" + std::to_string(index + 1);
        chapter.title = headings[index].second;
        chapter.ordinal = static_cast<int>(index + 1);
        chapter.start_byte = start;
        chapter.end_byte = end;
        chapter.start_codepoint = cumulative_codepoints;
        chapter.end_codepoint = cumulative_codepoints + utf8CodepointCount(text.substr(start, end - start));
        chapters.push_back(std::move(chapter));
        previous_byte = start;
    }
    return Result<std::vector<SourceChapter>>::success(std::move(chapters));
}

Result<std::string> codepointSlice(std::string_view utf8, std::size_t start, std::size_t end) {
    const auto count = utf8CodepointCount(utf8);
    if (end < start || end > count) {
        return Result<std::string>::failure(
            {ErrorCode::validation_failed, "证据码点区间超出标准化文本", false, "重新定位来源证据"});
    }
    const auto begin_byte = byteAtCodepoint(utf8, start);
    const auto end_byte = byteAtCodepoint(utf8, end);
    return Result<std::string>::success(std::string{utf8.substr(begin_byte, end_byte - begin_byte)});
}

} // namespace xuyan::domain

