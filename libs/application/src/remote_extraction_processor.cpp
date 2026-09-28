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

namespace xuyan::application {
namespace {

using xuyan::domain::ExtractionJob;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

/** @brief 为同一步骤/尝试生成稳定命令 ID，使领取与完成回报可安全重放。 */
std::string commandId(std::string_view prefix, const std::string& job_id, int ordinal, int attempt) {
    return std::string(prefix) + '-' + xuyan::domain::sha256(job_id + '|' + std::to_string(ordinal)
        + '|' + std::to_string(attempt)).substr(0, 24);
}

/** @brief 构造可直接展示给用户的校验失败结果，不包含请求正文或凭据。 */
Result<ExtractionJob> error(std::string message) {
    return Result<ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::validation_failed, std::move(message), false, "检查模型连接或该步骤后重试"});
}

} // namespace

RemoteExtractionProcessor::RemoteExtractionProcessor(std::filesystem::path database_path,
    ICredentialStore& credentials, IProviderTransport& transport)
    : database_path_(std::move(database_path)), credentials_(credentials), transport_(transport) {}

Result<ExtractionJob> RemoteExtractionProcessor::processNext(const std::string& job_id) {
    ExtractionJobService jobs(database_path_);
    auto job = jobs.load(job_id);
    if (!job.ok()) return job;
    if (job.value->provider_connection_id.empty()) return error("此任务未绑定模型连接；请新建并选择连接");
    if (job.value->status == "cancelled" || job.value->status == "completed") return error("任务已经结束");
    // 旧任务继续可读，但不能在未重新确认的情况下更换协议或消耗发送预算。
    if (job.value->schema_version != typedCandidateSchemaVersion
        || job.value->prompt_version != typedCandidatePromptVersion)
        return error("此任务不是当前类型化提取协议；请重新创建任务并确认发送范围");
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto connection = repository.loadProviderConnection(job.value->provider_connection_id);
    if (!connection.ok()) return Result<ExtractionJob>::failure(*connection.error);
    // 配置在建任务后变化时，先于领取步骤和消耗请求预算拒绝；网关发送前还会再次核对。
    if (job.value->provider_connection_fingerprint.empty()
        || xuyan::domain::providerConnectionFingerprint(*connection.value)
            != job.value->provider_connection_fingerprint)
        return error("模型连接自建任务后已变化；请重新创建任务并确认发送目标");
    const auto next = std::find_if(job.value->steps.begin(), job.value->steps.end(), [](const auto& step) {
        return step.status == "ready";
    });
    if (next == job.value->steps.end()) return error("没有可抽样的待执行步骤");
    SourceImportService sources(database_path_);
    auto chunk = sources.evidenceText(job.value->source_id, next->start_codepoint, next->end_codepoint);
    if (!chunk.ok()) return Result<ExtractionJob>::failure(*chunk.error);

    // 原文片段确认可读之后才领取步骤；领取事务承担预算上限检查。
    const auto claimed = jobs.claimNext(commandId("remote-claim", job_id, next->ordinal, next->attempt + 1),
                                        job_id, job.value->revision);
    if (!claimed.ok()) return Result<ExtractionJob>::failure(*claimed.error);
    const auto& step = *claimed.value;
    // 所有请求失败均按同一尝试次数持久化，避免状态停留在运行中。
    const auto finishFailure = [&](const std::string& status, const std::string& reason) {
        return jobs.finishStep(commandId("remote-finish", job_id, step.ordinal, step.attempt), job_id,
                               step.ordinal, step.attempt, status, {}, reason);
    };
    // 四类输出分别要求不同字段；小说正文只作为不可信数据，无法指定写入事实。
    const auto schema = typedExtractionResponseSchema();
    const auto prompt = typedExtractionPrompt(*chunk.value);
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
        const auto status = generated.value->failure_kind == "timeout_unknown" ? "unknown" : "failed";
        return finishFailure(status, "模型请求未完成：" + generated.value->failure_kind);
    }
    auto parsed = parseTypedExtractionResponse(generated.value->text);
    if (!parsed.ok()) return finishFailure("failed", parsed.error->message);
    // 每条逐字引文重新映射到不可变原文，随后由候选服务再次做哈希和范围校验。
    JsonValue::Array candidates;
    for (const auto& item : *parsed.value) {
        const auto byte = chunk.value->find(item.quote);
        if (byte == std::string::npos) return finishFailure("failed", "模型引文无法在原文中定位");
        // 重复引文不能默认为第一次出现；这种证据锚点必须由人工补足上下文。
        if (chunk.value->find(item.quote, byte + 1) != std::string::npos)
            return finishFailure("failed", "模型引文在当前片段中出现多次，无法唯一定位");
        const auto start = step.start_codepoint + xuyan::domain::utf8CodepointCount(
            std::string_view(*chunk.value).substr(0, byte));
        const auto end = start + xuyan::domain::utf8CodepointCount(item.quote);
        candidates.emplace_back(JsonValue::Object{
            {"type", item.type}, {"name", item.name}, {"quote", item.quote},
            {"start_codepoint", static_cast<std::int64_t>(start)},
            {"end_codepoint", static_cast<std::int64_t>(end)},
            {"fields", item.fields},
            {"provenance_type", "model_inference"}});
    }
    const auto output = xuyan::package::writeJson(JsonValue::Object{
        {"schema_version", job.value->schema_version}, {"prompt_version", job.value->prompt_version},
        {"candidates", std::move(candidates)}});
    CandidateService ingestion(database_path_);
    auto committed = ingestion.ingestStepOutput(commandId("remote-commit", job_id, step.ordinal, step.attempt),
                                                job_id, step.ordinal, step.attempt, output);
    if (!committed.ok()) return finishFailure("failed", "模型候选未通过本地证据校验");
    return committed;
}

} // namespace xuyan::application
