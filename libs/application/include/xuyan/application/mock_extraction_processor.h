#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>

namespace xuyan::application {

class MockExtractionProcessor {
public:
    explicit MockExtractionProcessor(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processAll(const std::string& job_id, int maximum_steps = 10000);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
