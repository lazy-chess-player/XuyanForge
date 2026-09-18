#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>

namespace xuyan::domain {

struct EvidenceReference {
    std::string id;
    std::string entity_id;
    std::string field_path;
    std::string source_id;
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
    std::string quote;
    std::string quote_hash;
    std::string provenance_type{"original_fact"};
    int revision{0};
};

Result<EvidenceReference> validateEvidence(EvidenceReference evidence);

} // namespace xuyan::domain
