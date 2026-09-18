#pragma once

#include <string>
#include <string_view>

namespace xuyan::domain {

std::string sha256(std::string_view bytes);

} // namespace xuyan::domain

