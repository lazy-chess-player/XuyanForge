#include "xuyan/domain/extraction_candidate.h"
#include "xuyan/domain/hash.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<ExtractionCandidate> validateExtractionCandidate(ExtractionCandidate candidate) {
    constexpr std::array types{std::string_view{"entity"}, std::string_view{"event"},
        std::string_view{"relation"}, std::string_view{"rule"}};
    constexpr std::array provenance{std::string_view{"original_fact"}, std::string_view{"in_text_claim"},
        std::string_view{"model_inference"}, std::string_view{"author_setting"}};
    if (candidate.id.empty() || candidate.job_id.empty() || candidate.source_id.empty() || candidate.step_ordinal < 1)
        return Result<ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选缺少任务、步骤、来源或稳定 ID", false, "拒绝该模型输出"});
    if (std::find(types.begin(), types.end(), candidate.candidate_type) == types.end()
        || candidate.name.empty() || candidate.name.size() > 512)
        return Result<ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选类型或名称无效", false, "修正输出协议"});
    if (candidate.start_codepoint >= candidate.end_codepoint || candidate.end_codepoint - candidate.start_codepoint > 20000
        || candidate.quote.empty() || candidate.quote_hash != sha256(candidate.quote))
        return Result<ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选证据区间或引文摘要无效", false, "从来源重新提取证据"});
    if (candidate.fields_json.empty() || candidate.fields_json.size() > 1024 * 1024
        || candidate.fields_json.front() != '{' || candidate.fields_json.back() != '}')
        return Result<ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选 fields 必须是受限 JSON 对象", false, "修正输出协议"});
    if (std::find(provenance.begin(), provenance.end(), candidate.provenance_type) == provenance.end())
        return Result<ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选来源性质无效", false, "修正来源性质"});
    return Result<ExtractionCandidate>::success(std::move(candidate));
}

} // namespace xuyan::domain
