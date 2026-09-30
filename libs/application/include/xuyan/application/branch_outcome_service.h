#pragma once

#include "xuyan/domain/branch_outcome.h"

#include <filesystem>

namespace xuyan::application {

/*
 * 分支头比较、会话记录导出和显式结果采纳的同步服务；当前摘要/差异仍使用既有场景字段，不是通用世界事实比较器。
 * 仅拥有数据库路径值，每次调用临时打开仓储，局部连接、流及值对象随调用结束释放；不运行推演或调用模型。
 * 无内部线程、跨查询读快照或目标文件锁；调用方负责数据库使用环境、输入引用有效期及输出文件独占。
 */
class BranchOutcomeService {
public:
    /*
     * 功能：保存分支结果所在工作区路径，不立即打开或验证数据库。
     * 参数：database_path：输入，按值接收后移入成员，无默认值；空路径不在构造时拒绝。
     *   建议使用明确的绝对路径，相对路径随后续调用时工作目录解析。
     * 返回：无返回值；完成路径初始化。
     * 失败：路径复制/初始化异常向调用方传播。
     * 副作用：不访问数据库、文件、网络或凭据。
     * 线程与生命周期：调用线程同步构造；路径与服务同寿命，不保留调用方路径引用或数据库连接。
     */
    explicit BranchOutcomeService(std::filesystem::path database_path);

