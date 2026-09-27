#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>

namespace xuyan::application {

struct DecodedSourceText {
    std::string normalized_utf8;
    std::string encoding;
};

/** @brief 识别 UTF-8、带 BOM 的 UTF-16，并在 Windows 上回退 GB18030，统一输出标准化 UTF-8。 */
xuyan::domain::Result<DecodedSourceText> decodeSourceText(std::string_view bytes);

} // namespace xuyan::application
