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
    /** @brief 绑定需要完整备份的工作区数据库及其资产目录。 */
    explicit BackupService(std::filesystem::path database_path);
    /** @brief 在线快照数据库并复制资产到新目录，写入哈希清单且排除系统凭据。 */
    xuyan::domain::Result<BackupReport> create(const std::filesystem::path& destination_directory);
    /** @brief 验证清单、路径和哈希后原子恢复到尚不存在的新工作区目录。 */
    static xuyan::domain::Result<BackupReport> restore(
        const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
