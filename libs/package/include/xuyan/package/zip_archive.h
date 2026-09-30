#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::package {

/* 受限 ZIP 的拥有型条目；读取仅返回内存值，不解包到文件系统。
 * 每条目是一份相对路径和原始字节，读写接口拒绝以斜线结尾的目录占位，读取拒绝标记的符号链接；
 * 字符串由对象拥有至销毁，调用线程使用，同一对象并发修改需同步；调用方负责正文隐私和路径 UTF-8 编码。 */
struct ZipEntry {
    /* 包内相对路径字节，预期 UTF-8，默认空（无效）；调用方/读取器设置，写入器/调用方读取；只校验词法，不校验编码。 */
    std::string path;
    /* 条目原始字节，默认空且合法；调用方/读取器设置，写入器/调用方读取；对象独占，受读取限额约束，写入不压缩。 */
    std::string data;
};

/* 一次只读归档操作的资源限额值；调用方冻结后传入，无线程绑定，不自动提高限额。
 * 所有限额允许 0：条目数为 0 只允许空包，正文上限为 0 仍允许空文件，归档字节上限为 0 无法容纳合法 ZIP。
 * 调用方设置，各读取阶段只读，字段随限额对象存在；字节限额不是 Unicode 字符数量。 */
struct ZipLimits {
    /* 最大文件条目数，默认 1000；核对中央目录声明数量时使用，目录占位也不例外。 */
    std::size_t maximum_entries{1000};
    /* 每条目原始字节上限，默认 32 MiB；复制正文前检查。 */
    std::size_t maximum_entry_bytes{32 * 1024 * 1024};
    /* 所有返回条目正文累计字节上限，默认 128 MiB，不包括路径和容器开销。 */
    std::size_t maximum_total_bytes{128 * 1024 * 1024};
    /* 整个归档文件字节上限，默认 128 MiB；整文件读入前检查，峰值还包括条目副本。 */
    std::size_t maximum_archive_bytes{128 * 1024 * 1024};
};

/* 功能：生成不压缩、UTF-8 文件名的经典 ZIP，先写临时文件，再替换目标。
 * 参数：destination 为输出文件路径，不能为空，父目录按需创建；
 *       entries 为调用内借用的条目，可空，最多 65535 项，路径须符合 safePackagePath 且按字节唯一，
 *       路径 UTF-8 由调用方保证，大小/偏移及整包总字节数须能以 32 位表示。
 * 返回：成功为输出路径字符串；失败为中文校验错误。
 * 失败：路径/重复/格式容量、I/O 或内存标准异常转为错误结果；错误值自身分配异常仍可传播。
 *       临时路径已存在时拒绝覆盖，替换失败保留原目标；替换成功后返回路径字符串转换/分配若失败，目标已经更新。
 * 副作用：分配整包内存、创建父目录、写 destination 加 .tmp 后缀的文件并替换目标；
 *       仅尝试清理本次创建的临时文件，清理失败可能残留，已创建的父目录不回滚。
 * 线程：同步执行，同一目标由调用方串行化；无压缩/加密，不包含自动样例，不记录条目正文。 */
xuyan::domain::Result<std::string> writeZip(const std::filesystem::path& destination,
                                            const std::vector<ZipEntry>& entries);
/* 功能：按限额读取并核对不压缩 ZIP 的目录、本地头、路径和 CRC。
 * 参数：source 为现有归档路径，借用至返回；limits 为本次固定限额，默认值见 ZipLimits。
 * 返回：成功为按中央目录顺序排列的拥有型条目列表，合法空包返回空列表；失败不返回部分结果；路径不验证 UTF-8。
 * 失败：超限、截断、加密/跨盘/压缩/符号链接、不安全或按字节重复的路径、头部不一致/CRC 错误，
 *       以及 I/O/内存标准异常转为错误结果；错误值自身分配异常仍可传播。
 * 副作用：只读归档、分配整包及条目副本，不创建文件或提取正文到磁盘。
 * 线程：调用线程同步执行；调用期间不得修改归档，返回值与源文件生命周期无关。 */
xuyan::domain::Result<std::vector<ZipEntry>> readZip(const std::filesystem::path& source,
                                                     const ZipLimits& limits = {});
/* 功能：计算 ZIP 的 IEEE CRC-32，不用于密码学鉴别。
 * 参数：bytes 为调用内有效的原始字节视图，可空。返回：32 位校验值，空输入为 0。
 * 失败：无。副作用：只读内存，不缓存、不改变线程状态。 */
std::uint32_t crc32(std::string_view bytes);
/* 功能：检查包内相对文件路径的词法安全边界。
 * 参数：path 为调用内有效视图；1—1024 字节，只用 '/' 分段，禁止绝对路径、反斜杠、冒号、零字节及空/点段。
 * 返回：满足边界为真。失败：不满足时为假，无异常。
 * 副作用：只读，不访问文件系统，不校验 UTF-8、控制字符或平台保留文件名；
 *       不保证外部解包目录无符号链接，解包方仍须校验实际目标。 */
bool safePackagePath(std::string_view path);

} // namespace xuyan::package
