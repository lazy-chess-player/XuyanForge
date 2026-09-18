#pragma once

#include "xuyan/domain/evidence.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

class EvidenceService {
public:
    explicit EvidenceService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::EvidenceReference> create(
        const std::string& command_id, const std::string& entity_id, const std::string& field_path,
        const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
        const std::string& provenance_type);
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listForSource(const std::string& source_id);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
