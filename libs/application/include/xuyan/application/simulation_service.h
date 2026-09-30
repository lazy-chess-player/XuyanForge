#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

/*
 * 职责：控制已有分支、持久化推演会话配置/控制请求及导演介入；当前只支持既有受限领域操作，没有生产回合执行器。
 * 资源与生命周期：仅拥有数据库路径；每次同步调用创建并销毁局部仓储，结果独立持有提交/会话值。
 * 线程：所有方法在调用线程执行，不拥有模型连接、执行线程或取消令牌；服务须覆盖调用有效期，销毁不能与调用并发。
 * 存储边界：包括查询在内的业务调用可创建父目录/数据库并迁移结构；不预置分支、人物或世界。
 * 异常边界：本服务不捕获异常；仓储各业务方法捕获的存储标准异常以 Result 返回，仓储构造/迁移及捕获范围外的异常传播。
 */
class SimulationService {
public:
    /*
     * 功能：保存后续分支/会话操作使用的数据库位置。
     * 参数：database_path：输入数据库文件路径，借用至构造结束并复制到成员；相对路径在业务调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径复制/分配异常直接传播。
     * 副作用：仅初始化本对象，构造后不保留调用方引用。
     */
    explicit SimulationService(const std::filesystem::path& database_path);

    /*
     * 功能：读取活动分支选择，再读取该分支的头提交和完整状态；两次读取没有共同读快照。
     * 参数：无；工作区须已有活动分支和可读取的头提交。
     * 返回：自有 CommitView，含分支/提交 ID、父提交、状态摘要及完整状态，不返回空视图表示缺失。
     * 失败：无活动分支为 missing_context；分支/提交缺失或快照损坏等仓储读取失败为 storage_error；打开/迁移异常直接传播。
     * 副作用：只读分支业务状态；打开可能创建/迁移结构，不生成世界或分支。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> open();
    /*
     * 功能：读取当前活动分支头，将其 paused 字段设为指定值并以头提交进行乐观提交；与会话控制请求分开。
     * 参数：
     *   command_id：输入幂等标识，调用期间借用；命令负载还包含本次读取的头提交，重用时头变化可能导致冲突。
     *   paused：输入 true 暂停分支状态、false 解除暂停；即使值未变，新命令仍产生新提交并递增状态修订。
     * 返回：自有新提交视图，或仓储同负载命令的固定提交视图。
     * 失败：无活动分支、头提交读取失败、读取后头变化或命令负载不同分别返回 Result；存储失败返回 storage_error，
     *   打开/迁移异常直接传播；调用方须保证状态 revision 可安全加 1，当前该路径未做上溢校验。
     * 副作用：同事务写快照、推进分支头及记命令；不改回合/时间或会话控制标志，不终止在途网络，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(const std::string& command_id, bool paused);
    /*
     * 功能：读取活动分支当前提交，复制其状态为新分支根，解除根状态暂停并切换活动选择。
     * 参数：
     *   command_id：输入幂等标识，调用期间借用；负载包含本次读取的源提交与 name，活动头变化后重用可能冲突。
     *   name：输入作者分支名，调用期间借用并原样保存；当前不单独拒绝空名、不检查长度或同名分支。
     * 返回：新分支根提交视图，根父提交指向源提交；同负载命令可返回固定提交，但重放不重新切换活动分支。
     * 失败：活动分支缺失、源提交读取/存储失败或命令冲突返回 Result，打开/迁移异常传播；
     *   调用方须保证源状态 revision 可安全加 1，该路径未做上溢校验。
     * 副作用：同事务写分支、根快照、活动选择与命令；状态修订加 1，不复制会话或根绑定，不启动推演，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> forkCurrent(const std::string& command_id,
                                                                 const std::string& name);
    /*
     * 功能：读取指定分支头并更新已有活动分支元数据项。
     * 参数：branch_id：输入已有分支稳定 ID，借用至返回；空值通常查无分支；正常工作区须已存在活动选择元数据项。
     * 返回：目标头提交的自有视图；当前 UPDATE 不补建缺失的元数据项，不能据返回值断言缺失元数据已修复。
     * 失败：分支/头缺失、快照无效或数据库操作失败为 storage_error；打开/迁移异常直接传播。
     * 副作用：事务更新活动选择，不产生新提交；不取消旧任务或修改会话状态，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    /*
     * 功能：读取工作区全部分支元数据，不加载各分支完整状态、不分页。
     * 参数：无。
     * 返回：按创建时间排序的自有分支列表，含名称、父分支、分叉提交及头提交 ID；无分支为成功空列表。
     * 失败：数据库查询失败为 storage_error；打开/迁移异常直接传播。
     * 副作用：只读分支业务记录，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> branches();
    /*
     * 功能：同步使用 SQLite 备份 API 复制数据库 main 内容；完整工作区备份应使用 BackupService。
     * 参数：destination：输入目标数据库文件路径，借用至返回；相对路径按进程当前目录解析，调用方须独占目标且与源区分；
     *   当前会创建父目录，并可覆盖已有目标数据库内容，没有“目标必须不存在”的保护，不复制 assets 或系统凭据。
     * 返回：成功返回 destination.string() 的路径文本，不保证是绝对路径。
     * 失败：目标目录/打开/SQLite 复制/关闭失败为 storage_error；源仓储打开/迁移异常直接传播；失败可能留有目标文件。
     * 副作用：创建目录并创建/改写目标数据库，源打开可能创建/迁移结构；不读写原文资产或凭据，不能撤销已覆盖目标。
     */
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    /*
     * 功能：创建已有分支的会话配置及有序人物/模型绑定，不执行回合。
     * 参数：
     *   command_id：输入幂等标识，调用期间借用；与 branch_id 的组合摘要前 20 位生成会话 ID，同标识须保持相同配置。
     *   branch_id：输入非空已有分支 ID，调用期间借用；不要求当前活动分支，也不在此验证根绑定。
     *   max_turns：输入回合次数上限，1—10000，无默认实参。
     *   continuous：输入连续模式标志，false 表示单步配置、true 表示连续配置；不在此调度执行。
     *   max_calls：输入模型调用次数硬上限，1—1000000，无默认实参。
     *   actors：输入有序绑定集合，按值移入会话，2—16 项；actor_id 不得为空或重复，连接 ID 和模型 ID 均非空；
     *     当前仅校验字段，不查询人物/连接实际存在或授权，顺序保留且参与命令身份。
     * 返回：新会话 status=ready、revision=1，无进展阈值 3，used/reserved/unknown_calls 均 0，
     *   pause/cancel_requested 均 false、回合集合为空；同负载命令重放读取现存会话，可能已推进。
     * 失败：上限/绑定非法为 validation_failed，分支缺失为 missing_context，命令负载不同为 command_conflict，
     *   存储失败为 storage_error；仓储打开/迁移及捕获范围外的值构造/摘要异常传播。
     * 副作用：同事务写会话、绑定及命令，打开可能创建/迁移结构；不读取凭据或发送文本。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSession(
        const std::string& command_id, const std::string& branch_id, int max_turns,
        bool continuous, int max_calls, std::vector<xuyan::domain::ActorModelBinding> actors);
    /*
     * 功能：读取会话配置、预算计数、控制状态、人物绑定及全部已存回合。
     * 参数：session_id：输入已有会话稳定 ID，借用至返回；空值作为查询值，通常无匹配。
     * 返回：完整自有会话值，含回合意图/叙事/调用记录；无回合时 turns 为空，但会话缺失不视为成功。
     * 失败：会话缺失或相关记录读取失败为 storage_error；打开/迁移异常直接传播。
     * 副作用：只读会话业务记录，打开可能创建/迁移结构；不执行或恢复回合。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> session(const std::string& session_id);
    /*
     * 功能：查询工作区全部会话及各会话完整记录，不分页、不限制到活动分支。
     * 参数：无。
     * 返回：按创建时间降序、会话 ID 升序的自有列表，含每个会话的全部回合；无会话为成功空列表。
     * 失败：查询或任一会话/回合读取失败为 storage_error；打开/迁移异常直接传播。
     * 副作用：只读会话业务记录，打开可能创建/迁移结构；不启动或恢复执行。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> sessions();
    /*
     * 功能：按会话自身修订保存暂停、恢复或取消请求，与分支状态的 setPaused 独立。
     * 参数：
     *   command_id：输入幂等标识，借用至返回；相同标识须复用 session_id、expected_revision 和 action。
     *   session_id：输入已有会话 ID，借用至返回。
     *   expected_revision：输入会话当前正修订，新会话为 1，必须匹配存储值。
     *   action：输入内部动作，借用至返回，仅 pause/resume/cancel；pause 拒绝 completed/cancelled，
     *     设置暂停请求且仅在 reserved_calls=0 时立即改为 paused；resume 仅限 paused/blocked 且 unknown_calls=0，
     *     改为 ready 并清除暂停/取消请求；cancel 设置取消、清除暂停并改 cancelled，当前不单独拒绝终态取消。
     * 返回：修订加 1 的完整会话；同负载命令重放读取现存会话，不再应用控制动作。
     * 失败：未知动作为 validation_failed，非法状态/未知调用阻止恢复为 rule_conflict，过期修订/负载冲突分别为
     *   revision_conflict/command_conflict；缺失会话或存储失败为 storage_error，打开/迁移异常传播。
     * 副作用：同事务更新会话控制状态、UTC 更新时间及命令；不更新分支 paused，不撤销在途请求/费用，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    /*
     * 功能：读取会话及其分支头，校验导演意图、执行受限领域规则，再原子提交介入；纯发言仅复制状态并加修订。
     * 参数：所有字符串均为输入，仅调用期间借用。
     *   command_id：幂等标识，负载绑定会话/行动者/操作/目标/同意/发言；重放前仍会读取当前头并执行前置规则。
     *   session_id：已有会话 ID；新提交时会话不能 completed/cancelled，且 reserved_calls 必须为 0。
     *   actor_id：非空会话绑定人物 ID；领域操作另须人物存在于当前状态，纯发言只核对会话绑定。
     *   speech：发言正文，最多 64 KiB（字节）；允许空串，当前没有非空发言校验。
     *   operation：默认 speak；仅 speak、inspect_seal、reveal_gate_secret、reveal_seal_forgery、transfer_seal 合法，
     *     后四项仍为当前受限规则，不表示任意世界的通用执行器。
     *   target_id：默认空；两种 reveal 及 transfer_seal 须指定非空目标并由规则核对存在，speak/inspect_seal 不要求目标。
     *   holder_consented：默认 false，输入物品持有人明确同意标志，仅 transfer_seal 用于转移许可，不代替持有人身份核对。
     * 返回：介入后的完整会话；会话修订加 1，原 blocked 状态改 paused，其他非终态保持；同负载命令可读现存会话。
     * 失败：意图无效、领域知识/持有权限不足、非会话人物、终态/在途调用、会话修订或头变化、命令冲突返回 Result；
     *   会话/分支读取或存储失败为 storage_error，打开/迁移及服务内值构造异常传播。
     *   调用方须保证头状态 revision/turn/elapsed_ticks 可安全加 1；当前纯发言及回合/时间增量未做上溢校验。
     * 副作用：状态修订/回合/离散时间刻数各加 1，同事务写提交快照、分支头、导演审计、会话及命令；
     *   不生成 simulation_turn 记录或模型调用，不读取凭据，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> directorIntervene(
        const std::string& command_id, const std::string& session_id, const std::string& actor_id,
        const std::string& speech, const std::string& operation = "speak",
        const std::string& target_id = {}, bool holder_consented = false);
    /*
     * 功能：将遗留 status=sent 的调用标为 unknown，并将其 requesting 回合标为 unknown、所属会话暂停。
     * 参数：无；调用前须确认旧执行器已退出，应由启动协调者串行调用，不处理所有运行态会话。
     * 返回：成功恢复的调用记录数（不是去重后的会话数），0 表示无 sent 调用；多个调用可属于同一会话。
     * 失败：关联回合不再 requesting、记录更新数量不符或其他存储失败为 storage_error，事务整体回滚；打开/迁移异常传播。
     * 副作用：同事务更新调用失败分类 interrupted、回合状态/修订及会话暂停请求/修订/UTC 更新时间，
     *   每个调用使 reserved_calls 至多减 1（最低 0）、unknown_calls 加 1；不自动重发/返还已用预算，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<int> recoverInterruptedSessions();

private:
    /* 推演数据库文件路径，无计量单位；初值为构造实参的副本，无默认实参，空值原样保存。
     * 构造写入，各方法只读；拥有路径值至服务销毁，不拥有会话执行器、模型连接或活跃 SQL 连接。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
