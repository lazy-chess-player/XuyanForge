#include "xuyan/package/zip_archive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace xuyan::package {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

/* 功能：包装包操作错误。参数：message 为按值接收的中文说明，不含正文。
 * 返回：不可自动重试的校验错误。失败：分配异常传播。副作用：只分配错误对象，无 I/O。 */
Error packageError(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "检查包文件后重试"};
}

/* 功能：追加经典 ZIP 的小端字段。参数：output 为拥有型输出引用；value 为 16 位无符号数。
 * 返回：无。失败：分配异常传播。副作用：追加两字节，可能使旧视图失效；不保存引用。 */
void put16(std::string& output, std::uint16_t value) {
    output.push_back(static_cast<char>(value & 0xff));
    output.push_back(static_cast<char>((value >> 8) & 0xff));
}

/* 功能：追加 32 位 ZIP 字段。参数：output 为输出引用；value 为大小/CRC/签名/偏移值。
 * 返回：无。失败：分配异常传播。副作用：追加四个小端字节，不执行 I/O。 */
void put32(std::string& output, std::uint32_t value) {
    put16(output, static_cast<std::uint16_t>(value & 0xffff));
    put16(output, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

/* 功能：读取小端 16 位字段。参数：input 为调用内借用字节；offset 为从 0 起的字节偏移。
 * 返回：字段值。失败：剩余不足两字节抛中文 runtime_error。
 * 副作用：只读，不移动外部游标、不保存视图；减法检查防止偏移回绕。 */
std::uint16_t get16(std::string_view input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 2) throw std::runtime_error("ZIP 字段被截断");
    return static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset]))
         | static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset + 1]) << 8);
}

/* 功能：读取小端 32 位字段。参数：input 为调用内借用字节；offset 为零起始字节偏移。
 * 返回：字段值。失败：剩余不足四字节抛中文 runtime_error。
 * 副作用：只读；先核对全部四字节再计算第二个半字偏移，不保留输入。 */
std::uint32_t get32(std::string_view input, std::size_t offset) {
    if (offset > input.size() || input.size() - offset < 4) throw std::runtime_error("ZIP 字段被截断");
    return static_cast<std::uint32_t>(get16(input, offset))
         | (static_cast<std::uint32_t>(get16(input, offset + 2)) << 16);
}

/* 功能：在分配之前核对归档大小并读取整文件。
 * 参数：source 为借用路径；maximum 为允许文件字节数，0 仅容许空文件。
 * 返回：独立拥有的字节串。失败：大小读取/打开/短读或容量超限抛中文异常，分配异常传播。
 * 副作用：同步只读文件，流在返回/异常时关闭；文件不得并发改写，不复制到其他路径。 */
