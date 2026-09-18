#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

struct ExtractionStep {
    std::string id;
    std::string job_id;
    int ordinal{0};
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
    std::string chunk_hash;
    std::string status{"ready"};
    int attempt{0};
    std::string output_json;
    std::string error_message;
};

struct ExtractionBudget {
    std::size_t estimated_input_tokens{0};
    int output_token_limit_per_request{1200};
    int max_requests{0};
    int consumed_requests{0};
    int sample_steps{0};
    bool price_known{false};
    std::int64_t estimated_cost_microunits{0};
    std::string currency;
};

struct ExtractionQualityReport {
    int sampled_candidates{0};
    int evidence_valid{0};
    int accepted{0};
    int rejected{0};
    int unresolved{0};
    bool model_quality_verified{false};
};

struct ExtractionJob {
    std::string id;
    std::string source_id;
    std::string status{"queued"};
    std::string schema_version{"candidate-v1"};
    std::string prompt_version{"extract-v1"};
    std::string provider_connection_id;
    std::string model_id;
    int total_steps{0};
    int completed_steps{0};
    bool cancel_requested{false};
    int revision{0};
    ExtractionBudget budget;
    std::vector<ExtractionStep> steps;
};

Result<ExtractionJob> validateExtractionJob(ExtractionJob job);

} // namespace xuyan::domain
