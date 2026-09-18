#pragma once

#include "xuyan/domain/extraction_candidate.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

class CandidateService {
public:
    explicit CandidateService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> ingestStepOutput(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> list(
        const std::string& review_status = "candidate");
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> review(
        const std::string& command_id, const std::string& candidate_id, int expected_revision,
        const std::string& review_status, const std::string& name,
        const std::string& fields_json, const std::string& provenance_type);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