std::string readBounded(const std::filesystem::path& source, std::size_t maximum) {
    std::error_code error;
    const auto size = std::filesystem::file_size(source, error);
    if (error) throw std::runtime_error("无法读取 ZIP 大小");
    if (size > maximum || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("ZIP 文件超过大小上限");
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("无法打开 ZIP 文件");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("读取 ZIP 文件失败");
    return bytes;
}

/* 本次独占创建的临时文件清理守卫，只在 writeStaged 内使用，不复制、不跨线程。
 * 未创建或已经替换目标时不清理，避免删除其他调用预先存在的临时文件。 */
struct TemporaryArchive {
    /* 临时文件的原生路径；先于创建取得，生命周期覆盖全部 I/O。 */
    std::filesystem::path path;
    /* 是否由本次成功创建且尚未提交；默认否，写入函数设置，析构读取。 */
    bool owned{false};
    /* 功能：绑定尚未创建的暂存路径。参数：temporary 为准确文件路径，按值接收并移入。
     * 返回：完成初始化，尚无文件所有权。失败：实参复制可能分配失败。
     * 副作用：不创建/删除文件；路径独占保存，仅在本次调用线程使用。 */
    explicit TemporaryArchive(std::filesystem::path temporary) : path(std::move(temporary)) {}
    /* 功能：禁止复制清理职责。参数：另一守卫只用于匹配禁用签名。
     * 返回/失败：无运行期返回，调用在编译期被拒绝。副作用：不会共享或转移临时文件所有权。 */
    TemporaryArchive(const TemporaryArchive&) = delete;
    /* 功能：禁止以赋值复制清理职责。参数：另一守卫只用于匹配禁用签名。
     * 返回/失败：无运行期返回，调用在编译期被拒绝。副作用：不会覆盖现有路径或清理责任。 */
    TemporaryArchive& operator=(const TemporaryArchive&) = delete;
    /* 功能：回收本次失败留下的临时文件。参数：无。返回：释放资源，无返回值。
     * 失败：清理错误不抛异常，可能残留临时文件供人工处理。
     * 副作用：仅在 owned 为真时删除准确 path；文件句柄须先关闭，在调用线程执行。 */
    ~TemporaryArchive() noexcept {
        if (!owned) return;
        try {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        } catch (...) {
            // 析构不承担恢复操作；分配失败时保留暂存文件，不覆盖正在传播的原始异常。
        }
    }
};

/* 独占暂存文件的原生句柄，局部作用域使用，不复制；异常时先关闭句柄，再由路径守卫清理文件。 */
struct ArchiveFile {
    /* 功能：建立空句柄守卫。参数：无。返回：完成初始化，未打开文件。
     * 失败：无。副作用：无 I/O，调用线程中持有资源直到析构。 */
    ArchiveFile() = default;
    /* 功能：禁止复制原生句柄所有权。参数：另一守卫仅用于禁用签名。
     * 返回/失败：无运行期返回，编译期拒绝。副作用：避免两个析构器关闭同一句柄。 */
    ArchiveFile(const ArchiveFile&) = delete;
    /* 功能：禁止覆盖拥有的句柄。参数：另一守卫仅用于禁用签名。
     * 返回/失败：无运行期返回，编译期拒绝。副作用：不丢失或重复释放资源。 */
    ArchiveFile& operator=(const ArchiveFile&) = delete;
#ifdef _WIN32
    /* CREATE_NEW 成功返回的拥有型句柄，初始 INVALID_HANDLE_VALUE；只在本次写入期间有效。 */
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    /* O_EXCL 成功返回的拥有型文件描述符，初始 -1；只在本次写入期间有效。 */
    int handle{-1};
#endif
    /* 功能：关闭本次暂存句柄。参数：无。返回：释放资源，无返回值。
     * 失败：析构不抛异常；写入/刷新错误已由 writeStaged 报告。
     * 副作用：关闭句柄，不删除路径，调用线程执行，不允许复制拥有者。 */
    ~ArchiveFile() noexcept {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
#else
        if (handle >= 0) ::close(handle);
#endif
    }
};

/* 功能：独占创建相邻暂存文件，完整落盘后原子替换目标，避免先删除原包。
 * 参数：destination 为准确输出路径；bytes 为调用期间有效的完整包字节视图。
 * 返回：无。失败：已存在暂存文件、创建/写入/刷新/替换失败抛中文 runtime_error。
 * 副作用：创建 destination+.tmp，仅删除本次创建的临时文件；失败不删除旧目标，父目录由外层创建。
 * 线程：同步 I/O，同一目标须串行；独占创建阻止覆盖其他调用的临时文件或预植链接。 */
void writeStaged(const std::filesystem::path& destination, std::string_view bytes) {
    auto temporary = destination;
    temporary += ".tmp";
    TemporaryArchive cleanup{temporary};
    {
        ArchiveFile file;
#ifdef _WIN32
        file.handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("无法独占创建 ZIP 临时文件，请检查已有临时文件和目录权限");
#else
        file.handle = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (file.handle < 0) throw std::runtime_error("无法独占创建 ZIP 临时文件，请检查已有临时文件和目录权限");
#endif
        cleanup.owned = true;
        std::size_t offset = 0;
        while (offset < bytes.size()) {
#ifdef _WIN32
            const auto amount = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset,
                                                                        std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(file.handle, bytes.data() + offset, amount, &written, nullptr) || written == 0)
                throw std::runtime_error("写入 ZIP 临时文件失败");
#else
            const auto amount = std::min<std::size_t>(bytes.size() - offset,
                                                      static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto written = ::write(file.handle, bytes.data() + offset, amount);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("写入 ZIP 临时文件失败");
#endif
            offset += static_cast<std::size_t>(written);
        }
#ifdef _WIN32
        if (!FlushFileBuffers(file.handle)) throw std::runtime_error("刷新 ZIP 临时文件失败");
#else
        if (::fsync(file.handle) != 0) throw std::runtime_error("刷新 ZIP 临时文件失败");
#endif
    }
    // 相邻文件保证同文件系统；原目标仅在替换成功时改变，不能使用 remove+rename 的空窗。
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("替换 ZIP 文件失败，原目标已保留");
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) throw std::runtime_error("替换 ZIP 文件失败，原目标已保留");
#endif
    cleanup.owned = false;
}

} // namespace

