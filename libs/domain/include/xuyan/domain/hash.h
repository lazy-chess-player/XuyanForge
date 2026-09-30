#pragma once

#include <string>
#include <string_view>

namespace xuyan::domain {

/*
 * 功能：计算任意字节序列的 SHA-256 十六进制摘要。
 * 参数：
 *   bytes：借用任意字节序列，允许零字节，不要求UTF-8。
 * 返回：64字符小写十六进制SHA-256摘要，空输入同样具有确定摘要。
 * 失败：缓冲分配失败可传播标准异常，无错误结果容器。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
std::string sha256(std::string_view bytes);

} // namespace xuyan::domain
