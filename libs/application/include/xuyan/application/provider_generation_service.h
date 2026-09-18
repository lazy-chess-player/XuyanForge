#pragma once

#include "xuyan/application/credential_store.h"
#include "xuyan/providers/model_protocol.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

struct ProviderTransportResponse {
    int http_status{0};
    bool timed_out{false};
    bool cancelled{false};
    std::string body;
};

class IProviderTransport {
public:
    virtual ~IProviderTransport() = default;
    virtual xuyan::domain::Result<ProviderTransportResponse> send(
        const xuyan::providers::ProviderHttpRequest& request,
        const std::string& credential, int timeout_ms) = 0;
};

struct ProviderTestReport {
    std::string connection_id;
    std::string provider_kind;
    std::string model_id;
    std::string status;
    std::string failure_kind;
    int input_tokens{0};
    int output_tokens{0};
    int elapsed_ms{0};
    bool json_valid{false};
};

class ProviderGenerationService {
public:
    ProviderGenerationService(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);

    xuyan::domain::Result<xuyan::providers::ProviderGenerationResult> generate(
        const std::string& connection_id, const std::string& prompt,
        const std::string& json_schema, int max_output_tokens = 1024,
        int timeout_ms = 30000);
    xuyan::domain::Result<ProviderTestReport> testStructuredGeneration(
        const std::string& connection_id, int timeout_ms = 30000);

private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
