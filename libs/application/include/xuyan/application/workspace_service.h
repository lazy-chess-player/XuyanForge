#pragma once

#include "xuyan/domain/world_entity.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

class WorkspaceService {
public:
    /** @brief 绑定保存世界条目的工作区数据库。 */
    explicit WorkspaceService(std::filesystem::path database_path);

    /** @brief 打开空白或现有工作区，并分页列出首批世界条目。 */
    xuyan::domain::Result<xuyan::domain::EntityPage> openAndList(int limit = 50);
    /** @brief 按名称、类型和分页参数检索世界条目。 */
    xuyan::domain::Result<xuyan::domain::EntityPage> search(
        const std::string& query, const std::string& kind = {}, int offset = 0, int limit = 50);
    /** @brief 通过幂等命令创建一个世界条目。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> create(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    /** @brief 按期望修订保存条目，拒绝覆盖并发编辑。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> save(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    /** @brief 按稳定 ID 读取最新世界条目。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> load(const std::string& entity_id);
    /** @brief 以软删除方式移除条目并保留版本冲突检查。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> remove(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    /** @brief 合并两个条目并原子改写受影响的证据引用。 */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> merge(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    /** @brief 按合并记录拆分已合并条目并恢复可追溯引用。 */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision = -1, int target_expected_revision = -1);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
