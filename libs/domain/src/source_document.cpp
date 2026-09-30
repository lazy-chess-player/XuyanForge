#include "xuyan/domain/source_document.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace xuyan::domain {
namespace {

/*
 * 功能：按首字节位模式识别UTF-8候选序列长度。
 * 参数：
 *   first：输入首字节的无符号值，不是已解码Unicode码点。
 * 返回：候选宽度1—4字节；不符合首字节模式为0，仍须validateSequence校验。
 * 失败：约束内的纯计算不产生业务异常；调用者须遵守参数前置条件。
 * 副作用：只进行整数计算。
 */
std::size_t sequenceLength(unsigned char first) {
    if (first <= 0x7f) return 1;
    if ((first & 0xe0) == 0xc0) return 2;
    if ((first & 0xf0) == 0xe0) return 3;
    if ((first & 0xf8) == 0xf0) return 4;
    return 0;
}

/*
 * 功能：检查一个UTF-8序列的边界、续字节、最短编码和Unicode范围。
 * 参数：
 *   text：借用完整字节序列。
 *   offset：从零开始的字节偏移，非码点位置。
 *   length：候选字节宽度，调用者由sequenceLength取得0—4。
 * 返回：序列合法为true，截断/代理项/越界/非最短编码为false。
 * 失败：约束内的纯计算不产生业务异常；调用者须遵守参数前置条件。
 * 副作用：只读文本，不做编码修复。
 */
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

/*
 * 功能：复制标题文本，去除两端ASCII空格及制表符，不删除其他字符。
 * 参数：
 *   text：调用期间借用的字节文本。
 * 返回：独立拥有的去边界空白字符串，原文不改。
 * 失败：字符串或容器分配可抛标准异常。
 * 副作用：仅分配返回字符串。
 */
std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return std::string{text};
}

/*
 * 功能：识别1—6级Markdown或以中文序数开头的短章回标题。
 * 参数：
 *   line：调用期间借用的一行原文，不含行分隔符。
 *   title：调用者拥有的输出字符串；命中写标题，未命中不保证清空旧值。
 * 返回：命中有效非空标题为true，否则false；中文标题最多96字节，此启发式不证明正文结构。
 * 失败：字符串或容器分配可抛标准异常。
 * 副作用：可能改写title，不修改原文。
 */
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

/*
 * 功能：从合法UTF-8文本起点线性定位一个码点位置。
 * 参数：
 *   text：已验证合法的UTF-8文本；非法输入可能使扫描不前进，调用者必须先验证。
 *   target：零基码点偏移，须不超文本码点总数；末端返回字节总长。
 * 返回：目标码点对应的零基字节起点。
 * 失败：约束内的纯计算不产生业务异常；调用者须遵守参数前置条件。
 * 副作用：只读扫描，不复制正文。
 */
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
    // 计数接口的0同时表示空串和非法编码；先区分，避免生成看似合法的零码点索引。
    if (!text.empty() && utf8CodepointCount(text) == 0)
        return Result<std::vector<SourceChapter>>::failure(
            {ErrorCode::validation_failed, "章节检测输入不是合法的统一编码文本", false, "先解码并规范化来源正文"});
    // 每项拥有标题副本和正文中的零基字节起点，不复制章节正文。
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

    // 没有标题时整篇算一章；标题前的正文单独保留为序章，保证索引覆盖全文。
    if (headings.empty()) headings.emplace_back(0, "正文");
    else if (headings.front().first != 0) headings.insert(headings.begin(), {0, "序章"});
    std::vector<SourceChapter> chapters;
    chapters.reserve(headings.size());
    std::size_t cumulative_codepoints = 0;
    // 每个章节正文只计数一次，累加形成双锚点，避免重复扫描上一章节。
    for (std::size_t index = 0; index < headings.size(); ++index) {
        const auto start = headings[index].first;
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
        cumulative_codepoints = chapters.back().end_codepoint;
    }
    return Result<std::vector<SourceChapter>>::success(std::move(chapters));
}

Result<std::string> codepointSlice(std::string_view utf8, std::size_t start, std::size_t end) {
    const auto count = utf8CodepointCount(utf8);
    if (!utf8.empty() && count == 0)
        return Result<std::string>::failure(
            {ErrorCode::validation_failed, "原文切片输入编码无效", false, "先规范化来源正文"});
    if (end < start || end > count) {
        return Result<std::string>::failure(
            {ErrorCode::validation_failed, "证据码点区间超出标准化文本", false, "重新定位来源证据"});
    }
    const auto begin_byte = byteAtCodepoint(utf8, start);
    const auto end_byte = byteAtCodepoint(utf8, end);
    return Result<std::string>::success(std::string{utf8.substr(begin_byte, end_byte - begin_byte)});
}

} // namespace xuyan::domain