    /*
     * 功能：沿单父提交链寻找共同祖先，比较两侧当前头状态，并分别累计该分支所有会话的已记录用量。
     * 参数：
     *   left_branch_id：输入，比较左侧的非空稳定分支标识，无默认值，只借用至返回。
     *   right_branch_id：输入，比较右侧的非空稳定分支标识，无默认值，须与左侧不同，只借用至返回。
     * 返回：成功 Result 含共同祖先、两侧头、祖先之后从旧到新的提交 ID、差异及调用/令牌计数。
     *   状态相同则差异列表为空；用量含所有匹配会话，不限祖先之后或已完成回合，不是金额估算。
     * 失败：空/相同标识返回 validation_failed，无共同祖先返回 missing_context；仓储错误直接返回。
     *   捕获标准异常转为 storage_error；依赖提交链无环，未设遍历上限，整数累计不检测溢出。
     * 副作用：查询不改分支/会话；打开仓储可能创建数据库及迁移结构，不保证多次查询属于同一快照。
     * 线程与生命周期：调用线程同步执行，不触发模型调用、取消或回调；返回对象独立持有查询结果。
     */
    xuyan::domain::Result<xuyan::domain::BranchComparison> compare(
        const std::string& left_branch_id, const std::string& right_branch_id);
    /*
     * 功能：导出该分支会话中 status 为 completed 且提交 ID 非空的回合，按头提交父链位置排序。
     *   不单独校验回合提交属于当前头父链；当前摘要仅覆盖回合、既有物品状态及人物信任值。
     * 参数：
     *   branch_id：输入，待导出分支稳定 ID，无默认值，只借用至返回；空/不存在由仓储报错。
     *   destination：输入，非空输出文件路径，无默认值，只借用至返回；可替换现有目标，调用方须独占目标及同级辅助文件。
     *   format：输入，无默认值，仅接收 markdown、json、text 内部协议值，不是界面显示标签。
     *   include_technical_log：输入，无默认值；true 时为文本/Markdown逐回合附加操作及提交 ID。
     *     false 仍导出头/分支/人物标识和用量；JSON 固定包含技术字段，不受该开关影响。
     * 返回：成功 Result 含写入目标路径字符串；没有合格回合仍导出头摘要及全部匹配会话用量。
     * 失败：未知格式/空目标返回 validation_failed；缺分支或仓储读取错误直接返回，标准异常转 storage_error。
     *   父链依赖无环且无长度上限；用量累计不检测整数溢出，文本格式不转义用户内容。
     * 副作用：打开仓储可能创建/迁移数据库；写同级临时文件、移走旧目标并重命名新文件，替换失败尝试恢复旧目标。
     *   临时及 previous 辅助名由哈希确定，可能覆盖/删除同名辅助文件；失败可能残留临时或旧备份文件。
     *   导出含用户对话、叙事和稳定标识，未做内容脱敏；无网络或系统凭据读取。
     * 线程与生命周期：调用线程同步执行，无取消或回调，不保留输入引用；多次读取不保证同一快照。
     */
    xuyan::domain::Result<std::string> exportBranch(
        const std::string& branch_id, const std::filesystem::path& destination,
        const std::string& format, bool include_technical_log);
    /*
     * 功能：导出工作区分支/会话数量及逐会话摘要，不序列化正文、路径、提示词、叙事或凭据字段。
     * 参数：destination：输入，只借用至返回，非空输出文件路径，无默认值；可替换已有目标。
     *   调用方须独占目标及同级临时/previous 文件。
     * 返回：成功 Result 含目标路径字符串；无分支/会话时计数为 0、会话数组为空。
     * 失败：空目标返回 validation_failed，仓储错误直接返回，读取/序列化/写入标准异常转 storage_error。
     * 副作用：打开仓储可能创建/迁移数据库；读取完整会话但仅输出会话 ID 的无盐 SHA-256、状态、回合数、调用数及修订。
     *   哈希可跨报告关联或用于猜测比对，不保证匿名；状态字段原样输出，不作自由文本脱敏。
     *   写同级临时文件并替换目标，失败尝试回滚旧文件，可能残留辅助文件；不读取系统凭据或发送网络请求。
     * 线程与生命周期：调用线程同步执行，无取消或回调；输入不保存，局部连接/文件流随作用域结束释放，多次查询不保证同一快照。
     */
    xuyan::domain::Result<std::string> exportDiagnostics(const std::filesystem::path& destination);
    /*
     * 功能：把分支当前头的场景摘要新建为 event 通用资料，再发布目标世界当前全部未删除条目的版本。
     *   资料属性保留 simulation_result 来源、candidate 待校对性质及分支/提交/状态哈希，不证明原文事实，也不创建专用时间线证据。
     * 参数：
     *   command_id：输入，基础命令 ID，无默认值，只借用至返回；派生 :material 和 :version 子命令。
     *     调用方应传非空且可重放的 ID；本服务不单独校验空值，重放仍重新读取当前分支头及世界父版本。
     *   branch_id：输入，来源分支稳定 ID，无默认值，只借用至返回，须可读取头状态。
     *   world_id：输入，目标世界 ID，无默认值，只借用至返回，不能为空；本服务不核对分支与目标世界的归属关系。
     *   title：输入，资料名称，无默认值，只借用至返回，须通过条目名称校验，不是版本标题。
     * 返回：成功 Result 含已发布版本；版本包含目标世界现有资料，不仅是本次摘要。
     * 失败：头读取、条目验证/插入、版本读取/发布或子命令负载冲突直接返回仓储错误；标准异常转 storage_error。
     *   同分支/头生成固定资料 ID，与 world_id 无关；重复采纳或头/父版本变化可能发生存储或命令冲突，未保证整体重放成功。
     * 副作用：打开仓储可能创建/迁移数据库；资料创建与版本发布是独立事务，后续失败可留下已提交资料。
     *   资料写入摘要和来源属性，发布版本冻结当前条目修订及检索范围；不改原文、不调用模型、不发送网络请求。
     * 线程与生命周期：调用线程同步执行，不持事务等待网络；输入引用不保存，无取消/回调或跨查询一致快照。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> adoptAsWorldVersion(
        const std::string& command_id, const std::string& branch_id,
        const std::string& world_id, const std::string& title);

private:
    /* 工作区数据库路径值，无数值单位、无成员默认值；构造参数初始化，空值不提前校验。各方法只读以打开局部仓储，与服务同寿命，不缓存连接/分支头/提交链。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
