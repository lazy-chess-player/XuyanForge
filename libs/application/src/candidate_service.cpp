#include "xuyan/application/candidate_service.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/application/extraction_output_contract.h"
#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <optional>
#include <type_traits>

namespace xuyan::application {
namespace {

using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

/** @brief 从候选 JSON 对象读取必需字段；非对象时返回空指针。 */
const JsonValue* required(const JsonValue& object, std::string_view key) {
    return object.isObject() ? object.find(key) : nullptr;
}

/** @brief 将模型输出的协议错误转为拒绝提交的可报告结果。 */
template<class JobResult>
Result<JobResult> protocolError(std::string message) {
    return Result<JobResult>::failure(
        {ErrorCode::validation_failed, std::move(message), false, "拒绝该输出并检查模型协议"});
}

} // namespace

CandidateService::CandidateService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

Result<xuyan::domain::ExtractionJob> CandidateService::ingestStepOutput(
    const std::string& command_id, const std::string& job_id, int step_ordinal,
    int expected_attempt, const std::string& output_json) {
    return ingestStepOutputImpl<xuyan::domain::ExtractionJob>(command_id, job_id, step_ordinal, expected_attempt, output_json);
}

Result<xuyan::domain::ExtractionJobState> CandidateService::ingestStepOutputState(
    const std::string& command_id, const std::string& job_id, int step_ordinal,
    int expected_attempt, const std::string& output_json) {
    return ingestStepOutputImpl<xuyan::domain::ExtractionJobState>(command_id, job_id, step_ordinal, expected_attempt, output_json);
}

/** @brief 共用协议和原文证据校验，仅根据结果类型选择完整或检查点提交接口。 */
template<class JobResult>
Result<JobResult> CandidateService::ingestStepOutputImpl(
    const std::string& command_id, const std::string& job_id, int step_ordinal,
    int expected_attempt, const std::string& output_json) {
    if (output_json.size() > 8 * 1024 * 1024) return protocolError<JobResult>("候选输出超过 8 MiB 上限");
    auto parsed = xuyan::package::parseJson(output_json, 32, 20000);
    if (!parsed.ok() || !parsed.value->isObject()) return protocolError<JobResult>("候选输出不是有效 JSON 对象");
    const auto* schema = required(*parsed.value, "schema_version");
    const auto* prompt = required(*parsed.value, "prompt_version");
    const auto* items = required(*parsed.value, "candidates");
    const auto* rejected = required(*parsed.value, "rejected_candidates");
    if (schema == nullptr || !schema->isString() || prompt == nullptr || !prompt->isString()
        || !((schema->string() == "candidate-v1" && prompt->string() == "extract-v1")
            || isSupportedTypedCandidateProtocol(schema->string(), prompt->string()))
        || items == nullptr || !items->isArray() || items->array().size() > 256)
        return protocolError<JobResult>("候选输出版本或 candidates 数组无效");
    const auto typed_maximum = typedCandidateMaximumFor(schema->string(), prompt->string());
    const bool current_typed = schema->string() == typedCandidateSchemaVersion
        && prompt->string() == typedCandidatePromptVersion;
    if ((current_typed && (parsed.value->object().size() != 4 || rejected == nullptr || !rejected->isInteger()
            || rejected->integer() < 0 || rejected->integer() > static_cast<std::int64_t>(typed_maximum)
            || items->array().size() + static_cast<std::size_t>(rejected->integer()) > typed_maximum))
        || (!current_typed && (parsed.value->object().size() != 3 || rejected != nullptr)))
        return protocolError<JobResult>("候选输出的逐条校验统计与协议版本不一致");
    if (typed_maximum > 0 && items->array().size() > typed_maximum)
        return protocolError<JobResult>("类型化单步输出超过当前协议的候选上限");
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto job = repository.loadExtractionJobState(job_id);
        if (!job.ok()) return Result<JobResult>::failure(*job.error);
        if (job.value->schema_version != schema->string() || job.value->prompt_version != prompt->string())
            return protocolError<JobResult>("候选输出版本与任务固定协议不一致");
        auto metadata = repository.loadExtractionStepMetadata(job_id, step_ordinal);
        if (!metadata.ok()) return Result<JobResult>::failure(*metadata.error);
        const auto* step = &*metadata.value;
        if ((step->status != "running" && step->status != "completed") || step->attempt != expected_attempt)
            return Result<JobResult>::failure(
                {ErrorCode::revision_conflict, "候选对应的步骤尝试已过期", false, "刷新任务后重试"});
        // 重新读取不可变原文，不信任模型报告的引文文本或码点范围。
        SourceImportService sources(database_path_);
        auto step_text = sources.evidenceText(job.value->source_id,
                                              step->start_codepoint, step->end_codepoint);
        if (!step_text.ok()) return Result<JobResult>::failure(*step_text.error);
        // 重新核对完整上下文摘要，防止在途改变未引用文字却通过逐字引文校验。
        if (xuyan::domain::sha256(*step_text.value) != step->chunk_hash)
            return protocolError<JobResult>("解析原文片段与任务冻结摘要不一致");
        auto valid_input = xuyan::domain::validateExtractionInputConfig(job.value->input);
        if (!valid_input.ok()) return Result<JobResult>::failure(*valid_input.error);
        std::optional<NarrativePreview> retained_input;
        if (job.value->input.mode == "backbone") {
            auto input = buildExtractionInput(*step_text.value, step->start_codepoint, job.value->input);
            if (!input.ok()) return Result<JobResult>::failure(*input.error);
            retained_input = std::move(*input.value);
        }
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
                return protocolError<JobResult>("候选字段缺失或类型错误");
            if (item.object().size() != 7) return protocolError<JobResult>("候选含有协议以外的属性");
            if (typed_maximum > 0) {
                // 模型不能绕过远程解析器，提交缺字段候选或自称人工确认事实。
                auto typed = validateTypedCandidateFields(type->string(), *fields, quote->string(), name->string());
                if (!typed.ok()) return Result<JobResult>::failure(*typed.error);
                if (provenance->string() != "model_inference") return protocolError<JobResult>("模型候选不能自动声明为确认事实");
            }
            const auto start_cp = static_cast<std::size_t>(start->integer());
            const auto end_cp = static_cast<std::size_t>(end->integer());
            if (start_cp < step->start_codepoint || end_cp > step->end_codepoint)
                return protocolError<JobResult>("候选证据范围超出当前文本块");
            auto original = xuyan::domain::codepointSlice(*step_text.value,
                start_cp - step->start_codepoint, end_cp - step->start_codepoint);
            if (!original.ok() || *original.value != quote->string())
                return protocolError<JobResult>("候选引文与来源码点区间不一致");
            if (retained_input) {
                // 候选服务自身执行保留范围校验，其他入口不能绕过远程处理器提交省略内容。
                const auto mapped = locateNarrativeQuote(*retained_input, quote->string());
                if (!mapped.ok() || mapped.value->start_codepoint != start_cp || mapped.value->end_codepoint != end_cp)
                    return protocolError<JobResult>("候选引文不在唯一连续保留的原文范围内");
            }
            xuyan::domain::ExtractionCandidate candidate;
            candidate.id = "candidate-" + xuyan::domain::sha256(command_id + '|' + std::to_string(index)).substr(0, 24);
            candidate.job_id = job_id; candidate.step_ordinal = step_ordinal; candidate.source_id = job.value->source_id;
            candidate.candidate_type = type->string(); candidate.name = name->string();
            candidate.fields_json = xuyan::package::writeJson(*fields); candidate.start_codepoint = start_cp;
            candidate.end_codepoint = end_cp; candidate.quote = quote->string();
            candidate.quote_hash = xuyan::domain::sha256(candidate.quote); candidate.provenance_type = provenance->string();
            candidate.schema_version = schema->string(); candidate.prompt_version = prompt->string();
            auto valid = xuyan::domain::validateExtractionCandidate(std::move(candidate));
            if (!valid.ok()) return Result<JobResult>::failure(*valid.error);
            candidates.push_back(std::move(*valid.value));
        }
        if constexpr (std::is_same_v<JobResult, xuyan::domain::ExtractionJobState>)
            return repository.commitExtractionCandidatesState(command_id, job_id, step_ordinal, expected_attempt,
                                                              output_json, std::move(candidates));
        else return repository.commitExtractionCandidates(command_id, job_id, step_ordinal, expected_attempt,
                                                         output_json, std::move(candidates));
    } catch (const std::exception& exception) { return Result<JobResult>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<std::vector<xuyan::domain::ExtractionCandidate>> CandidateService::list(const std::string& review_status) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listExtractionCandidates(review_status); }
    catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::ExtractionCandidate>>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<xuyan::domain::ExtractionCandidatePage> CandidateService::listPage(
    const std::string& world_id, const std::string& source_id,
    const std::string& review_status, int limit, std::int64_t offset) {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).listExtractionCandidatesPage(
            world_id, source_id, review_status, limit, offset);
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::ExtractionCandidatePage>::failure(
            {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"});
    }
}