std::uint32_t crc32(std::string_view bytes) {
    std::uint32_t crc = 0xffffffffu;
    for (const auto raw : bytes) {
        crc ^= static_cast<unsigned char>(raw);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool safePackagePath(std::string_view path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/' || path.front() == '\\'
        || path.find('\\') != std::string_view::npos || path.find(':') != std::string_view::npos
        || path.find('\0') != std::string_view::npos) return false;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

xuyan::domain::Result<std::string> writeZip(const std::filesystem::path& destination,
                                            const std::vector<ZipEntry>& entries) {
    try {
        // 先写各条目的本地文件头和数据，末尾再生成中央目录索引。
        if (entries.size() > 65535) throw std::runtime_error("ZIP 条目数量超过格式上限");
        if (destination.empty()) throw std::runtime_error("ZIP 输出路径不能为空");
        /* 本次写入的中央目录快照，独立拥有名称；仅在内存组包期间存在，所有数字均按经典 ZIP 表示。 */
        struct Central {
            /* 已校验词法安全的包内相对路径，UTF-8 由条目调用方保证；创建时复制，中央目录输出读取。 */
            std::string name;
            /* 原始正文的 IEEE CRC-32，创建时计算，两个文件头读取。 */
            std::uint32_t crc;
            /* 未压缩正文大小，单位字节，创建时固定，两个文件头共用。 */
            std::uint32_t size;
            /* 本地文件头相对包起点的字节偏移，组包时固定。 */
            std::uint32_t offset;
        };
        std::vector<Central> central;
        std::set<std::string> names;
        // 先核对累计格式容量，再做窄化或分配，不能仅校验单条目大小。
        std::size_t archive_size = 22;
        constexpr auto maximum_classic_bytes = std::numeric_limits<std::uint32_t>::max();
        for (const auto& entry : entries) {
            if (!safePackagePath(entry.path)) throw std::runtime_error("ZIP 包含不安全路径");
            if (!names.insert(entry.path).second) throw std::runtime_error("ZIP 包含重复路径");
            const auto overhead = 76 + entry.path.size() * 2;
            if (overhead > maximum_classic_bytes - archive_size
                || entry.data.size() > maximum_classic_bytes - archive_size - overhead)
                throw std::runtime_error("ZIP 总大小超过经典格式上限");
            archive_size += overhead + entry.data.size();
        }
        std::string output;
        output.reserve(archive_size);
        for (const auto& entry : entries) {
            if (entry.path.size() > 65535 || entry.data.size() > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("ZIP 条目过大");
            Central item{entry.path, crc32(entry.data), static_cast<std::uint32_t>(entry.data.size()),
                         static_cast<std::uint32_t>(output.size())};
            put32(output, 0x04034b50); put16(output, 20); put16(output, 0x0800); put16(output, 0);
            put16(output, 0); put16(output, 0); put32(output, item.crc); put32(output, item.size); put32(output, item.size);
            put16(output, static_cast<std::uint16_t>(item.name.size())); put16(output, 0);
            output += item.name; output += entry.data; central.push_back(std::move(item));
        }
        const auto central_offset = static_cast<std::uint32_t>(output.size());
        for (const auto& item : central) {
            put32(output, 0x02014b50); put16(output, 20); put16(output, 20); put16(output, 0x0800); put16(output, 0);
            put16(output, 0); put16(output, 0); put32(output, item.crc); put32(output, item.size); put32(output, item.size);
            put16(output, static_cast<std::uint16_t>(item.name.size())); put16(output, 0); put16(output, 0);
            put16(output, 0); put16(output, 0); put32(output, 0); put32(output, item.offset); output += item.name;
        }
        const auto central_size = static_cast<std::uint32_t>(output.size()) - central_offset;
        put32(output, 0x06054b50); put16(output, 0); put16(output, 0);
        put16(output, static_cast<std::uint16_t>(central.size())); put16(output, static_cast<std::uint16_t>(central.size()));
        put32(output, central_size); put32(output, central_offset); put16(output, 0);
        std::error_code directory_error;
        if (!destination.parent_path().empty())
            std::filesystem::create_directories(destination.parent_path(), directory_error);
        if (directory_error) throw std::runtime_error("无法创建 ZIP 输出目录");
        writeStaged(destination, output);
        return xuyan::domain::Result<std::string>::success(destination.string());
    } catch (const std::system_error&) {
        return xuyan::domain::Result<std::string>::failure(packageError("ZIP 文件路径或文件操作失败"));
    } catch (const std::runtime_error& exception) {
        return xuyan::domain::Result<std::string>::failure(packageError(exception.what()));
    } catch (const std::exception&) {
        return xuyan::domain::Result<std::string>::failure(packageError("ZIP 写入失败，请检查可用内存及输出路径"));
    }
}

xuyan::domain::Result<std::vector<ZipEntry>> readZip(const std::filesystem::path& source, const ZipLimits& limits) {
    try {
        const auto bytes = readBounded(source, limits.maximum_archive_bytes);
        if (bytes.size() < 22) throw std::runtime_error("ZIP 文件过短");
        const auto search_begin = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
        std::size_t eocd = std::string::npos;
        for (std::size_t position = bytes.size() - 22;; --position) {
            if (get32(bytes, position) == 0x06054b50) { eocd = position; break; }
            if (position == search_begin) break;
        }
        if (eocd == std::string::npos) throw std::runtime_error("找不到 ZIP 中央目录");
        if (get16(bytes, eocd + 4) != 0 || get16(bytes, eocd + 6) != 0)
            throw std::runtime_error("不支持多磁盘 ZIP");
        const auto entries = get16(bytes, eocd + 10);
        const auto central_size = get32(bytes, eocd + 12);
        const auto central_offset = get32(bytes, eocd + 16);
        const auto comment_length = get16(bytes, eocd + 20);
        if (eocd + 22 + comment_length != bytes.size()) throw std::runtime_error("ZIP 尾部长度不一致");
        if (get16(bytes, eocd + 8) != entries || entries > limits.maximum_entries
            || central_offset > eocd || central_size > eocd - central_offset)
            throw std::runtime_error("ZIP 中央目录超出限制");
        std::vector<ZipEntry> result;
        result.reserve(entries);
        std::set<std::string> names;
        std::size_t total = 0;
        std::size_t cursor = central_offset;
        // 中央目录字段必须完全落在自身区域内；本地正文不得与目录重叠。
        const auto central_end = static_cast<std::size_t>(central_offset) + central_size;
        for (std::size_t index = 0; index < entries; ++index) {
            if (cursor > central_end || central_end - cursor < 46 || get32(bytes, cursor) != 0x02014b50)
                throw std::runtime_error("ZIP 中央目录损坏");
            const auto flags = get16(bytes, cursor + 8);
            const auto method = get16(bytes, cursor + 10);
            const auto expected_crc = get32(bytes, cursor + 16);
            const auto compressed = get32(bytes, cursor + 20);
            const auto uncompressed = get32(bytes, cursor + 24);
            const auto name_length = get16(bytes, cursor + 28);
            const auto extra_length = get16(bytes, cursor + 30);
            const auto entry_comment = get16(bytes, cursor + 32);
            const auto disk = get16(bytes, cursor + 34);
            const auto external = get32(bytes, cursor + 38);
            const auto local_offset = get32(bytes, cursor + 42);
            if (flags & 0x0001 || disk != 0) throw std::runtime_error("ZIP 加密或跨磁盘条目不受支持");
            if (method != 0 || compressed != uncompressed) throw std::runtime_error("仅接受无压缩的安全包条目");
            if (((external >> 16) & 0170000) == 0120000) throw std::runtime_error("ZIP 符号链接被拒绝");
            if (uncompressed > limits.maximum_entry_bytes || uncompressed > limits.maximum_total_bytes
                || total > limits.maximum_total_bytes - uncompressed)
                throw std::runtime_error("ZIP 解包大小超过限制");
            const auto variable_bytes = static_cast<std::size_t>(name_length) + extra_length + entry_comment;
            if (variable_bytes > central_end - cursor - 46) throw std::runtime_error("ZIP 条目名称被截断");
            std::string name = bytes.substr(cursor + 46, name_length);
            if (!safePackagePath(name) || !names.insert(name).second) throw std::runtime_error("ZIP 包含不安全或重复路径");
            if (local_offset > central_offset || central_offset - local_offset < 30
                || get32(bytes, local_offset) != 0x04034b50)
                throw std::runtime_error("ZIP 本地条目头损坏");
            const auto local_name = get16(bytes, local_offset + 26);
            const auto local_extra = get16(bytes, local_offset + 28);
            const auto local_variable = static_cast<std::size_t>(local_name) + local_extra;
            if (local_variable > central_offset - local_offset - 30) throw std::runtime_error("ZIP 本地条目名称被截断");
            const auto data_offset = static_cast<std::size_t>(local_offset) + 30 + local_variable;
            if (compressed > central_offset - data_offset) throw std::runtime_error("ZIP 条目数据被截断或与中央目录重叠");
            if (get16(bytes, local_offset + 6) != flags || get16(bytes, local_offset + 8) != method
                || get32(bytes, local_offset + 14) != expected_crc || get32(bytes, local_offset + 18) != compressed
                || get32(bytes, local_offset + 22) != uncompressed
                || bytes.substr(local_offset + 30, local_name) != name) {
                throw std::runtime_error("ZIP 本地条目与中央目录不一致");
            }
            std::string data = bytes.substr(data_offset, compressed);
            if (crc32(data) != expected_crc) throw std::runtime_error("ZIP 条目 CRC 校验失败");
            total += uncompressed;
            result.push_back({std::move(name), std::move(data)});
            cursor += 46 + name_length + extra_length + entry_comment;
        }
        if (cursor != static_cast<std::size_t>(central_offset) + central_size) throw std::runtime_error("ZIP 中央目录大小不一致");
        return xuyan::domain::Result<std::vector<ZipEntry>>::success(std::move(result));
    } catch (const std::system_error&) {
        return xuyan::domain::Result<std::vector<ZipEntry>>::failure(packageError("ZIP 文件路径或读取操作失败"));
    } catch (const std::runtime_error& exception) {
        return xuyan::domain::Result<std::vector<ZipEntry>>::failure(packageError(exception.what()));
    } catch (const std::exception&) {
        return xuyan::domain::Result<std::vector<ZipEntry>>::failure(packageError("ZIP 读取失败，请检查可用内存及文件路径"));
    }
}

} // namespace xuyan::package
