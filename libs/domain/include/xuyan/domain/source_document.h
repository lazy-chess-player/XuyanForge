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

Result<std::string> normalizeUtf8Text(std::string_view input);
Result<std::vector<SourceChapter>> detectChapters(std::string_view normalized_utf8,
                                                  std::string_view document_id);
Result<std::string> codepointSlice(std::string_view utf8, std::size_t start, std::size_t end);
std::size_t utf8CodepointCount(std::string_view utf8);

} // namespace xuyan::domain
