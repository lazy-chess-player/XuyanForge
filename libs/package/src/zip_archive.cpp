#include "xuyan/package/zip_archive.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace xuyan::package {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

Error packageError(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "检查包文件后重试"};
}

void put16(std::string& output, std::uint16_t value) {
    output.push_back(static_cast<char>(value & 0xff));
    output.push_back(static_cast<char>((value >> 8) & 0xff));
}

void put32(std::string& output, std::uint32_t value) {
    put16(output, static_cast<std::uint16_t>(value & 0xffff));
    put16(output, static_cast<std::uint16_t>((value >> 16) & 0xffff));
}

std::uint16_t get16(std::string_view input, std::size_t offset) {
    if (offset + 2 > input.size()) throw std::runtime_error("ZIP 字段被截断");
    return static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset]))
         | static_cast<std::uint16_t>(static_cast<unsigned char>(input[offset + 1]) << 8);
}

std::uint32_t get32(std::string_view input, std::size_t offset) {
    return static_cast<std::uint32_t>(get16(input, offset))
         | (static_cast<std::uint32_t>(get16(input, offset + 2)) << 16);
}

std::string readBounded(const std::filesystem::path& source, std::size_t maximum) {
    std::error_code error;
    const auto size = std::filesystem::file_size(source, error);
    if (error || size > maximum) throw std::runtime_error(error ? "无法读取 ZIP 大小" : "ZIP 文件超过大小上限");
    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("无法打开 ZIP 文件");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) throw std::runtime_error("读取 ZIP 文件失败");
    return bytes;
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
        if (entries.size() > 65535) throw std::runtime_error("ZIP 条目数量超过格式上限");
        struct Central { std::string name; std::uint32_t crc; std::uint32_t size; std::uint32_t offset; };
        std::vector<Central> central;
        std::set<std::string> names;
        std::string output;
        for (const auto& entry : entries) {
            if (!safePackagePath(entry.path)) throw std::runtime_error("ZIP 包含不安全路径：" + entry.path);
            if (!names.insert(entry.path).second) throw std::runtime_error("ZIP 包含重复路径：" + entry.path);
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
        if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
        const auto temporary = destination.string() + ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("无法创建 ZIP 临时文件");
            file.write(output.data(), static_cast<std::streamsize>(output.size()));
            if (!file) throw std::runtime_error("写入 ZIP 失败");
        }
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        std::filesystem::rename(temporary, destination);
        return xuyan::domain::Result<std::string>::success(destination.string());
    } catch (const std::exception& exception) {
        return xuyan::domain::Result<std::string>::failure(packageError(exception.what()));
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
        if (entries > limits.maximum_entries || static_cast<std::size_t>(central_offset) + central_size > eocd)
            throw std::runtime_error("ZIP 中央目录超出限制");
        std::vector<ZipEntry> result;
        result.reserve(entries);
        std::set<std::string> names;
        std::size_t total = 0;
        std::size_t cursor = central_offset;
        for (std::size_t index = 0; index < entries; ++index) {
            if (get32(bytes, cursor) != 0x02014b50 || cursor + 46 > bytes.size()) throw std::runtime_error("ZIP 中央目录损坏");
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
            if (cursor + 46 + name_length + extra_length + entry_comment > bytes.size()) throw std::runtime_error("ZIP 条目名称被截断");
            std::string name = bytes.substr(cursor + 46, name_length);
            if (!safePackagePath(name) || !names.insert(name).second) throw std::runtime_error("ZIP 包含不安全或重复路径");
            if (static_cast<std::size_t>(local_offset) + 30 > bytes.size() || get32(bytes, local_offset) != 0x04034b50)
                throw std::runtime_error("ZIP 本地条目头损坏");
            const auto local_name = get16(bytes, local_offset + 26);
            const auto local_extra = get16(bytes, local_offset + 28);
            const auto data_offset = static_cast<std::size_t>(local_offset) + 30 + local_name + local_extra;
            if (data_offset + compressed > bytes.size()) throw std::runtime_error("ZIP 条目数据被截断");
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
    } catch (const std::exception& exception) {
        return xuyan::domain::Result<std::vector<ZipEntry>>::failure(packageError(exception.what()));
    }
}

} // namespace xuyan::package
