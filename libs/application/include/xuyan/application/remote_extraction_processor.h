#pragma once

#include "xuyan/application/provider_generation_service.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>

namespace xuyan::application {

class RemoteExtractionProcessor {
public:
    RemoteExtractionProcessor(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
