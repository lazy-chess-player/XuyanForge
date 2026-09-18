#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

struct BackupReport {
    std::filesystem::path workspace_database;
    int asset_count{0};
    std::uintmax_t asset_bytes{0};
};

class BackupService {
public:
    explicit BackupService(std::filesystem::path database_path);
    xuyan::domain::Result<BackupReport> create(const std::filesystem::path& destination_directory);
    static xuyan::domain::Result<BackupReport> restore(
        const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
