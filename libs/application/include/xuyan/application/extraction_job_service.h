#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

class ExtractionJobService {
public:
    explicit ExtractionJobService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> create(
        const std::string& command_id, const std::string& source_id,
        std::size_t maximum_codepoints = 6000, std::size_t overlap_codepoints = 200,
        int max_requests = 0, int output_token_limit_per_request = 1200,
        const std::string& provider_connection_id = {});
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> list();
    xuyan::domain::Result<xuyan::domain::ExtractionJob> load(const std::string& job_id);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancel(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimNext(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    xuyan::domain::Result<int> recoverInterrupted();
    xuyan::domain::Result<xuyan::domain::ExtractionQualityReport> qualityReport(
        const std::string& job_id, int sample_limit = 100);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
