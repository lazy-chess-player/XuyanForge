#include "xuyan/domain/evidence.h"
#include "xuyan/domain/hash.h"

#include <array>
#include <algorithm>

namespace xuyan::domain {

Result<EvidenceReference> validateEvidence(EvidenceReference evidence) {
    if (evidence.id.empty() || evidence.entity_id.empty() || evidence.source_id.empty()) return Result<EvidenceReference>::failure(
        {ErrorCode::validation_failed, "证据缺少稳定 ID、条目或来源", false, "重新选择条目和来源"});
    if (evidence.field_path.empty() || evidence.field_path.size() > 256) return Result<EvidenceReference>::failure(
        {ErrorCode::validation_failed, "字段路径不能为空或过长", false, "填写被证据支持的字段"});
    if (evidence.start_codepoint >= evidence.end_codepoint || evidence.end_codepoint - evidence.start_codepoint > 20000)
        return Result<EvidenceReference>::failure(
            {ErrorCode::validation_failed, "证据区间为空、反向或超过 20000 个码点", false, "重新选择较短原文"});
    if (evidence.quote.empty() || evidence.quote_hash != sha256(evidence.quote)) return Result<EvidenceReference>::failure(
        {ErrorCode::validation_failed, "证据引文与摘要不一致", false, "从原文重新选择证据"});
    constexpr std::array types{std::string_view{"original_fact"}, std::string_view{"in_text_claim"},
        std::string_view{"model_inference"}, std::string_view{"author_setting"}, std::string_view{"simulation_result"}};
    if (std::find(types.begin(), types.end(), evidence.provenance_type) == types.end()) return Result<EvidenceReference>::failure(
        {ErrorCode::validation_failed, "证据来源性质无效", false, "选择受支持的来源性质"});
    return Result<EvidenceReference>::success(std::move(evidence));
}

} // namespace xuyan::domain
