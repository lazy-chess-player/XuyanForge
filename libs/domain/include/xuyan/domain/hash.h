#pragma once

#include <string>
#include <string_view>

namespace xuyan::domain {

/** @brief 计算任意字节序列的 SHA-256 十六进制摘要。 */
std::string sha256(std::string_view bytes);

} // namespace xuyan::domain
