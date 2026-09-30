#pragma once

#include "xuyan/domain/branch_outcome.h"

#include <filesystem>

namespace xuyan::application {

/* 分支比较、导出与采纳入口；仅拥有路径，调用线程读取提交链并生成值/文件，不运行推演回合。 */
class BranchOutcomeService {
public:
    /*
     * 功能：绑定分支结果工作区。参数：database_path 为数据库路径，按值保存。
     * 返回：初始化路径。失败：分配异常传播，不验证存储。
     * 副作用：无文件操作；线程：同步构造，不持有连接。
     */
    explicit BranchOutcomeService(std::filesystem::path database_path);

    /*
     * 功能：沿提交链比较分支头及累计会话用量，当前仅比较已实现的场景字段。
     * 参数：left_branch_id、right_branch_id 为非空且不同的分支标识，分别对应比较左右侧。
     * 返回：共同祖先、差异、提交链与用量。失败：分支缺失、无共同祖先或读取错误返回 Result。
     * 副作用：只读分支，不保证多次查询组成同一读快照；线程：同步，不触发模型调用。
     */
    xuyan::domain::Result<xuyan::domain::BranchComparison> compare(
        const std::string& left_branch_id, const std::string& right_branch_id);
    /*
     * 功能：导出已完成且有提交标识的回合，未提交叙事不进入正文。
     * 参数：branch_id 为分支；destination 为输出文件，可替换现有文件；format 为 markdown/json/text 内部值；
     * include_technical_log 为是否在文本/Markdown附加操作与提交记录，JSON保持固定协议。
     * 返回：写入路径字符串。失败：格式/分支无效、读取或写文件失败返回 Result。
     * 副作用：读取会话并写临时文件后替换目标，失败尽力恢复旧文件；线程：同步，调用方独占目标文件。
     */
    xuyan::domain::Result<std::string> exportBranch(
        const std::string& branch_id, const std::filesystem::path& destination,
        const std::string& format, bool include_technical_log);
    /*
     * 功能：导出不含正文及本地原始标识的诊断统计。参数：destination 为输出文件，可替换旧文件。
     * 返回：目标路径。失败：读取或写文件失败返回 Result。
     * 副作用：会话标识先哈希，仅输出状态/计数/修订；采用临时文件替换，不包含凭据。
     * 线程：同步磁盘操作，调用方须独占目标。
     */
    xuyan::domain::Result<std::string> exportDiagnostics(const std::filesystem::path& destination);
    /*
     * 功能：把分支头摘要记录为推演来源资料，再发布世界版本。
     * 参数：command_id 为基础幂等命令，派生资料/版本子命令；branch_id 为来源分支；world_id 为目标世界；title 为资料标题。
     * 返回：发布版本。失败：分支/世界/资料校验、命令或存储错误返回 Result。
     * 副作用：资料创建与版本发布是两次独立事务；发布失败可能已留下资料，重放由子命令保护，未承诺整体回滚。
     * 线程：调用线程同步执行，不运行模型；推演结果保留来源性质，不自动证明为世界事实。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> adoptAsWorldVersion(
        const std::string& command_id, const std::string& branch_id,
        const std::string& world_id, const std::string& title);

private:
    /* 分支结果数据库路径；构造取得后只读，与服务同寿命，不缓存分支头或提交链。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
