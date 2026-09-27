#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

class SimulationService {
public:
    /** @brief 绑定当前工作区的推演分支与会话存储。 */
    explicit SimulationService(const std::filesystem::path& database_path);

    /** @brief 打开当前分支头，不创建任何默认世界资料。 */
    xuyan::domain::Result<xuyan::domain::CommitView> open();
    /** @brief 持久化当前分支的暂停或继续状态。 */
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(const std::string& command_id, bool paused);
    /** @brief 从当前提交派生独立分支并切换到新分支。 */
    xuyan::domain::Result<xuyan::domain::CommitView> forkCurrent(const std::string& command_id,
                                                                 const std::string& name);
    /** @brief 切换活动分支并返回其最新提交视图。 */
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    /** @brief 列出工作区中的推演分支。 */
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> branches();
    /** @brief 把当前工作区数据库备份到指定目标路径。 */
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    /** @brief 在指定分支创建持久化推演会话及演员模型绑定。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSession(
        const std::string& command_id, const std::string& branch_id, int max_turns,
        bool continuous, int max_calls, std::vector<xuyan::domain::ActorModelBinding> actors);
    /** @brief 读取指定会话及其预算、进度和终结状态。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> session(const std::string& session_id);
    /** @brief 列出当前工作区的推演会话。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> sessions();
    /** @brief 按期望修订执行暂停、继续或取消等会话控制动作。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    /** @brief 提交导演发言或白名单领域干预，并记录目标人物与同意状态。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> directorIntervene(
        const std::string& command_id, const std::string& session_id, const std::string& actor_id,
        const std::string& speech, const std::string& operation = "speak",
        const std::string& target_id = {}, bool holder_consented = false);
    /** @brief 启动时把中断的会话运行状态转为可恢复状态。 */
    xuyan::domain::Result<int> recoverInterruptedSessions();

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
