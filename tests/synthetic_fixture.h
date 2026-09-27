#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::test {

/** @brief 为规则回归构造只存在于测试目标中的合成情景。 */
domain::ScenarioState makeSyntheticInitialState();

/** @brief 仅在测试数据库中显式创建一组世界条目，已有条目时保持原状。 */
domain::Result<bool> installSyntheticEntities(const std::filesystem::path& database_path);

/** @brief 仅在测试数据库中显式创建一张人物卡，已有卡片时保持原状。 */
domain::Result<bool> installSyntheticBlueprint(const std::filesystem::path& database_path);

/** @brief 显式创建测试根分支，若同一测试工作区已有活动分支则读取其头。 */
domain::Result<domain::CommitView> ensureSyntheticBranch(const std::filesystem::path& database_path);

/** @brief 返回合成会话使用的两个固定模型绑定。 */
std::vector<domain::ActorModelBinding> syntheticActors();

/** @brief 通过测试响应推进活动分支一步，并验证仓储的幂等提交路径。 */
domain::Result<domain::CommitView> stepSyntheticBranch(
    const std::filesystem::path& database_path, const std::string& command_id);

/** @brief 通过测试响应推进持久化会话一回合，供恢复和预算回归使用。 */
domain::Result<domain::SimulationSession> stepSyntheticSession(
    const std::filesystem::path& database_path, const std::string& command_id,
    const std::string& session_id);

} // namespace xuyan::test
