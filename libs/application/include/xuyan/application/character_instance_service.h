#pragma once

#include "xuyan/domain/character_instance.h"

#include <filesystem>

namespace xuyan::application {

class CharacterInstanceService {
public:
    /** @brief 绑定人物实例和分支根绑定所在的工作区数据库。 */
    explicit CharacterInstanceService(std::filesystem::path database_path);
    /** @brief 以卡片版本、世界快照和能力适配创建独立人物实例。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> instantiate(
        const std::string& command_id, const std::string& blueprint_id, int blueprint_version,
        const std::string& world_version_id, const std::string& snapshot_id,
        const std::string& adaptation_json, const std::string& knowledge_policy);
    /** @brief 按实例 ID 读取人物的当前状态和私有记忆。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> load(const std::string& instance_id);
    /** @brief 列出指定世界版本下的人物实例。 */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> list(const std::string& world_version_id);
    /** @brief 按期望修订保存某个人物实例的记忆，不回写其原始卡片。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    /** @brief 将分支固定到世界版本、历史快照和人物实例集合，拒绝静默重绑。 */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, std::string branch_id, std::string world_version_id,
        std::string snapshot_id, std::string history_mode, std::vector<std::string> character_instance_ids);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
