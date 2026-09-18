#include "xuyan/application/provider_generation_service.h"

#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <chrono>

namespace xuyan::application {
namespace {

using xuyan::domain::ErrorCode;

xuyan::domain::Error configurationError(std::string message, std::string action) {
    return {ErrorCode::validation_failed, std::move(message), false, std::move(action)};
}

void clearSecret(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

} // namespace

ProviderGenerationService::ProviderGenerationService(
    std::filesystem::path database_path, ICredentialStore& credentials, IProviderTransport& transport)
    : database_path_(std::move(database_path)), credentials_(credentials), transport_(transport) {}

xuyan::domain::Result<xuyan::providers::ProviderGenerationResult> ProviderGenerationService::generate(
    const std::string& connection_id, const std::string& prompt,
    const std::string& json_schema, int max_output_tokens, int timeout_ms) {
    if (timeout_ms < 1000 || timeout_ms > 300000) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(
        configurationError("模型请求超时必须在 1—300 秒之间", "调整超时设置"));
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto connection = repository.loadProviderConnection(connection_id);
        if (!connection.ok()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(*connection.error);
        if (!connection.value->enabled) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(
            configurationError("模型连接已停用", "启用连接后重试"));
        if (connection.value->default_model.empty()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(
            configurationError("模型连接未指定默认模型", "填写实际模型标识"));
        if (connection.value->kind != "local" && connection.value->data_policy != "remote_allowed")
            return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(
                configurationError("当前数据策略不允许发送到远程提供商", "明确设为 remote_allowed"));

        auto protocol = xuyan::providers::protocolForProviderKind(connection.value->kind);
        if (!protocol.ok()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(*protocol.error);
        xuyan::providers::StructuredGenerationRequest generation;
        generation.endpoint = connection.value->endpoint;
        generation.model_id = connection.value->default_model;
        generation.prompt = prompt;
        generation.json_schema = json_schema;
        generation.max_output_tokens = max_output_tokens;
        auto request = xuyan::providers::buildProviderRequest(*protocol.value, generation);
        if (!request.ok()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(*request.error);

        std::string secret;
        if (connection.value->kind != "local") {
            auto credential = credentials_.get(connection.value->credential_ref);
            if (!credential.ok()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(*credential.error);
            secret = std::move(*credential.value);
        }
        auto response = transport_.send(*request.value, secret, timeout_ms);
        clearSecret(secret);
        if (!response.ok()) return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(*response.error);
        if (response.value->http_status < 200 || response.value->http_status >= 300
            || response.value->timed_out || response.value->cancelled) {
            return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::success(
                xuyan::providers::classifyProviderFailure(response.value->http_status,
                                                           response.value->timed_out,
                                                           response.value->cancelled));
        }
        return xuyan::providers::parseProviderResponse(*protocol.value, response.value->body);
    } catch (const std::exception& exception) {
        return xuyan::domain::Result<xuyan::providers::ProviderGenerationResult>::failure(
            {ErrorCode::storage_error, exception.what(), true, "检查工作区与网络后重试"});
    }
}

xuyan::domain::Result<ProviderTestReport> ProviderGenerationService::testStructuredGeneration(
    const std::string& connection_id, int timeout_ms) {
    const std::string prompt =
        "这是连接自检。请只输出 JSON 对象：ok 必须为 true，provider 填写当前提供商名称。不要输出解释。";
    const std::string schema =
        R"({"type":"object","properties":{"ok":{"type":"boolean","const":true},"provider":{"type":"string"}},"required":["ok","provider"],"additionalProperties":false})";
    const auto started = std::chrono::steady_clock::now();
    auto generated = generate(connection_id, prompt, schema, 128, timeout_ms);
    const auto elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    if (!generated.ok()) return xuyan::domain::Result<ProviderTestReport>::failure(*generated.error);

    ProviderTestReport report;
    report.connection_id = connection_id;
    report.status = generated.value->status;
    report.failure_kind = generated.value->failure_kind;
    report.input_tokens = generated.value->input_tokens;
    report.output_tokens = generated.value->output_tokens;
    report.elapsed_ms = elapsed;
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto connection = repository.loadProviderConnection(connection_id);
        if (connection.ok()) {
            report.provider_kind = connection.value->kind;
            report.model_id = connection.value->default_model;
        }
    } catch (...) {}
    if (generated.value->status == "completed") {
        auto parsed = xuyan::package::parseJson(generated.value->text, 8, 100);
        if (parsed.ok() && parsed.value->isObject()) {
            const auto* ok = parsed.value->find("ok");
            const auto* provider = parsed.value->find("provider");
            report.json_valid = ok != nullptr && ok->isBool() && ok->boolean()
                && provider != nullptr && provider->isString() && !provider->string().empty();
        }
        if (!report.json_valid) {
            report.status = "error";
            report.failure_kind = "schema_validation";
        }
    }
    return xuyan::domain::Result<ProviderTestReport>::success(std::move(report));
}

} // namespace xuyan::application
