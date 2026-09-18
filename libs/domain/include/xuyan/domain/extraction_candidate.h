#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>

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

Result<ExtractionCandidate> validateExtractionCandidate(ExtractionCandidate candidate);

} // namespace xuyan::domain
