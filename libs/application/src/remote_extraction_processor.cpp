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

/** @brief 保存本进程正在执行的工作区/任务键；锁只保护集合，不跨越网络或回调。 */
struct ExecutionRegistry {
    std::mutex mutex;
    std::unordered_set<std::string> active;
};

/** @brief 返回跨处理器实例共享的租约注册表。 */
ExecutionRegistry& executionRegistry() {
    static ExecutionRegistry registry;
    return registry;
}

/** @brief 以规范 UTF-8 路径标识任务；Windows 的 ASCII 大小写别名归一化。 */
std::string executionKey(const std::filesystem::path& database, const std::string& job_id) {
    const auto path = std::filesystem::weakly_canonical(database).generic_u8string();
    std::string encoded(path.begin(), path.end());
#ifdef _WIN32
    for (auto& byte : encoded) if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
#endif
    return xuyan::package::writeJson(JsonValue::Array{encoded, job_id});
}

/** @brief 通过作用域释放执行权，覆盖失败返回与异常；不拥有线程或传输端口。 */
class ExecutionLease {
public:
    /** @brief 原子尝试登记任务，不等待已存在的执行者。 */
    ExecutionLease(const std::filesystem::path& database, const std::string& job_id)
        : key_(executionKey(database, job_id)) {
        auto& registry = executionRegistry();
        const std::lock_guard lock(registry.mutex);
        acquired_ = registry.active.insert(key_).second;
    }
    /** @brief 仅释放本实例成功取得的执行权。 */
    ~ExecutionLease() {
        if (!acquired_) return;
        auto& registry = executionRegistry();
        const std::lock_guard lock(registry.mutex);
        registry.active.erase(key_);
    }
    ExecutionLease(const ExecutionLease&) = delete;
    ExecutionLease& operator=(const ExecutionLease&) = delete;
    /** @brief 查询是否取得执行权，未取得时禁止领取或发送。 */
    bool acquired() const noexcept { return acquired_; }
private:
    std::string key_;
    bool acquired_{false};
};

/** @brief 检测必须由人工结算或恢复的步骤，防止跳过未知结果继续消耗预算。 */
bool needsAttention(const ExtractionJobState& job) {
    return job.requires_attention;
}

/** @brief 为同一步骤/尝试生成稳定命令 ID，使领取与完成回报可安全重放。 */
std::string commandId(std::string_view prefix, const std::string& job_id, int ordinal, int attempt) {
    return std::string(prefix) + '-' + xuyan::domain::sha256(job_id + '|' + std::to_string(ordinal)
        + '|' + std::to_string(attempt)).substr(0, 24);
}

/** @brief 构造可直接展示给用户的校验失败结果，不包含请求正文或凭据。 */
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
                                          job.value->provider_connection_fingerprint);
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
        auto parsed = parseTypedExtractionResponse(generated.value->text);
        if (!parsed.ok()) return finishFailure("failed", parsed.error->message);
        // 每条逐字引文重新映射到不可变原文，随后由候选服务再次做哈希和范围校验。
        JsonValue::Array candidates;
        for (const auto& item : *parsed.value) {
            // 在连续保留的原文中定位，省略标记不是证据，不允许把不连续句段拼成引文。
            const auto range = locateNarrativeQuote(*input.value, item.quote);
            if (!range.ok()) return finishFailure("failed", "模型引文不在唯一连续保留的原文中");
            candidates.emplace_back(JsonValue::Object{
                {"type", item.type}, {"name", item.name}, {"quote", item.quote},
                {"start_codepoint", static_cast<std::int64_t>(range.value->start_codepoint)},
                {"end_codepoint", static_cast<std::int64_t>(range.value->end_codepoint)},
                {"fields", item.fields},
                {"provenance_type", "model_inference"}});
        }
        const auto output = xuyan::package::writeJson(JsonValue::Object{
            {"schema_version", job.value->schema_version}, {"prompt_version", job.value->prompt_version},
            {"candidates", std::move(candidates)}});
        CandidateService ingestion(database_path_);
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
