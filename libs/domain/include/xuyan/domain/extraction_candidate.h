#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_graph.h"

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

/** @brief 作者明确选择的已有实体；关联时必须同时核对候选和实体的当前修订。 */
struct CandidateEntitySelection {
    std::string entity_id;
    int expected_revision{0};
};

/** @brief 表示当前已确认实体的轻量端点建议；稳定ID和修订必须由作者明确选择。 */
struct RelationEndpointMatch {
    std::string entity_id;
    std::string name;
    std::string kind;
    std::vector<std::string> aliases;
    int revision{0};
    bool name_match{false};
    bool alias_match{false};
};

/** @brief 保存同一读快照中按世界及逐字标识匹配的有界端点页，不作自动绑定。 */
struct RelationEndpointMatchPage {
    std::string world_id;
    std::string mention;
    std::vector<RelationEndpointMatch> items;
    std::uint64_t total{0};
    int limit{0};
    std::int64_t offset{0};
};

/** @brief 校验待审核抽取候选及其原文证据范围。 */
Result<ExtractionCandidate> validateExtractionCandidate(ExtractionCandidate candidate);

} // namespace xuyan::domain
