#include "xuyan/providers/model_protocol.h"

#include "xuyan/package/json.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace xuyan::providers {
namespace {

using xuyan::domain::ErrorCode;
using xuyan::package::JsonValue;

xuyan::domain::Error protocolError(std::string message) {
    return {ErrorCode::validation_failed, std::move(message), false, "保留原始响应并暂停该推演回合"};
}

const JsonValue* member(const JsonValue* value, std::string_view name) {
    return value != nullptr && value->isObject() ? value->find(name) : nullptr;
}

std::string stringValue(const JsonValue* value) {
    return value != nullptr && value->isString() ? value->string() : std::string{};
}

int integerValue(const JsonValue* value) {
    if (value == nullptr || !value->isInteger()) return 0;
    return static_cast<int>(std::clamp<std::int64_t>(value->integer(), 0, 2'000'000'000));
}

std::string appendPath(std::string endpoint, std::string_view path) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    if (endpoint.ends_with(path)) return endpoint;
    endpoint.append(path);
    return endpoint;
}

JsonValue schemaValue(const std::string& schema) {
    auto parsed = xuyan::package::parseJson(schema);
    if (!parsed.ok() || !parsed.value->isObject()) throw std::runtime_error("结构化输出 Schema 不是有效 JSON 对象");
    return std::move(*parsed.value);
}

JsonValue requestBody(ProviderProtocol protocol, const StructuredGenerationRequest& request) {
    const auto schema = schemaValue(request.json_schema);
    const JsonValue user_message(JsonValue::Object{{"role", "user"}, {"content", request.prompt}});
    if (protocol == ProviderProtocol::openai_responses) {
        const JsonValue input_part(JsonValue::Object{{"type", "input_text"}, {"text", request.prompt}});
        const JsonValue input_message(JsonValue::Object{
            {"role", "user"}, {"content", JsonValue::Array{input_part}}});
        const JsonValue format(JsonValue::Object{{"type", "json_schema"}, {"name", "actor_intent"},
            {"strict", true}, {"schema", schema}});
        return JsonValue::Object{
            {"model", request.model_id},
            {"input", JsonValue::Array{input_message}},
            {"text", JsonValue::Object{{"format", format}}},
            {"max_output_tokens", request.max_output_tokens}, {"stream", request.stream}, {"store", false}};
    }
    if (protocol == ProviderProtocol::openai_compatible) {
        const JsonValue format(JsonValue::Object{{"name", "actor_intent"}, {"strict", true}, {"schema", schema}});
        return JsonValue::Object{
            {"model", request.model_id},
            {"messages", JsonValue::Array{user_message}},
            {"response_format", JsonValue::Object{{"type", "json_schema"}, {"json_schema", format}}},
            {"max_tokens", request.max_output_tokens}, {"stream", request.stream}};
    }
    if (protocol == ProviderProtocol::anthropic_messages) {
        const JsonValue format(JsonValue::Object{{"type", "json_schema"}, {"schema", schema}});
        return JsonValue::Object{
            {"model", request.model_id}, {"max_tokens", request.max_output_tokens},
            {"messages", JsonValue::Array{user_message}},
            {"output_config", JsonValue::Object{{"format", format}}},
            {"stream", request.stream}};
    }
    const JsonValue part(JsonValue::Object{{"text", request.prompt}});
    const JsonValue content(JsonValue::Object{{"role", "user"}, {"parts", JsonValue::Array{part}}});
    return JsonValue::Object{
        {"contents", JsonValue::Array{content}},
        {"generationConfig", JsonValue::Object{{"responseMimeType", "application/json"},
            {"responseJsonSchema", schema}, {"maxOutputTokens", request.max_output_tokens}, {"candidateCount", 1}}}};
}

ProviderGenerationResult parseOpenAiResponses(const JsonValue& root) {
    ProviderGenerationResult result;
    const auto status = stringValue(member(&root, "status"));
    const auto* usage = member(&root, "usage");
    result.input_tokens = integerValue(member(usage, "input_tokens"));
    result.output_tokens = integerValue(member(usage, "output_tokens"));
    if (status == "incomplete") { result.status = "incomplete"; result.failure_kind = "output_truncated"; return result; }
    if (status == "failed" || status == "cancelled") { result.status = "error"; result.failure_kind = status; return result; }
    const auto* output = member(&root, "output");
    if (output != nullptr && output->isArray()) {
        for (const auto& item : output->array()) {
            const auto* content = member(&item, "content");
            if (content == nullptr || !content->isArray()) continue;
            for (const auto& part : content->array()) {
                const auto type = stringValue(member(&part, "type"));
                if (type == "output_text") result.text += stringValue(member(&part, "text"));
                else if (type == "refusal") { result.status = "refusal"; result.failure_kind = "refusal"; }
            }
        }
    }
    if (result.status.empty()) result.status = status == "completed" && !result.text.empty() ? "completed" : "error";
    if (result.status == "error" && result.failure_kind.empty()) result.failure_kind = "empty_output";
    return result;
}

ProviderGenerationResult parseCompatible(const JsonValue& root) {
    ProviderGenerationResult result;
    const auto* usage = member(&root, "usage");
    result.input_tokens = integerValue(member(usage, "prompt_tokens"));
    result.output_tokens = integerValue(member(usage, "completion_tokens"));
    const auto* choices = member(&root, "choices");
    if (choices == nullptr || !choices->isArray() || choices->array().empty()) {
        result.status = "error"; result.failure_kind = member(&root, "error") ? "provider_error" : "empty_output"; return result;
    }
    const auto& choice = choices->array().front();
    const auto finish = stringValue(member(&choice, "finish_reason"));
    result.text = stringValue(member(member(&choice, "message"), "content"));
    if (finish == "length") { result.status = "incomplete"; result.failure_kind = "output_truncated"; }
    else if (finish == "content_filter") { result.status = "refusal"; result.failure_kind = "content_filter"; }
    else if (!result.text.empty()) result.status = "completed";
    else { result.status = "error"; result.failure_kind = "empty_output"; }
    return result;
}

ProviderGenerationResult parseAnthropic(const JsonValue& root) {
    ProviderGenerationResult result;
    const auto reason = stringValue(member(&root, "stop_reason"));
    const auto* usage = member(&root, "usage");
    result.input_tokens = integerValue(member(usage, "input_tokens"));
    result.output_tokens = integerValue(member(usage, "output_tokens"));
    const auto* content = member(&root, "content");
    if (content != nullptr && content->isArray())
        for (const auto& block : content->array()) if (stringValue(member(&block, "type")) == "text") result.text += stringValue(member(&block, "text"));
    if (reason == "refusal") { result.status = "refusal"; result.failure_kind = "refusal"; }
    else if (reason == "max_tokens" || reason == "model_context_window_exceeded") {
        result.status = "incomplete"; result.failure_kind = "output_truncated";
    } else if ((reason == "end_turn" || reason == "stop_sequence") && !result.text.empty()) result.status = "completed";
    else { result.status = "error"; result.failure_kind = reason.empty() ? "empty_output" : reason; }
    return result;
}

ProviderGenerationResult parseGemini(const JsonValue& root) {
    ProviderGenerationResult result;
    const auto* usage = member(&root, "usageMetadata");
    result.input_tokens = integerValue(member(usage, "promptTokenCount"));
    result.output_tokens = integerValue(member(usage, "candidatesTokenCount"));
    const auto blocked = stringValue(member(member(&root, "promptFeedback"), "blockReason"));
    if (!blocked.empty() && blocked != "BLOCK_REASON_UNSPECIFIED") {
        result.status = "refusal"; result.failure_kind = "prompt_blocked"; return result;
    }
    const auto* candidates = member(&root, "candidates");
    if (candidates == nullptr || !candidates->isArray() || candidates->array().empty()) {
        result.status = "error"; result.failure_kind = "empty_output"; return result;
    }
    const auto& candidate = candidates->array().front();
    const auto finish = stringValue(member(&candidate, "finishReason"));
    const auto* parts = member(member(&candidate, "content"), "parts");
    if (parts != nullptr && parts->isArray())
        for (const auto& part : parts->array()) result.text += stringValue(member(&part, "text"));
    if (finish == "MAX_TOKENS") { result.status = "incomplete"; result.failure_kind = "output_truncated"; }
    else if (finish == "SAFETY" || finish == "RECITATION" || finish == "BLOCKLIST" || finish == "PROHIBITED_CONTENT" || finish == "SPII") {
        result.status = "refusal"; result.failure_kind = "content_filter";
    } else if ((finish == "STOP" || finish.empty()) && !result.text.empty()) result.status = "completed";
    else { result.status = "error"; result.failure_kind = finish.empty() ? "empty_output" : "provider_finish_reason"; }
    return result;
}

} // namespace

xuyan::domain::Result<ProviderHttpRequest> buildProviderRequest(
    ProviderProtocol protocol, const StructuredGenerationRequest& request) {
    if (request.endpoint.empty() || request.model_id.empty() || request.prompt.empty()
        || request.json_schema.empty() || request.max_output_tokens < 1 || request.max_output_tokens > 1'000'000)
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError("模型请求缺少端点、模型、提示词、Schema 或有效输出上限"));
    try {
        ProviderHttpRequest result;
        result.headers["Content-Type"] = "application/json";
        if (protocol == ProviderProtocol::openai_responses) {
            result.url = appendPath(request.endpoint, "/responses"); result.credential_header = "Authorization: Bearer";
        } else if (protocol == ProviderProtocol::openai_compatible) {
            result.url = appendPath(request.endpoint, "/chat/completions"); result.credential_header = "Authorization: Bearer";
        } else if (protocol == ProviderProtocol::anthropic_messages) {
            result.url = appendPath(request.endpoint, "/v1/messages"); result.credential_header = "x-api-key";
            result.headers["anthropic-version"] = "2023-06-01";
        } else {
            auto endpoint = request.endpoint;
            while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
            result.url = endpoint + "/v1beta/models/" + request.model_id + ":generateContent";
            result.credential_header = "x-goog-api-key";
        }
        result.body = xuyan::package::writeJson(requestBody(protocol, request));
        return xuyan::domain::Result<ProviderHttpRequest>::success(std::move(result));
    } catch (const std::exception& exception) {
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError(exception.what()));
    }
}

