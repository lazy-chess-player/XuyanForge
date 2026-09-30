#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

/* 分支与会话控制入口；只拥有工作区路径，在调用线程建立局部仓储。没有生产回合执行器，不拥有模型/线程。 */
class SimulationService {
public:
    /*
     * 功能：绑定推演工作区。参数：database_path 为借用路径，构造时复制，不保留引用。
     * 返回：完成路径初始化。失败：复制异常传播，不验证数据库。
     * 副作用：无文件访问；线程：同步构造，实例不持有共享连接。
     */
    explicit SimulationService(const std::filesystem::path& database_path);

    /*
     * 功能：读取当前活动分支头。参数：无。
     * 返回：提交及状态视图。失败：无活动分支或仓储错误返回 Result，构造异常可传播。
     * 副作用：只读业务状态，打开数据库可能初始化结构，不生成世界；线程：同步查询。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> open();
    /*
     * 功能：按当前分支头请求暂停/继续。参数：command_id 为幂等命令；paused 为 true 暂停、false 继续。
     * 返回：更新后的提交视图。失败：无分支、头已改变或存储错误返回 Result，构造异常可传播。
     * 副作用：仓储提交分支状态与命令，不终止在途网络；线程：同步，调用方管理执行器生命周期。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(const std::string& command_id, bool paused);
    /*
     * 功能：以活动分支当前提交创建并切换新分支。参数：command_id 为幂等命令；name 为作者输入分支名。
     * 返回：新分支提交视图。失败：无分支、名称/命令或存储错误返回 Result，构造异常可传播。
     * 副作用：写分支及活动分支选择；线程：同步，不复制默认世界或启动推演。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> forkCurrent(const std::string& command_id,
                                                                 const std::string& name);
    /*
     * 功能：选择已有活动分支。参数：branch_id 为目标分支稳定标识。
     * 返回：目标分支头视图。失败：分支缺失或存储错误返回 Result，构造异常可传播。
     * 副作用：持久化活动分支选择；线程：同步，不负责取消调用方的旧任务。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    /*
     * 功能：查询分支列表。参数：无。
     * 返回：分支信息列表，空工作区成功为空。失败：仓储错误返回 Result，构造异常可传播。
     * 副作用：只读分支，打开数据库可能初始化结构；线程：同步查询。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> branches();
    /*
     * 功能：仅备份数据库，不复制 assets；完整备份应使用 BackupService。
     * 参数：destination 为数据库目标文件路径，是否可覆盖由仓储备份接口校验。
     * 返回：目标路径字符串。失败：目标/SQLite备份或存储错误返回 Result，构造异常可传播。
     * 副作用：写 SQLite 快照文件，不读取凭据；线程：同步操作，调用方独占备份目标。
     */
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    /*
     * 功能：创建会话配置与演员绑定，不执行回合。
     * 参数：command_id 为幂等命令；branch_id 为已有分支；max_turns 为回合上限；continuous 为连续模式；
     * max_calls 为模型调用上限；actors 为演员/模型绑定值集合，合法范围由领域及仓储校验。
     * 返回：持久化会话。失败：分支、绑定、预算或命令错误返回 Result，仓储构造异常可传播。
     * 副作用：写会话与命令，不发送小说；线程：同步，actors 移入会话，不保存调用方引用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSession(
        const std::string& command_id, const std::string& branch_id, int max_turns,
        bool continuous, int max_calls, std::vector<xuyan::domain::ActorModelBinding> actors);
    /*
     * 功能：读取会话完整状态。参数：session_id 为会话稳定标识。
     * 返回：含预算、演员及回合的值对象。失败：缺失会话或仓储错误返回 Result，构造异常可传播。
     * 副作用：只读会话；线程：调用线程同步完成。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> session(const std::string& session_id);
    /*
     * 功能：查询所有持久化会话。参数：无。
     * 返回：会话列表，无会话成功为空。失败：仓储错误返回 Result，构造异常可传播。
     * 副作用：只读会话，不启动或恢复执行；线程：同步。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> sessions();
    /*
     * 功能：按修订执行会话状态机动作。
     * 参数：command_id 为幂等命令；session_id 为会话；expected_revision 为现有修订；
     * action 为控制内部值，合法动作及转换由仓储校验。
     * 返回：新会话状态。失败：动作/状态不允许、修订或存储错误返回 Result，构造异常可传播。
     * 副作用：写控制状态与命令，不保证撤销已发请求或费用；线程：同步，执行器自行观察控制状态。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    /*
     * 功能：以当前分支头校验导演意图，生成下一状态并原子提交介入。
     * 参数：command_id 为幂等命令；session_id 为会话；actor_id 为发言/操作人物；speech 为发言；
     * operation 默认为 speak，仅接受领域白名单；target_id 默认为空，操作需要目标时必须填写；
     * holder_consented 默认 false，表示物品持有人是否明确同意。
     * 返回：更新会话。失败：会话/意图/操作/权限或头修订冲突返回 Result，构造异常可传播。
     * 副作用：推进回合及领域时间刻度，同事务写意图、状态、会话和命令；线程：同步，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> directorIntervene(
        const std::string& command_id, const std::string& session_id, const std::string& actor_id,
        const std::string& speech, const std::string& operation = "speak",
        const std::string& target_id = {}, bool holder_consented = false);
    /*
     * 功能：启动恢复时处理遗留运行会话。参数：无，调用前应确认旧执行器已退出。
     * 返回：处理会话数，0 表示无遗留。失败：仓储错误返回 Result，构造异常可传播。
     * 副作用：修改中断状态，不自动重发可能已计费请求；线程：同步，应由启动协调者串行调用。
     */
    xuyan::domain::Result<int> recoverInterruptedSessions();

private:
    /* 推演工作区路径；构造复制后只读，与服务同寿命，不拥有会话执行器或活跃连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
