#pragma once

#include "xuyan/domain/branch_outcome.h"

#include <filesystem>

namespace xuyan::application {

class BranchOutcomeService {
public:
    /** @brief 绑定分支提交与世界版本所在的工作区数据库。 */
    explicit BranchOutcomeService(std::filesystem::path database_path);

    /** @brief 对比两个分支的共同祖先、状态差异与未解决后果。 */
    xuyan::domain::Result<xuyan::domain::BranchComparison> compare(
        const std::string& left_branch_id, const std::string& right_branch_id);
    /** @brief 将指定分支导出为选定格式，可选择包含技术日志。 */
    xuyan::domain::Result<std::string> exportBranch(
        const std::string& branch_id, const std::filesystem::path& destination,
        const std::string& format, bool include_technical_log);
    /** @brief 导出已脱敏的工作区诊断资料。 */
    xuyan::domain::Result<std::string> exportDiagnostics(const std::filesystem::path& destination);
    /** @brief 把选定分支结果采纳为新的不可变世界版本。 */
    xuyan::domain::Result<xuyan::domain::WorldVersion> adoptAsWorldVersion(
        const std::string& command_id, const std::string& branch_id,
        const std::string& world_id, const std::string& title);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
