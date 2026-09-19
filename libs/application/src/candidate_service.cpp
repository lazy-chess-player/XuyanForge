#include "xuyan/application/candidate_service.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <optional>

namespace xuyan::application {
namespace {

using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

const JsonValue* required(const JsonValue& object, std::string_view key) {
    return object.isObject() ? object.find(key) : nullptr;
}

Result<xuyan::domain::ExtractionJob> protocolError(std::string message) {
    return Result<xuyan::domain::ExtractionJob>::failure(
        {ErrorCode::validation_failed, std::move(message), false, "拒绝该输出并检查模型协议"});
}

} // namespace

CandidateService::CandidateService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

Result<xuyan::domain::ExtractionJob> CandidateService::ingestStepOutput(
    const std::string& command_id, const std::string& job_id, int step_ordinal,
    int expected_attempt, const std::string& output_json) {
    if (output_json.size() > 8 * 1024 * 1024) return protocolError("候选输出超过 8 MiB 上限");
    auto parsed = xuyan::package::parseJson(output_json, 32, 20000);
    if (!parsed.ok() || !parsed.value->isObject()) return protocolError("候选输出不是有效 JSON 对象");
    const auto* schema = required(*parsed.value, "schema_version");
    const auto* prompt = required(*parsed.value, "prompt_version");
    const auto* items = required(*parsed.value, "candidates");
    if (schema == nullptr || !schema->isString() || schema->string() != "candidate-v1"
        || prompt == nullptr || !prompt->isString() || prompt->string() != "extract-v1"
        || items == nullptr || !items->isArray() || items->array().size() > 256)
        return protocolError("候选输出版本或 candidates 数组无效");
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto job = repository.loadExtractionJob(job_id);
        if (!job.ok()) return Result<xuyan::domain::ExtractionJob>::failure(*job.error);
        const auto step = std::find_if(job.value->steps.begin(), job.value->steps.end(), [step_ordinal](const auto& value) {
            return value.ordinal == step_ordinal;
        });
        if (step == job.value->steps.end()
            || (step->status != "running" && step->status != "completed") || step->attempt != expected_attempt)
            return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::revision_conflict, "候选对应的步骤尝试已过期", false, "刷新任务后重试"});
        SourceImportService sources(database_path_);
        auto step_text = sources.evidenceText(job.value->source_id,
                                              step->start_codepoint, step->end_codepoint);
        if (!step_text.ok()) return Result<xuyan::domain::ExtractionJob>::failure(*step_text.error);
        std::vector<xuyan::domain::ExtractionCandidate> candidates;
        candidates.reserve(items->array().size());
        for (std::size_t index = 0; index < items->array().size(); ++index) {
            const auto& item = items->array()[index];
            const auto* type = required(item, "type"); const auto* name = required(item, "name");
            const auto* fields = required(item, "fields"); const auto* start = required(item, "start_codepoint");
            const auto* end = required(item, "end_codepoint"); const auto* quote = required(item, "quote");
            const auto* provenance = required(item, "provenance_type");
            if (type == nullptr || !type->isString() || name == nullptr || !name->isString()
                || fields == nullptr || !fields->isObject() || start == nullptr || !start->isInteger()
                || end == nullptr || !end->isInteger() || quote == nullptr || !quote->isString()
                || provenance == nullptr || !provenance->isString() || start->integer() < 0 || end->integer() < 0)
                return protocolError("候选字段缺失或类型错误");
            const auto start_cp = static_cast<std::size_t>(start->integer());
            const auto end_cp = static_cast<std::size_t>(end->integer());
            if (start_cp < step->start_codepoint || end_cp > step->end_codepoint)
                return protocolError("候选证据范围超出当前文本块");
            auto original = xuyan::domain::codepointSlice(*step_text.value,
                start_cp - step->start_codepoint, end_cp - step->start_codepoint);
            if (!original.ok() || *original.value != quote->string())
                return protocolError("候选引文与来源码点区间不一致");
            xuyan::domain::ExtractionCandidate candidate;
            candidate.id = "candidate-" + xuyan::domain::sha256(command_id + '|' + std::to_string(index)).substr(0, 24);
            candidate.job_id = job_id; candidate.step_ordinal = step_ordinal; candidate.source_id = job.value->source_id;
            candidate.candidate_type = type->string(); candidate.name = name->string();
            candidate.fields_json = xuyan::package::writeJson(*fields); candidate.start_codepoint = start_cp;
            candidate.end_codepoint = end_cp; candidate.quote = quote->string();
            candidate.quote_hash = xuyan::domain::sha256(candidate.quote); candidate.provenance_type = provenance->string();
            candidate.schema_version = schema->string(); candidate.prompt_version = prompt->string();
            auto valid = xuyan::domain::validateExtractionCandidate(std::move(candidate));
            if (!valid.ok()) return Result<xuyan::domain::ExtractionJob>::failure(*valid.error);
            candidates.push_back(std::move(*valid.value));
        }
        return repository.commitExtractionCandidates(command_id, job_id, step_ordinal, expected_attempt,
                                                     output_json, std::move(candidates));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<std::vector<xuyan::domain::ExtractionCandidate>> CandidateService::list(const std::string& review_status) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listExtractionCandidates(review_status); }
    catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::ExtractionCandidate>>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<xuyan::domain::ExtractionCandidate> CandidateService::review(
    const std::string& command_id, const std::string& candidate_id, int expected_revision,
    const std::string& review_status, const std::string& name,
    const std::string& fields_json, const std::string& provenance_type) {
    auto fields = xuyan::package::parseJson(fields_json, 16, 2000);
    if (!fields.ok() || !fields.value->isObject()) return Result<xuyan::domain::ExtractionCandidate>::failure(
        {ErrorCode::validation_failed, "候选 fields 必须是有效 JSON 对象", false, "修正字段后重试"});
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto loaded = repository.loadExtractionCandidate(candidate_id);
        if (!loaded.ok()) return loaded;
        auto candidate = std::move(*loaded.value);
        candidate.name = name; candidate.fields_json = xuyan::package::writeJson(*fields.value);
        candidate.provenance_type = provenance_type; candidate.review_status = review_status;
        std::optional<xuyan::domain::WorldEntity> entity;
        if (review_status == "accepted") {
            auto source = repository.loadSource(candidate.source_id);
            if (!source.ok()) return Result<xuyan::domain::ExtractionCandidate>::failure(*source.error);
            xuyan::domain::WorldEntity value;
            value.world_id = source.value->world_id;
            value.id = "entity-from-" + candidate.id; value.name = candidate.name;
            value.kind = candidate.candidate_type == "event" ? "event"
                : candidate.candidate_type == "rule" ? "rule" : "other";
            if (candidate.candidate_type == "entity") {
                const auto* kind = fields.value->find("kind");
                if (kind != nullptr && kind->isString() && xuyan::domain::isSupportedEntityKind(kind->string())) value.kind = kind->string();
            }
            auto enriched_fields = *fields.value;
            enriched_fields.object()["xuyan_provenance_type"] = candidate.provenance_type;
            const auto truth_status = candidate.provenance_type == "in_text_claim" ? "claim"
                : candidate.provenance_type == "model_inference" ? "hypothesis" : "fact";
            enriched_fields.object()["xuyan_truth_status"] = truth_status;
            if (truth_status != std::string_view{"fact"}) value.kind = "other";
            value.description = truth_status == std::string_view{"claim"}
                ? "作者保留的原文人物说法，不作为世界真相。证据引文：" + candidate.quote
                : truth_status == std::string_view{"hypothesis"}
                    ? "作者保留的模型推断，仍是待验证假设。证据引文：" + candidate.quote
                    : "由提取候选审核接受的事实。证据引文：" + candidate.quote;
            value.attributes_json = xuyan::package::writeJson(enriched_fields); value.review_status = "accepted";
            entity = std::move(value);
        }
        return repository.reviewExtractionCandidate(command_id, std::move(candidate), expected_revision, std::move(entity));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionCandidate>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
