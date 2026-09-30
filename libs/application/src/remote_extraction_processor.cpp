#include "xuyan/application/remote_extraction_processor.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/extraction_output_contract.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/storage/workspace_repository.h"
#include "xuyan/package/json.h"

#include <algorithm>
#include <mutex>
#include <type_traits>
#include <unordered_set>

namespace xuyan::application {
namespace {

using xuyan::domain::ExtractionJob;
using xuyan::domain::ExtractionJobState;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

/*
 * 远程批次进程内登记表，函数静态对象持有至进程退出，不拥有网络传输或线程。
 * 仅用于防止同一工作区任务在本进程重复发送，不提供跨进程排他保证。
 */
struct ExecutionRegistry {
    /* 保护 active 的互斥锁，默认未锁定；租约只在登记和释放时持有。 */
    std::mutex mutex;
    /* 当前被租约占用的工作区/任务键，初始为空，租约构造或析构时更新。 */
    std::unordered_set<std::string> active;
};

/*
 * 功能：取得跨处理器实例共享的进程内执行登记表。
 * 参数：无。返回：进程寿命内有效的 ExecutionRegistry 引用。
 * 失败：首次初始化分配异常可传播；副作用：首次调用创建空集合。
 * 线程：静态初始化安全，访问集合仍须持有 mutex。
 */
ExecutionRegistry& executionRegistry() {
    static ExecutionRegistry registry;
    return registry;
}

/*
 * 功能：把工作区数据库路径与任务 ID 编成进程内排他键。
 * 参数：database 为借用的数据库路径；job_id 为借用的稳定任务 ID。
 * 返回：规范路径及任务 ID 的 JSON 数组文本；Windows 仅折叠路径的 ASCII 大小写。
 * 失败：路径规范化、编码或分配异常可传播；副作用：只读文件系统路径信息，不写数据库。
 * 线程：同步，不保存路径或任务引用；此键不保证跨进程唯一。
 */
std::string executionKey(const std::filesystem::path& database, const std::string& job_id) {
    const auto path = std::filesystem::weakly_canonical(database).generic_u8string();
    std::string encoded(path.begin(), path.end());
#ifdef _WIN32
    for (auto& byte : encoded) if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
#endif
    return xuyan::package::writeJson(JsonValue::Array{encoded, job_id});
}

/*
 * 单次远程处理的进程内执行租约；成功构造后至析构前占用任务键，禁止复制。
 * 不拥有线程或传输端口；退出只释放本进程登记，不撤销已经发出的请求。
 */
class ExecutionLease {
public:
    /*
     * 功能：尝试登记同一工作区任务的进程内唯一执行权，不等待现有执行者。
     * 参数：database 为工作区路径；job_id 为任务 ID，均只在构造时借用。
     * 返回：完成租约初始化，通过 acquired() 查询是否取得。
     * 失败：规范化或分配异常可传播；副作用：成功时向共享集合插入键。
     * 线程：仅登记时短暂持锁，后续网络和 SQL 不持锁。
     */
    ExecutionLease(const std::filesystem::path& database, const std::string& job_id)
        : key_(executionKey(database, job_id)) {
        auto& registry = executionRegistry();
        const std::lock_guard lock(registry.mutex);
        acquired_ = registry.active.insert(key_).second;
    }
    /*
     * 功能：释放本租约成功登记的任务键；未取得时不触碰共享集合。
     * 参数：无。返回：无。失败：预期不传播异常。
     * 副作用：可能移除进程内 active 键；线程：仅释放时短暂持锁，不取消网络调用。
     */
    ~ExecutionLease() {
        if (!acquired_) return;
        auto& registry = executionRegistry();
        const std::lock_guard lock(registry.mutex);
        registry.active.erase(key_);
    }
    /* 禁止复制构造，避免同一任务键由两个租约释放；无运行时参数、返回或副作用。 */
    ExecutionLease(const ExecutionLease&) = delete;
    /* 禁止复制赋值，避免覆盖已有执行权；无运行时参数、返回或副作用。 */
    ExecutionLease& operator=(const ExecutionLease&) = delete;
    /*
     * 功能：查询本对象是否成功登记任务执行权。参数：无。
     * 返回：已取得为真，否则为假。失败：不抛异常。
     * 副作用：只读本对象；线程：调用方须保证对象在查询期间存活。
     */
    bool acquired() const noexcept { return acquired_; }
private:
    /* 工作区路径与任务 ID 的编码键；构造时生成，析构后销毁，只由当前租约拥有。 */
    std::string key_;
    /* 本租约是否成功插入键；初值假，构造时写入、析构时读取。 */
    bool acquired_{false};
};

/*
 * 功能：判断检查点是否要求人工处理，阻止未知结果后继续发送。
 * 参数：job 为调用期间借用的只读任务检查点。
 * 返回：requires_attention 的当前布尔值。失败：无主动错误。
 * 副作用：只读任务状态；线程：同步，不保存引用。
 */
bool needsAttention(const ExtractionJobState& job) {
    return job.requires_attention;
}

/*
 * 功能：由任务、步骤及尝试序号生成稳定的领取或完成命令 ID。
 * 参数：prefix 为固定操作前缀；job_id 为任务 ID；ordinal 为从 1 开始的步骤序号；
 * attempt 为该步骤当前尝试次数，均只在调用期间借用或复制。
 * 返回：带摘要截断值的独立字符串。失败：分配异常可传播。
 * 副作用：仅内存计算，不写仓储；线程：同步，不保存输入引用。
 */
std::string commandId(std::string_view prefix, const std::string& job_id, int ordinal, int attempt) {
    return std::string(prefix) + '-' + xuyan::domain::sha256(job_id + '|' + std::to_string(ordinal)
        + '|' + std::to_string(attempt)).substr(0, 24);
}

/*
 * 功能：把远程任务前置校验失败包装为指定任务结果类型。
 * 参数：模板 JobResult 为完整任务或检查点类型；message 为按值接收的中文提示，不得含正文或凭据。
 * 返回：validation_failed 的 Result<JobResult> 失败值。失败：分配异常可传播。
 * 副作用：仅构造内存对象；线程：同步，不保存输入引用。
 */
template<class JobResult = ExtractionJobState>
Result<JobResult> error(std::string message) {
    return Result<JobResult>::failure(
        {xuyan::domain::ErrorCode::validation_failed, std::move(message), false, "检查模型连接或该步骤后重试"});
}

} // namespace

RemoteExtractionProcessor::RemoteExtractionProcessor(std::filesystem::path database_path,
    ICredentialStore& credentials, IProviderTransport& transport)
    : database_path_(std::move(database_path)), credentials_(credentials), transport_(transport) {}

Result<ExtractionJob> RemoteExtractionProcessor::processNext(const std::string& job_id) {
    try {
        ExecutionLease lease(database_path_, job_id);
        if (!lease.acquired()) return error<ExtractionJob>("此解析任务已有执行者；请等待当前调用停止");
        return processNextUnchecked<ExtractionJob>(job_id);
    } catch (...) {
        return error<ExtractionJob>("远程解析发生内部错误；请检查持久化任务状态，详情已隐藏");
    }
}

Result<RemoteBatchResult> RemoteExtractionProcessor::processBatch(
    const std::string& job_id, const RemoteBatchOptions& options) {
    using BatchResult = Result<RemoteBatchResult>;
    if (options.maximum_steps < 1 || options.maximum_steps > 100000)
        return BatchResult::failure({xuyan::domain::ErrorCode::validation_failed,
            "远程批次必须明确指定有效步骤上限", false, "使用 1—100000"});
    try {
        ExecutionLease lease(database_path_, job_id);
        if (!lease.acquired()) return BatchResult::failure({xuyan::domain::ErrorCode::rule_conflict,
            "此解析任务已有执行者", false, "等待当前调用停止"});
        ExtractionJobService jobs(database_path_);
        auto current = jobs.loadState(job_id);
        if (!current.ok()) return BatchResult::failure(*current.error);
        if (current.value->provider_connection_id.empty()
            || current.value->schema_version != typedCandidateSchemaVersion
            || current.value->prompt_version != typedCandidatePromptVersion)
            return BatchResult::failure({xuyan::domain::ErrorCode::validation_failed,
                "批次仅支持绑定模型连接的当前原文提取协议", false, "重新创建任务并确认发送范围"});
        int processed = 0;
        for (;;) {
            auto action = RemoteBatchAction::proceed;
            if (options.on_progress) {
                const auto& job = *current.value;
                try {
                    action = options.on_progress({job.id, job.total_steps, job.completed_steps,
                        processed, job.revision, job.budget.consumed_requests, job.budget.max_requests});
                } catch (...) {
                    return BatchResult::failure({xuyan::domain::ErrorCode::validation_failed,
                        "远程批次进度回调失败；已提交检查点保留，详情已隐藏", false, "检查回调后显式继续"});
                }
            }
            // 回调可以取消任务；重新读取后才判断是否允许下一次发送。
            current = jobs.loadState(job_id);
            if (!current.ok()) return BatchResult::failure(*current.error);
            /*
             * 功能：停止批次时只读取一次完整任务，并校验与当前轻量检查点同一修订。
             * 参数：reason 为已确定的停止原因；jobs、job_id、current、processed 只在同步调用期间引用。
             * 返回：成功时为完整任务、计数及原因，读取或修订冲突时为失败 Result。
             * 失败：仓储读取及修订检查失败不触发再次发送。
             * 副作用：只读持久任务；线程：当前批次线程同步执行，捕获不逃逸。
             */
            const auto stopped = [&](RemoteBatchStopReason reason) {
                // 仅返回时读取一次完整结果，批次运行过程不复制前序模型输出。
                auto full = jobs.load(job_id);
                if (!full.ok()) return BatchResult::failure(*full.error);
                if (full.value->revision != current.value->revision)
                    return BatchResult::failure({xuyan::domain::ErrorCode::revision_conflict,
                        "批次停止检查点已被其他操作改变", false, "重新加载任务，不自动继续发送"});
                return BatchResult::success({std::move(*full.value), processed, reason});
            };
            if (current.value->status == "completed") return stopped(RemoteBatchStopReason::completed);
            if (current.value->status == "cancelled") return stopped(RemoteBatchStopReason::cancelled);
            if (needsAttention(*current.value)) return stopped(RemoteBatchStopReason::needs_attention);
            if (action == RemoteBatchAction::cancel || current.value->cancel_requested) {
                current = jobs.cancelState("remote-batch-cancel-" + xuyan::domain::sha256(
                    job_id + '|' + std::to_string(current.value->revision)), job_id, current.value->revision);
                if (!current.ok()) return BatchResult::failure(*current.error);
                return stopped(RemoteBatchStopReason::cancelled);
            }
            if (options.stop_token.stop_requested() || action == RemoteBatchAction::pause)
                return stopped(RemoteBatchStopReason::paused);
            if (processed >= options.maximum_steps) return stopped(RemoteBatchStopReason::step_limit);
            if (current.value->budget.consumed_requests >= current.value->budget.max_requests)
                return stopped(RemoteBatchStopReason::budget_exhausted);
            if (!current.value->has_ready_step) return stopped(RemoteBatchStopReason::needs_attention);
            // 不持有注册表锁或数据库事务等待 HTTP；逐步提交后再通知轻量进度。
            current = processNextUnchecked<ExtractionJobState>(job_id);
            if (!current.ok()) return BatchResult::failure(*current.error);
            ++processed;
        }
    } catch (...) {
        return BatchResult::failure({xuyan::domain::ErrorCode::storage_error,
            "远程批次发生内部错误；已提交检查点保留，详情已隐藏", false, "检查任务状态后显式继续"});
    }
}

template<class JobResult>
Result<JobResult> RemoteExtractionProcessor::processNextUnchecked(const std::string& job_id) {
    ExtractionJobService jobs(database_path_);
    auto job = jobs.loadState(job_id);
    if (!job.ok()) return Result<JobResult>::failure(*job.error);
    if (job.value->provider_connection_id.empty()) return error<JobResult>("此任务未绑定模型连接；请新建并选择连接");
    if (job.value->status == "cancelled" || job.value->status == "completed") return error<JobResult>("任务已经结束");
    if (job.value->cancel_requested || needsAttention(*job.value))
        return error<JobResult>("任务已请求取消或存在未结算步骤；请先人工核对状态");
    // 旧任务继续可读，但不能在未重新确认的情况下更换协议或消耗发送预算。
    if (job.value->schema_version != typedCandidateSchemaVersion
        || job.value->prompt_version != typedCandidatePromptVersion)
        return error<JobResult>("此任务不是当前类型化提取协议；请重新创建任务并确认发送范围");
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto connection = repository.loadProviderConnection(job.value->provider_connection_id);
    if (!connection.ok()) return Result<JobResult>::failure(*connection.error);
    const auto generation_config = xuyan::domain::validateProviderGenerationConfig(job.value->generation, connection.value->kind);
    if (!generation_config.ok()) return Result<JobResult>::failure(*generation_config.error);
    // 配置在建任务后变化时，先于领取步骤和消耗请求预算拒绝；网关发送前还会再次核对。
    if (job.value->provider_connection_fingerprint.empty()
        || xuyan::domain::providerConnectionFingerprint(*connection.value)
            != job.value->provider_connection_fingerprint)
        return error<JobResult>("模型连接自建任务后已变化；请重新创建任务并确认发送目标");
    auto next_result = jobs.nextStep(job_id);
    if (!next_result.ok()) return Result<JobResult>::failure(*next_result.error);
    if (!next_result.value->has_value()) return error<JobResult>("没有可抽样的待执行步骤");
    const auto& next = **next_result.value;
    SourceImportService sources(database_path_);
    auto chunk = sources.evidenceText(job.value->source_id, next.start_codepoint, next.end_codepoint);
    if (!chunk.ok()) return Result<JobResult>::failure(*chunk.error);
    // 不仅核对引文：上下文篡改也会改变模型含义，必须先于领取和消耗额度拒绝。
    if (xuyan::domain::sha256(*chunk.value) != next.chunk_hash)
        return error<JobResult>("解析原文片段与任务冻结摘要不一致；请重新导入并创建任务");
    auto input = buildExtractionInput(std::move(*chunk.value), next.start_codepoint, job.value->input);
    if (!input.ok()) return Result<JobResult>::failure(*input.error);

    // 原文片段确认可读之后才领取步骤；领取事务承担预算上限检查。
    const auto claimed = jobs.claimNext(commandId("remote-claim", job_id, next.ordinal, next.attempt + 1),
                                        job_id, job.value->revision);
    if (!claimed.ok()) return Result<JobResult>::failure(*claimed.error);
    const auto& step = *claimed.value;
    // 所有请求失败均按同一尝试次数持久化，避免状态停留在运行中。
    /*
     * 功能：将已领取步骤的失败或未知状态按同一尝试落盘，避免永久停在运行中。
     * 参数：status 为终态协议值；reason 为不含正文和凭据的中文说明；其余上下文借用至本次同步调用结束。
     * 返回：完整任务或检查点 Result，具体类型由 JobResult 决定。
     * 失败：仓储提交失败返回 Result；副作用：更新步骤和预算状态，不自动重发。
     * 线程：当前处理线程同步执行，捕获局部仓储与步骤不逃逸。
     */
    const auto finishFailure = [&](const std::string& status, const std::string& reason) {
        if constexpr (std::is_same_v<JobResult, ExtractionJobState>)
            return jobs.finishStepState(commandId("remote-finish", job_id, step.ordinal, step.attempt), job_id,
                                        step.ordinal, step.attempt, status, {}, reason);
        else return jobs.finishStep(commandId("remote-finish", job_id, step.ordinal, step.attempt), job_id,
                                    step.ordinal, step.attempt, status, {}, reason);
    };
    try {
        // 四类输出分别要求不同字段；小说正文只作为不可信数据，无法指定写入事实。
        const auto schema = typedExtractionResponseSchema();
        const auto prompt = typedExtractionPrompt(input.value->preview_text);
        ProviderGenerationService gateway(database_path_, credentials_, transport_);
        auto generated = gateway.generate(job.value->provider_connection_id, prompt, schema,
                                          job.value->budget.output_token_limit_per_request, 60000,
                                          job.value->provider_connection_fingerprint, job.value->generation);
        if (!generated.ok()) {
            // 传输异常可能发生在发送之后；未知请求不能被普通失败的重试路径自动重发。
            const auto status = generated.error->code == xuyan::domain::ErrorCode::storage_error
                ? "unknown" : "failed";
            return finishFailure(status, "模型连接或请求未完成；请人工核对后处理");
        }
        if (generated.value->status != "completed") {
            const auto& kind = generated.value->failure_kind;
            const auto status = kind == "timeout_unknown" || kind == "cancelled" || kind == "network"
                ? "unknown" : "failed";
            return finishFailure(status, "模型请求未完成：" + generated.value->failure_kind);
        }
        std::size_t rejected_candidates = 0;
        auto parsed = parseTypedExtractionResponse(generated.value->text, &rejected_candidates);
        if (!parsed.ok()) return finishFailure("failed", parsed.error->message);
        // 每条逐字引文重新映射到不可变原文，随后由候选服务再次做哈希和范围校验。
        JsonValue::Array candidates;
        for (const auto& item : *parsed.value) {
            // 在连续保留的原文中定位，省略标记不是证据，不允许把不连续句段拼成引文。
            const auto range = locateNarrativeQuote(*input.value, item.quote);
            if (!range.ok()) {
                ++rejected_candidates;
                continue;
            }
            candidates.emplace_back(JsonValue::Object{
                {"type", item.type}, {"name", item.name}, {"quote", item.quote},
                {"start_codepoint", static_cast<std::int64_t>(range.value->start_codepoint)},
                {"end_codepoint", static_cast<std::int64_t>(range.value->end_codepoint)},
                {"fields", item.fields},
                {"provenance_type", "model_inference"}});
        }
        if (!parsed.value->empty() && candidates.empty())
            return finishFailure("failed", "模型候选的引文全部无法唯一映射到保留原文");
        const auto output = xuyan::package::writeJson(JsonValue::Object{
            {"schema_version", job.value->schema_version}, {"prompt_version", job.value->prompt_version},
            {"rejected_candidates", static_cast<std::int64_t>(rejected_candidates)},
            {"candidates", std::move(candidates)}});
        CandidateService ingestion(database_path_);
        /*
         * 功能：根据调用方要求把已校验候选提交为完整任务或轻量检查点。
         * 参数：无；借用当前步骤、候选服务和 output，均在本次同步调用期间有效。
         * 返回：对应 JobResult 的提交结果，失败保持仓储错误。
         * 失败：候选及证据校验、修订或持久化错误由 Result 报告。
         * 副作用：成功时提交候选与步骤；线程：当前处理线程同步执行，捕获不逃逸。
         */
        const auto commit = [&] {
            if constexpr (std::is_same_v<JobResult, ExtractionJobState>)
                return ingestion.ingestStepOutputState(commandId("remote-commit", job_id, step.ordinal, step.attempt),
                                                        job_id, step.ordinal, step.attempt, output);
            else return ingestion.ingestStepOutput(commandId("remote-commit", job_id, step.ordinal, step.attempt),
                                                   job_id, step.ordinal, step.attempt, output);
        };
        auto committed = commit();
        if (!committed.ok()) return finishFailure("failed", "模型候选未通过本地证据校验");
        return committed;
    } catch (...) {
        // 领取之后的异常无法证明请求未发送，落盘为未知且禁止自动重发。
        return finishFailure("unknown", "步骤发生内部错误；请人工核对请求与检查点，详情已隐藏");
    }
}

} // namespace xuyan::application
