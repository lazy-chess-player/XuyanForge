#pragma once

#include "xuyan/domain/scenario.h"

#include <map>
#include <string>
#include <string_view>

namespace xuyan::providers {

enum class ProviderProtocol { openai_responses, openai_compatible, anthropic_messages, gemini_generate_content };

struct StructuredGenerationRequest {
    std::string endpoint;
    std::string model_id;
    std::string prompt;
    std::string json_schema;
    int max_output_tokens{2048};
    bool stream{false};
};

struct ProviderHttpRequest {
    std::string method{"POST"};
    std::string url;
    std::map<std::string, std::string, std::less<>> headers;
    std::string credential_header;
    std::string body;
};

struct ProviderGenerationResult {
    std::string status;
    std::string text;
    std::string failure_kind;
    bool retryable{false};
    int input_tokens{0};
    int output_tokens{0};
};

/** @brief 将统一生成请求转换成指定厂商协议的 HTTP 请求；不附加明文密钥。 */
xuyan::domain::Result<ProviderHttpRequest> buildProviderRequest(
    ProviderProtocol protocol, const StructuredGenerationRequest& request);
/** @brief 解析厂商响应并归一化正文、错误类型和用量。 */
xuyan::domain::Result<ProviderGenerationResult> parseProviderResponse(
    ProviderProtocol protocol, std::string_view response_json);
/** @brief 根据 HTTP 状态、超时和取消标志归类请求失败。 */
ProviderGenerationResult classifyProviderFailure(int http_status, bool timed_out, bool cancelled);
/** @brief 将连接的提供商类型映射到受支持的协议。 */
xuyan::domain::Result<ProviderProtocol> protocolForProviderKind(std::string_view kind);

} // namespace xuyan::providers