xuyan::domain::Result<ProviderGenerationResult> parseProviderResponse(
    ProviderProtocol protocol, std::string_view response_json) {
    auto parsed = xuyan::package::parseJson(response_json);
    if (!parsed.ok()) return xuyan::domain::Result<ProviderGenerationResult>::failure(*parsed.error);
    if (!parsed.value->isObject()) return xuyan::domain::Result<ProviderGenerationResult>::failure(protocolError("提供商响应根值不是对象"));
    ProviderGenerationResult result;
    if (protocol == ProviderProtocol::openai_responses) result = parseOpenAiResponses(*parsed.value);
    else if (protocol == ProviderProtocol::openai_compatible) result = parseCompatible(*parsed.value);
    else if (protocol == ProviderProtocol::anthropic_messages) result = parseAnthropic(*parsed.value);
    else result = parseGemini(*parsed.value);
    return xuyan::domain::Result<ProviderGenerationResult>::success(std::move(result));
}

ProviderGenerationResult classifyProviderFailure(int http_status, bool timed_out, bool cancelled) {
    ProviderGenerationResult result; result.status = "error";
    if (cancelled) { result.failure_kind = "cancelled"; return result; }
    if (timed_out) { result.failure_kind = "timeout_unknown"; result.retryable = false; return result; }
    if (http_status == 401 || http_status == 403) result.failure_kind = "authentication";
    else if (http_status == 408 || http_status == 429 || http_status >= 500) { result.failure_kind = "transient_http"; result.retryable = true; }
    else if (http_status >= 400) result.failure_kind = "invalid_request";
    else result.failure_kind = "network";
    return result;
}

xuyan::domain::Result<ProviderProtocol> protocolForProviderKind(std::string_view kind) {
    if (kind == "openai" || kind == "deepseek")
        return xuyan::domain::Result<ProviderProtocol>::success(ProviderProtocol::openai_responses);
    if (kind == "openai-compatible" || kind == "local")
        return xuyan::domain::Result<ProviderProtocol>::success(ProviderProtocol::openai_compatible);
    if (kind == "anthropic")
        return xuyan::domain::Result<ProviderProtocol>::success(ProviderProtocol::anthropic_messages);
    if (kind == "gemini")
        return xuyan::domain::Result<ProviderProtocol>::success(ProviderProtocol::gemini_generate_content);
    return xuyan::domain::Result<ProviderProtocol>::failure(protocolError("提供商类型没有可用的请求协议映射"));
}

} // namespace xuyan::providers
