#include "xuyan/providers/model_protocol.h"

#include "xuyan/package/json.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace xuyan::providers {
namespace {

using xuyan::domain::ErrorCode;
using xuyan::package::JsonValue;

/* 功能：包装报文校验失败。参数：message 为按值接收的中文原因，不含正文。
 * 返回：不可自动重试的领域错误。失败：分配异常可传播。
 * 副作用：只构造值对象，在调用线程执行，不保存原始响应。 */
xuyan::domain::Error protocolError(std::string message) {
    return {ErrorCode::validation_failed, std::move(message), false, "检查提供商配置和响应格式，暂停当前模型任务"};
}

/* 功能：借用可选对象中的成员。参数：value 为观察指针，可空；name 为键名视图。
 * 返回：成员观察指针，缺失或非对象为 nullptr；有效期随 value，修改对象后需重新查找。
 * 失败：无业务异常。副作用：只读，在调用线程执行。 */
const JsonValue* member(const JsonValue* value, std::string_view name) {
    return value != nullptr && value->isObject() ? value->find(name) : nullptr;
}

/* 功能：提取可选字符串字段。参数：value 为调用期间有效的观察指针，可空。
 * 返回：独立字符串副本；缺失、非字符串或空字符串均返回空串。
 * 失败：复制分配异常可传播。副作用：只读节点，不改变响应状态。 */
std::string stringValue(const JsonValue* value) {
    return value != nullptr && value->isString() ? value->string() : std::string{};
}

/* 功能：归一厂商词元计数。参数：value 为观察指针，可空，整数单位为词元。
 * 返回：整数截断到 0—20 亿；缺失或类型不符为 0。
 * 失败：无。副作用：只读，不估算缺失账单，在调用线程执行。 */
int integerValue(const JsonValue* value) {
    if (value == nullptr || !value->isInteger()) return 0;
    return static_cast<int>(std::clamp<std::int64_t>(value->integer(), 0, 2'000'000'000));
}

/* 功能：拼接报文路径且避免重复后缀。参数：endpoint 按值接收并修改；path 为非空路径视图。
 * 返回：去除端点末尾斜线后的完整地址。失败：分配异常可传播，不校验 URL 合法性。
 * 副作用：仅修改地址副本，不联网；path 只借用至调用结束。 */
std::string appendPath(std::string endpoint, std::string_view path) {
    while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
    if (endpoint.ends_with(path)) return endpoint;
    endpoint.append(path);
    return endpoint;
}

/* 功能：读取结构化输出约束对象。参数：schema 为只读 JSON 文本，借用至返回。
 * 返回：拥有全部节点的 Schema。失败：非法 JSON 或非对象抛出中文 runtime_error，分配异常传播。
 * 副作用：只分配内存；不保存约束文本，不联网。 */
JsonValue schemaValue(const std::string& schema) {
    auto parsed = xuyan::package::parseJson(schema);
    if (!parsed.ok() || !parsed.value->isObject()) throw std::runtime_error("结构化输出约束不是有效 JSON 对象");
    return std::move(*parsed.value);
}

/* 功能：根据已校验协议组装厂商原生结构化正文。
 * 参数：protocol 为四类受支持枚举；request 为调用期间有效的只读冻结请求。
 * 返回：独立 JSON 对象，包含提示词、模型与协议支持的生成配置。
 * 失败：Schema 无效或分配失败抛异常，由 buildProviderRequest 转为领域错误。
 * 副作用：不联网、不读取凭据；字段名和内部协议值不翻译。 */
JsonValue requestBody(ProviderProtocol protocol, const StructuredGenerationRequest& request) {
    const auto schema = schemaValue(request.json_schema);
    const JsonValue user_message(JsonValue::Object{{"role", "user"}, {"content", request.prompt}});
    if (protocol == ProviderProtocol::openai_responses) {
        const JsonValue input_part(JsonValue::Object{{"type", "input_text"}, {"text", request.prompt}});
        const JsonValue input_message(JsonValue::Object{
            {"role", "user"}, {"content", JsonValue::Array{input_part}}});
        const JsonValue format(JsonValue::Object{{"type", "json_schema"}, {"name", "actor_intent"},
            {"strict", true}, {"schema", schema}});
        JsonValue::Object body{
            {"model", request.model_id},
            {"input", JsonValue::Array{input_message}},
            {"text", JsonValue::Object{{"format", format}}},
            {"max_output_tokens", request.max_output_tokens}, {"stream", request.stream}, {"store", false}};
        // Responses 使用 reasoning.effort；默认不发该字段，不能误用聊天接口的 thinking.type。
        if (request.generation.reasoning_effort != "provider_default")
            body.emplace("reasoning", JsonValue::Object{{"effort", request.generation.reasoning_effort}});
        return body;
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

/* 功能：归一 Responses 的文本块、状态和用量。
 * 参数：root 为已解析对象，调用内借用；缺失字段按空值处理。
 * 返回：独立结果；拒绝优先于正常完成，截断/失败不强行作为完整正文。
 * 失败：分配异常传播，厂商失败保留内部状态。副作用：只读，无日志或事实写入。 */
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

/* 功能：归一 Chat Completions 的首个候选，不合并多个选择。
 * 参数：root 为调用内借用的响应对象。返回：文本、停止原因及用量的独立结果。
 * 失败：缺失候选/正文返回 error；分配异常传播。
 * 副作用：只读；截断和内容过滤分别保留 incomplete/refusal，调用线程同步执行。 */
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

/* 功能：合并 Messages 文本块并判定结束状态。
 * 参数：root 为调用内借用的对象，仅接收 type=text 的块。
 * 返回：独立文本与用量，正常结束且有正文才完成。
 * 失败：未知停止原因/空输出为 error，分配异常传播。
 * 副作用：只读对象，不输出正文，不创建后台任务。 */
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

/* 功能：归一 generateContent 首个候选与安全拦截。
 * 参数：root 为调用内借用对象，提示词拦截优先于候选解析。
 * 返回：独立结果，保留截断/拒绝分类和厂商用量。
 * 失败：空输出或非正常结束为 error，分配异常传播。
 * 副作用：只读，不将候选提升为事实，在调用线程执行。 */
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
    if (protocol != ProviderProtocol::openai_responses && protocol != ProviderProtocol::openai_compatible
        && protocol != ProviderProtocol::anthropic_messages && protocol != ProviderProtocol::gemini_generate_content)
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError("模型请求协议无效"));
    const auto config = xuyan::domain::validateProviderGenerationConfig(request.generation, request.provider_kind);
    if (!config.ok()) return xuyan::domain::Result<ProviderHttpRequest>::failure(*config.error);
    if (request.generation.reasoning_effort != "provider_default"
        && (request.provider_kind != "deepseek" || protocol != ProviderProtocol::openai_responses))
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError("此协议不支持显式思考强度，未构造发送请求"));
    if (request.endpoint.empty() || request.model_id.empty() || request.prompt.empty()
        || request.json_schema.empty() || request.max_output_tokens < 1 || request.max_output_tokens > 1'000'000)
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError("模型请求缺少端点、模型、提示词、结构化约束或有效输出上限"));
    try {
        ProviderHttpRequest result;
        result.headers["Content-Type"] = "application/json";
        // 协议层只标记凭据应放入的请求头，不在请求对象中保存密钥。
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
    } catch (const std::exception&) {
        // 异常文字可能来自底层库；使用固定中文，避免泄露提示词或英文实现细节。
        return xuyan::domain::Result<ProviderHttpRequest>::failure(protocolError("无法构造模型请求，请检查结构化约束及可用内存"));
    }
}

xuyan::domain::Result<ProviderGenerationResult> parseProviderResponse(
    ProviderProtocol protocol, std::string_view response_json) {
    if (protocol != ProviderProtocol::openai_responses && protocol != ProviderProtocol::openai_compatible
        && protocol != ProviderProtocol::anthropic_messages && protocol != ProviderProtocol::gemini_generate_content)
        return xuyan::domain::Result<ProviderGenerationResult>::failure(protocolError("模型响应协议无效"));
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
    // 超时可能已被上游计费，因此不能据此自动重试。
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
