#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

/* 提取任务配置及持久化状态机入口；拥有路径，不拥有调度线程，领取/结算由仓储事务保证修订与预算。 */
class ExtractionJobService {
public:
    /*
     * 功能：绑定任务工作区。参数：database_path 为按值保存的路径。
     * 返回：初始化路径。失败：分配异常传播，不打开数据库。
     * 副作用：无；线程：同步构造，不创建后台调度。
     */
    explicit ExtractionJobService(std::filesystem::path database_path);
    /*
     * 功能：按章内段尾/句尾切片并冻结协议、连接指纹、输入模式及模型配置，创建任务本身不发送。
     * 参数：command_id 为幂等命令；source_id 为已有来源；maximum_codepoints 默认 6000，限 500—50000 码点；
     * overlap_codepoints 默认 200，须小于片长一半；max_requests 默认 0 自动按步骤加约一成余量，大于 0 为显式硬上限；
     * output_token_limit_per_request 默认 1200，限 1—1000000 输出词元；provider_connection_id 默认空为离线任务；
     * input 默认为原文模式，主干必须绑定连接；generation 默认为厂商默认思考配置，离线不能指定模型思考档位。
     * 返回：含全部切片及冻结预算的任务。失败：配置、来源、连接或任务校验/存储错误返回 Result；
     * 部分前置文件读取或仓储构造异常可传播，未承诺所有异常转 Result。
     * 副作用：读取有界全量标准化正文及索引，写任务与命令，不读取秘密；词元估算是原文粗略上界而非实测。
     * 线程：调用线程同步建立索引及事务，局部正文/索引于返回后释放，不拥有调度线程。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> create(
        const std::string& command_id, const std::string& source_id,
        std::size_t maximum_codepoints = 6000, std::size_t overlap_codepoints = 200,
        int max_requests = 0, int output_token_limit_per_request = 1200,
        const std::string& provider_connection_id = {},
        const xuyan::domain::ExtractionInputConfig& input = {},
        const xuyan::domain::ProviderGenerationConfig& generation = {});
    /*
     * 功能：列出全部持久化任务，包括终结任务。参数：无。
     * 返回：完整任务列表，空库成功为空。失败：数据库错误返回 Result。
     * 副作用：只读任务，不自动恢复/发送；线程：同步，列表可能包含历史输出，新调度应使用检查点。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> list();
    /*
     * 功能：按来源所属世界查询全部任务。参数：world_id 为非空世界稳定标识。
     * 返回：完整任务列表，无任务成功为空，可能含终结任务。失败：世界参数或存储错误返回 Result。
     * 副作用：只读任务，不发送；线程：调用线程同步，结果为自有值。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listForWorld(const std::string& world_id);
    /*
     * 功能：读取任务完整快照。参数：job_id 为任务稳定标识。
     * 返回：含全部步骤/历史输出及冻结配置的任务。失败：任务缺失或数据库错误返回 Result。
     * 副作用：只读；线程：同步，不持有连接供调用方跨线程使用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> load(const std::string& job_id);
    /*
     * 功能：读取调度用轻量检查点。参数：job_id 为任务标识。
     * 返回：计数、修订、预算及停止标志，不含历史步骤列表/输出。失败：任务缺失或存储错误返回 Result。
     * 副作用：只读任务；线程：同步，结果为值对象。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> loadState(const std::string& job_id);
    /*
     * 功能：查下一待执行片的定位元数据。参数：job_id 为任务标识。
     * 返回：成功可选步骤，无 ready 步骤为空可选值，不能等同任务完成。失败：任务/查询错误返回 Result。
     * 副作用：只读，不领取或扣预算；线程：同步，之后领取仍需任务修订校验。
     */
    xuyan::domain::Result<std::optional<xuyan::domain::ExtractionStep>> nextStep(const std::string& job_id);
    /*
     * 功能：按修订持久化取消并保留已提交候选。
     * 参数：command_id 为幂等命令；job_id 为任务；expected_revision 为当前任务修订。
     * 返回：完整任务快照。失败：状态/修订/命令或数据库错误返回 Result。
     * 副作用：取消未执行队列并记录标志，不撤回在途请求/费用；线程：同步，执行器在下次调度观察状态。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancel(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：取消任务并返回轻量结果。参数：command_id 为幂等命令；job_id 为任务；expected_revision 为当前修订。
     * 返回：同取消事务检查点。失败：状态/修订/命令或存储错误返回 Result。
     * 副作用：取消未执行步骤，不删除候选或撤销在途费用；线程：调用线程同步事务。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> cancelState(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：按预期任务修订领取下一 ready 片并计入请求硬预算。
     * 参数：command_id 为幂等领取命令；job_id 为任务；expected_revision 为当前任务修订。
     * 返回：含新尝试次数的已领取步骤。失败：取消/终结/需关注、无步骤、预算耗尽、修订或存储错误返回 Result。
     * 副作用：仓储同事务更新步骤、任务、预算及命令；失败/未知不能自动退还预算。
     * 线程：同步短事务，返回后才允许网络等待，不能携带数据库事务跨传输。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimNext(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：结算当前步骤尝试，不执行候选批量入库；候选输出须走 CandidateService。
     * 参数：command_id 为幂等命令；job_id 为任务；ordinal 为从 1 起步骤；expected_attempt 为领取尝试；
     * terminal_status 为 completed/failed/unknown 等仓储允许的终结内部值；output_json 为保留输出；
     * error_message 为脱敏中文错误，不得含秘密、私有正文或模型详细输出。
     * 返回：完整任务快照。失败：尝试/状态/命令冲突或存储错误返回 Result。
     * 副作用：写步骤/任务/命令，不自动重试或退预算；线程：同步，迟到回报由尝试校验拒绝。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /*
     * 功能：按相同结算规则返回轻量检查点。
     * 参数：command_id 为幂等命令；job_id 为任务；ordinal 为从 1 起步骤；expected_attempt 为已领取尝试；
     * terminal_status 为允许的终结内部值；output_json 为保留输出；error_message 为脱敏中文错误，不含正文或秘密。
     * 返回：同事务检查点。失败：状态、尝试、命令或存储冲突返回 Result。
     * 副作用：持久化步骤结算，不自动退预算/重发，不替代候选提交；线程：同步事务。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> finishStepState(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /*
     * 功能：作者明确确认后把失败/未知步骤重新设为待执行，不直接发送。
     * 参数：command_id 为幂等命令；job_id 为任务；ordinal 为从 1 起步骤；expected_attempt 为待重试的当前尝试。
     * 返回：完整任务。失败：步骤状态/尝试/命令或存储错误返回 Result。
     * 副作用：重设待执行状态，保留已消耗预算，下次领取再消耗额度；线程：同步，未知结果必须先人工核对。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    /*
     * 功能：启动时将遗留 running 步骤改为未知。参数：无，前提是旧执行者已结束。
     * 返回：恢复步骤数，0 表示没有遗留。失败：数据库错误返回 Result。
     * 副作用：持久化 unknown/关注状态，不猜测请求未计费，不自动重发；线程：由启动协调方同步串行执行。
     */
    xuyan::domain::Result<int> recoverInterrupted();
    /*
     * 功能：有限抽样复核原文位置与哈希并统计审核状态。
     * 参数：job_id 为任务；sample_limit 默认 100，限 1—1000 条候选，不保证随机或全书覆盖。
     * 返回：抽样数、证据有效数和审核分布，无候选成功为空统计；不代表语义精确率/召回率。
     * 失败：上限/任务或数据库错误返回 Result；个别原文读失败计为无效证据，不伪造有效。
     * 副作用：只读候选及原文，不调用模型；线程：同步，临时引文随循环释放。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionQualityReport> qualityReport(
        const std::string& job_id, int sample_limit = 100);
private:
    /* 任务工作区路径；构造后只读，与服务同寿命，不缓存检查点或持有共享连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
