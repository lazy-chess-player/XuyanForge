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
    /** @brief 释放传输适配器。 */
    virtual ~IProviderTransport() = default;
    /** @brief 发送已构建的模型请求；凭据只在此传输边界作为参数出现。 */
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
    /** @brief 绑定连接仓储、系统凭据和传输端口；构造本身不发送请求。 */
    ProviderGenerationService(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);

    /** @brief 校验连接策略与可选快照指纹后执行结构化生成，返回厂商协议结果。 */
    xuyan::domain::Result<xuyan::providers::ProviderGenerationResult> generate(
        const std::string& connection_id, const std::string& prompt,
        const std::string& json_schema, int max_output_tokens = 1024,
        int timeout_ms = 30000, const std::string& expected_connection_fingerprint = {},
        const xuyan::domain::ProviderGenerationConfig& generation_config = {});
    /** @brief 使用不含小说正文的合成提示词自检连接和结构化输出链路。 */
    xuyan::domain::Result<ProviderTestReport> testStructuredGeneration(
        const std::string& connection_id, int timeout_ms = 30000);

private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
