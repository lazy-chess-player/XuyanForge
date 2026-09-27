#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::domain {

struct SourceChapter {
    std::string id;
    std::string title;
    int ordinal{0};
    std::size_t start_byte{0};
    std::size_t end_byte{0};
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
};

struct SourceDocument {
    std::string id;
    std::string world_id;
    std::string name;
    std::string sha256;
    std::string original_asset_ref;
    std::string normalized_asset_ref;
    std::string edition{"1"};
    std::string detected_encoding{"utf-8"};
    int chapter_revision{1};
    std::vector<SourceChapter> chapters;
};

/** @brief 校验 UTF-8、移除 BOM，并将 CRLF/CR 统一为 LF；无效序列返回错误。 */
Result<std::string> normalizeUtf8Text(std::string_view input);
/** @brief 扫描 Markdown 与中文章回标题，返回覆盖全文的章节字节/码点区间。 */
Result<std::vector<SourceChapter>> detectChapters(std::string_view normalized_utf8,
                                                  std::string_view document_id);
/** @brief 按 Unicode 码点半开区间截取 UTF-8 原文，越界时返回错误。 */
Result<std::string> codepointSlice(std::string_view utf8, std::size_t start, std::size_t end);
/** @brief 校验并计数 UTF-8 码点；非空无效文本返回零。 */
std::size_t utf8CodepointCount(std::string_view utf8);

} // namespace xuyan::domain
