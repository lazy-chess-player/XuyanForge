#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

struct ExtractionCandidate {
    std::string id;
    std::string job_id;
    int step_ordinal{0};
    std::string source_id;
    std::string candidate_type;
    std::string name;
    std::string fields_json{"{}"};
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
    std::string quote;
    std::string quote_hash;
    std::string provenance_type{"model_inference"};
    std::string review_status{"candidate"};
    std::string schema_version{"candidate-v1"};
    std::string prompt_version{"extract-v1"};
    int revision{0};
};

/** @brief 保存按世界、来源和审核状态查询的一页候选及匹配总数。 */
struct ExtractionCandidatePage {
    std::vector<ExtractionCandidate> items;
    std::uint64_t total{0};
    int limit{0};
    std::int64_t offset{0};
};

/** @brief 校验待审核抽取候选及其原文证据范围。 */
Result<ExtractionCandidate> validateExtractionCandidate(ExtractionCandidate candidate);

} // namespace xuyan::domain