/** @brief 查询逐字名称或别名的端点建议，只读取作者已确认的当前世界条目。 */
Result<xuyan::domain::RelationEndpointMatchPage> CandidateService::matchRelationEndpoints(
    const std::string& world_id, const std::string& mention, int limit, std::int64_t offset) {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).matchRelationEndpoints(world_id, mention, limit, offset);
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::RelationEndpointMatchPage>::failure(
            {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"});
    }
}

/** @brief 转交明确的跨章实体关联，不根据同名结果替作者选择目标，不进行网络请求。 */
Result<xuyan::domain::ExtractionCandidate> CandidateService::acceptIntoEntity(
    const std::string& command_id, const std::string& candidate_id, int expected_candidate_revision,
    const xuyan::domain::CandidateEntitySelection& selection, const std::string& provenance_type) {
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        return repository.acceptCandidateIntoEntity(command_id, candidate_id, expected_candidate_revision,
                                                    selection, provenance_type);
    } catch (...) {
        return Result<xuyan::domain::ExtractionCandidate>::failure({ErrorCode::storage_error,
            "实体关联无法打开工作区，详情已隐藏", true, "检查工作区后重试"});
    }
}

/** @brief 按作者明确的端点选择和来源性质生成条目/事件/关系/地点投影，统一由仓储原子提交。 */
Result<xuyan::domain::ExtractionCandidate> CandidateService::review(
    const std::string& command_id, const std::string& candidate_id, int expected_revision,
    const std::string& review_status, const std::string& name,
    const std::string& fields_json, const std::string& provenance_type,
    std::optional<xuyan::domain::RelationEndpointSelection> endpoint_selection) {
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
        const bool typed_candidate = isSupportedTypedCandidateProtocol(
            candidate.schema_version, candidate.prompt_version);
        if (typed_candidate) {
            auto valid = validateTypedCandidateFields(candidate.candidate_type, *fields.value, candidate.quote, candidate.name);
            if (!valid.ok()) return Result<xuyan::domain::ExtractionCandidate>::failure(*valid.error);
        }
        std::optional<xuyan::domain::WorldEntity> entity;
        std::optional<xuyan::domain::TimelineEvent> timeline;
        std::optional<xuyan::domain::CandidateGraphProjection> graph;
        const bool typed_relation = typed_candidate
            && candidate.candidate_type == "relation" && review_status == "accepted";
        if (typed_relation != endpoint_selection.has_value())
            return Result<xuyan::domain::ExtractionCandidate>::failure({ErrorCode::validation_failed,
                "接受类型化关系须明确选择两个已确认端点；其他审核不携带端点", false, "校对主语、宾语及当前条目修订"});
        // 接受候选时仍按来源类型区分事实、原文人物说法和模型假设。
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
                if (typed_candidate) {
                    // 别名已通过逐字证据校验；保存为正式别名用于消歧，但不据此合并同名条目。
                    for (const auto& alias : fields.value->find("aliases")->array()) value.aliases.push_back(alias.string());
                }
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
            if (typed_candidate && candidate.candidate_type == "event") {
                xuyan::domain::TimelineEvent event;
                // 与接受条目共用稳定标识，现有证据查询可直接回到原文；说法和假设不升级为事实。
                event.id = value.id; event.world_id = value.world_id; event.name = value.name;
                event.truth_status = truth_status;
                event.narrative_order = candidate.step_ordinal;
                event.relative_time = fields.value->find("time_text")->string();
                // 文本切片序号仅表示本来源的叙述位置，不生成绝对故事时间或前因后果边。
                timeline = std::move(event);
            }
            if (typed_relation) {
                xuyan::domain::DirectedRelation relation;
                relation.id = value.id; relation.world_id = value.world_id;
                relation.from_entity_id = endpoint_selection->from_entity_id;
                relation.to_entity_id = endpoint_selection->to_entity_id;
                relation.dimension = fields.value->find("predicate")->string();
                relation.bidirectional = !fields.value->find("directed")->boolean();
                relation.strength = std::nullopt; relation.visibility = "author";
                relation.truth_status = truth_status; relation.evidence_status = std::string_view(truth_status) == "fact" ? "evidence" : "assumption";
                // 小说陈述不推定关系强度、有效日期或任何人物的知情权限。
                graph = xuyan::domain::CandidateGraphProjection{std::move(relation), std::nullopt, std::move(endpoint_selection)};
            } else if (typed_candidate && candidate.candidate_type == "entity"
                       && fields.value->find("kind")->string() == "location") {
                xuyan::domain::LocationPlacement location;
                location.location_id = value.id; location.truth_status = truth_status;
                location.evidence_status = std::string_view(truth_status) == "fact" ? "evidence" : "assumption";
                // 名称/别名候选不含地理位置或拓扑，保持坐标、父地点、底图和路线未知。
                graph = xuyan::domain::CandidateGraphProjection{std::nullopt, std::move(location), std::nullopt};
            }
            entity = std::move(value);
        }
        return repository.reviewExtractionCandidate(command_id, std::move(candidate), expected_revision,
                                                      std::move(entity), std::move(timeline), std::move(graph));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionCandidate>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
