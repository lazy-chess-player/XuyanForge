#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>

namespace xuyan::application {

struct DecodedSourceText {
    std::string normalized_utf8;
    std::string encoding;
};

xuyan::domain::Result<DecodedSourceText> decodeSourceText(std::string_view bytes);

} // namespace xuyan::application
