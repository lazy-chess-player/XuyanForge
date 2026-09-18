#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::package {

struct ZipEntry {
    std::string path;
    std::string data;
};

struct ZipLimits {
    std::size_t maximum_entries{1000};
    std::size_t maximum_entry_bytes{32 * 1024 * 1024};
    std::size_t maximum_total_bytes{128 * 1024 * 1024};
    std::size_t maximum_archive_bytes{128 * 1024 * 1024};
};

xuyan::domain::Result<std::string> writeZip(const std::filesystem::path& destination,
                                            const std::vector<ZipEntry>& entries);
xuyan::domain::Result<std::vector<ZipEntry>> readZip(const std::filesystem::path& source,
                                                     const ZipLimits& limits = {});
std::uint32_t crc32(std::string_view bytes);
bool safePackagePath(std::string_view path);

} // namespace xuyan::package
