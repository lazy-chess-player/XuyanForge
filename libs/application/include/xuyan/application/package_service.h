#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

struct PackageReport {
    std::string package_id;
    std::string kind;
    std::filesystem::path path;
    int entity_count{0};
};

class PackageService {
public:
    /** @brief 绑定世界条目和人物卡所在的工作区数据库。 */
    explicit PackageService(std::filesystem::path database_path);

    /** @brief 将当前世界资料导出为带摘要清单的可移植世界包。 */
    xuyan::domain::Result<PackageReport> exportWorld(const std::filesystem::path& destination,
                                                      const std::string& title,
                                                      const std::string& author);
    /** @brief 验证世界包格式、完整性和路径后以幂等命令导入。 */
    xuyan::domain::Result<PackageReport> importWorld(const std::string& command_id,
                                                      const std::filesystem::path& source);
    /** @brief 导出指定人物卡，可选择排除私人备注。 */
    xuyan::domain::Result<PackageReport> exportCharacter(const std::string& blueprint_id,
                                                          const std::filesystem::path& destination,
                                                          const std::string& author,
                                                          bool include_private_notes = true);
    /** @brief 验证并导入人物卡包，保留版本与字段校验。 */
    xuyan::domain::Result<PackageReport> importCharacter(const std::string& command_id,
                                                          const std::filesystem::path& source);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
