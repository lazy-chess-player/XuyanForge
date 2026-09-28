#include "xuyan/application/provider_connection_service.h"
#include "xuyan/application/provider_generation_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/remote_extraction_processor.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/package/json.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"

#include <QCoreApplication>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

/** @brief 在离开配置流程前覆盖并清空暂存凭据，避免输出或长时间保留密钥。 */
void clearSecret(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

/** @brief 从标准输入接收连接凭据，只写系统凭据管理器并保存非秘密连接元数据。 */
int configureDeepSeek(const std::filesystem::path& database) {
    std::string secret;
    if (!std::getline(std::cin, secret) || secret.empty()) {
        std::cerr << "No credential received on stdin.\n";
        return 2;
    }
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::application::ProviderConnectionService service(database, credentials);
    int revision = 0;
    auto connections = service.list();
    if (!connections.ok()) {
        clearSecret(secret);
        std::cerr << "Unable to open provider catalog.\n";
        return 3;
    }
    for (const auto& item : *connections.value)
        if (item.id == "provider-deepseek") revision = item.revision;
    xuyan::domain::ProviderConnection connection;
    connection.id = "provider-deepseek";
    connection.name = "DeepSeek";
    connection.kind = "deepseek";
    connection.endpoint = "https://api.deepseek.com";
    connection.default_model = "deepseek-flash";
    connection.data_policy = "remote_allowed";
    connection.enabled = true;
    auto saved = service.save("configure-deepseek-v1", std::move(connection), revision, secret);
    clearSecret(secret);
    if (!saved.ok()) {
        std::cerr << "DeepSeek configuration failed: " << saved.error->message << '\n';
        return 4;
    }
    std::cout << "DeepSeek connection configured; credential stored outside the workspace.\n";
    return 0;
}

/** @brief 显式执行不含小说正文的单次结构化自检，只打印状态和用量统计。 */
int testProvider(const std::filesystem::path& database, const std::string& connection_id) {
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::providers::QtProviderTransport transport;
    xuyan::application::ProviderGenerationService service(database, credentials, transport);
    auto report = service.testStructuredGeneration(connection_id, 45000);
    if (!report.ok()) {
        std::cerr << "Provider test failed before completion: " << report.error->message << '\n';
        return 5;
    }
    std::cout << "status=" << report.value->status
              << " provider=" << report.value->provider_kind
              << " model=" << report.value->model_id
              << " json_valid=" << (report.value->json_valid ? "true" : "false")
              << " input_tokens=" << report.value->input_tokens
              << " output_tokens=" << report.value->output_tokens
              << " elapsed_ms=" << report.value->elapsed_ms;
    if (!report.value->failure_kind.empty()) std::cout << " failure=" << report.value->failure_kind;
    std::cout << '\n';
    return report.value->status == "completed" && report.value->json_valid ? 0 : 6;
}

/** @brief 为人工授权的小说抽样限制发送次数、输入字节与输出上限，不保存或输出凭据。 */
class SampleTransport final : public xuyan::application::IProviderTransport {
public:
    /** @brief 指定本次人工验证剩余请求数，不以失败或超时为理由增加额度。 */
    explicit SampleTransport(int maximum_requests) : maximum_requests_(maximum_requests) {}
    /** @brief 仅允许官方低价模型端点、约定请求次数、每次20,000请求字节及2400输出token。 */
    xuyan::domain::Result<xuyan::application::ProviderTransportResponse> send(
        const xuyan::providers::ProviderHttpRequest& request,
        const std::string& credential, int timeout_ms) override {
        auto body = xuyan::package::parseJson(request.body);
        const auto* model = body.ok() ? body.value->find("model") : nullptr;
        const auto* limit = body.ok() ? body.value->find("max_output_tokens") : nullptr;
        if (attempts_ >= maximum_requests_ || request.url != "https://api.deepseek.com/responses"
            || request.body.size() > 20000 || model == nullptr || !model->isString()
            || model->string() != "deepseek-flash" || limit == nullptr || !limit->isInteger()
            || limit->integer() != 2400) {
            return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
                {xuyan::domain::ErrorCode::validation_failed, "抽样请求超出授权次数、模型或长度限制", false,
                 "不要自动重试，重新核对授权范围"});
        }
        // 请求体字节上限是保守边界，不冒充精确分词；费用按提供商返回的实际用量估算。
        // 只在此抽样工具明确关闭思考，避免把有限输出预算都用于思考；不暗改正式任务配置。
        auto sample_request = request;
        body.value->object()["reasoning"] = xuyan::package::JsonValue::Object{{"effort", "none"}};
        sample_request.body = xuyan::package::writeJson(*body.value);
        if (sample_request.body.size() > 20000)
            return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
                {xuyan::domain::ErrorCode::validation_failed, "抽样请求正文超出上限", false, "缩小片段后重新确认"});
        ++attempts_;
        auto response = transport_.send(sample_request, credential, timeout_ms);
        if (response.ok() && response.value->http_status >= 200 && response.value->http_status < 300) {
            auto generated = xuyan::providers::parseProviderResponse(
                xuyan::providers::ProviderProtocol::openai_responses, response.value->body);
            if (generated.ok()) {
                input_tokens_ += generated.value->input_tokens;
                output_tokens_ += generated.value->output_tokens;
            }
        }
        return response;
    }
    /** @brief 返回已到传输边界的请求次数，未知或失败请求也计数。 */
    int attempts() const noexcept { return attempts_; }
    /** @brief 返回响应中已知的累计输入token，不猜测超时请求的账单。 */
    int inputTokens() const noexcept { return input_tokens_; }
    /** @brief 返回响应中已知的累计输出token，不输出模型正文。 */
    int outputTokens() const noexcept { return output_tokens_; }
private:
    xuyan::providers::QtProviderTransport transport_;
    int attempts_{0};
    int input_tokens_{0};
    int output_tokens_{0};
    int maximum_requests_{0};
};

/** @brief 只读抽样连接元数据，不迁移数据库、不读取旧快照或恢复旧测试资料。 */
xuyan::domain::Result<xuyan::domain::ProviderConnection> readSampleConnection(
    const std::filesystem::path& database_path) {
    using Result = xuyan::domain::Result<xuyan::domain::ProviderConnection>;
    const auto encoded = database_path.u8string();
    const std::string filename(encoded.begin(), encoded.end());
    sqlite3* raw = nullptr;
    const auto opened = sqlite3_open_v2(filename.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, &sqlite3_close);
    sqlite3_stmt* statement = nullptr;
    if (opened != SQLITE_OK || sqlite3_prepare_v2(database.get(),
        "SELECT id,name,kind,endpoint,default_model,credential_ref,data_policy,enabled,deleted "
        "FROM provider_connection WHERE id='provider-deepseek'", -1, &statement, nullptr) != SQLITE_OK) {
        if (statement != nullptr) sqlite3_finalize(statement);
        return Result::failure({xuyan::domain::ErrorCode::storage_error,
            "无法只读抽样连接元数据", false, "使用已经配置连接的工作区"});
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement, &sqlite3_finalize);
    if (sqlite3_step(query.get()) != SQLITE_ROW) return Result::failure(
        {xuyan::domain::ErrorCode::missing_context, "抽样连接不存在", false, "先配置模型连接"});
    std::array<std::string, 7> fields;
    for (int index = 0; index < 7; ++index) {
        const auto* value = sqlite3_column_text(query.get(), index);
        fields[static_cast<std::size_t>(index)] = value == nullptr ? "" : reinterpret_cast<const char*>(value);
    }
    xuyan::domain::ProviderConnection connection;
    connection.id = fields[0]; connection.name = fields[1]; connection.kind = fields[2];
    connection.endpoint = fields[3]; connection.default_model = fields[4];
    connection.credential_ref = fields[5]; connection.data_policy = fields[6];
    connection.enabled = sqlite3_column_int(query.get(), 7) != 0;
    connection.deleted = sqlite3_column_int(query.get(), 8) != 0;
    if (connection.credential_ref != "XuyanForge/provider/provider-deepseek"
        || !connection.enabled || connection.deleted || connection.data_policy != "remote_allowed")
        return Result::failure({xuyan::domain::ErrorCode::validation_failed,
            "抽样连接的凭据引用或发送策略不符合授权范围", false, "在程序中检查连接配置"});
    return xuyan::domain::validateProviderConnection(std::move(connection));
}

/** @brief 在全新本机目录抽取外部小说指定章节或默认两处，每个样本最多发送一次2000码点。 */
int verifyNovel(const std::filesystem::path& output_directory,
                const std::filesystem::path& connection_database,
                const std::filesystem::path& novel_path, std::size_t first_sample,
                std::optional<std::size_t> explicit_chapter = std::nullopt) {
    if (std::filesystem::exists(output_directory) || !std::filesystem::is_regular_file(novel_path)
        || !std::filesystem::is_regular_file(connection_database)) {
        std::cerr << "验证需要全新输出目录、已有模型连接工作区和外部小说文件。\n";
        return 7;
    }
    auto connection = readSampleConnection(connection_database);
    if (!connection.ok() || connection.value->endpoint != "https://api.deepseek.com"
        || connection.value->kind != "deepseek" || connection.value->default_model != "deepseek-flash") {
        std::cerr << "未找到已配置的官方抽样连接；没有发送请求。\n";
        return 7;
    }
    if (!std::filesystem::create_directory(output_directory)) return 7;
    const auto database = output_directory / "workspace.sqlite";
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::application::ProviderConnectionService providers(database, credentials);
    auto saved = providers.save("sample-connection", *connection.value, 0, std::nullopt);
    auto available = providers.hasCredential("provider-deepseek");
    if (!saved.ok() || !available.ok() || !*available.value) {
        std::cerr << "系统凭据不可用；没有发送请求，请在程序中安全配置连接。\n";
        return 7;
    }
    xuyan::application::SourceImportService sources(database);
    auto source = sources.importTextFile("sample-full-local-source", novel_path, "1", "paid-validation");
    if (!source.ok()) {
        std::cerr << "外部小说导入失败：" << (source.error->code == xuyan::domain::ErrorCode::validation_failed
            ? source.error->message : "工作区存储不可用") << '\n';
        return 8;
    }
    if (source.value->chapters.empty()) return 8;
    const auto count = source.value->chapters.size();
    const std::vector<std::size_t> selected = explicit_chapter
        ? std::vector<std::size_t>{*explicit_chapter}
        : std::vector<std::size_t>{std::min<std::size_t>(1, count - 1), count / 2};
    /** @brief 判断人工选择的零起始章节索引是否落在已导入来源之外。 */
    const auto outside_source = [count](std::size_t index) { return index >= count; };
    if (first_sample >= selected.size()
        || std::any_of(selected.begin(), selected.end(), outside_source)) {
        std::cerr << "抽样章节超出本地小说章节范围；没有发送请求。\n";
        return 8;
    }
    SampleTransport transport(static_cast<int>(selected.size() - first_sample));
    xuyan::application::ExtractionJobService jobs(database);
    xuyan::application::RemoteExtractionProcessor processor(database, credentials, transport);
    bool complete = true;
    for (std::size_t index = first_sample; index < selected.size(); ++index) {
        const auto& chapter = source.value->chapters[selected[index]];
        const auto end = std::min(chapter.end_codepoint, chapter.start_codepoint + 2000);
        auto fragment = sources.evidenceText(source.value->id, chapter.start_codepoint, end);
        if (!fragment.ok()) return 8;
        const auto name = "sample-" + std::to_string(index + 1);
        const auto path = output_directory / (name + ".txt");
        { std::ofstream output(path, std::ios::binary);
          output.write(fragment.value->data(), static_cast<std::streamsize>(fragment.value->size()));
          if (!output) return 8; }
        auto sample = sources.importTextFile(name, path, "1", "paid-validation");
        if (!sample.ok()) return 8;
        auto job = jobs.create(name + "-job", sample.value->id, 2000, 0, 1, 2400, "provider-deepseek");
        if (!job.ok()) return 8;
        auto processed = processor.processNext(job.value->id);
        auto report = jobs.qualityReport(job.value->id, 100);
        std::cout << "抽样=" << index + 1 << " 章节区间=" << selected[index] + 1
                  << " 原文码点=" << end - chapter.start_codepoint
                  << " 步骤已完成=" << (processed.ok() ? processed.value->completed_steps : 0)
                  << " 候选=" << (report.ok() ? report.value->sampled_candidates : 0)
                  << " 证据可定位=" << (report.ok() ? report.value->evidence_valid : 0) << '\n';
        // 未知/失败不自动重发；即使没有完成，传输边界的计数也不会重置。
        if (!processed.ok() || processed.value->status == "needs_attention") {
            complete = false;
            std::cout << "抽样未完成，已停止后续请求；详请在本机工作区查看，不自动重试。\n";
            break;
        }
    }
    std::cout << "实际请求=" << transport.attempts() << " 已知输入token=" << transport.inputTokens()
              << " 已知输出token=" << transport.outputTokens()
              << " 已知用量高峰价估算元=" << (transport.inputTokens() * 2.0 + transport.outputTokens() * 8.0) / 1000000.0
              << "（全未命中缓存估算，不是实际账单；未知请求用量不计入）\n";
    return complete ? 0 : 10;
}

} // namespace

/** @brief 解析显式命令并调度连接配置、自检或有界小说抽样，未选择测试不会发送请求。 */
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() < 3) {
        std::cerr << "用法：xuyanforge_provider_cli <配置连接|测试连接|小说抽样命令> <工作区> [参数]\n";
        return 1;
    }
    const auto database = std::filesystem::path(arguments.at(2).toStdWString());
    if ((arguments.at(1) == QStringLiteral("verify-novel")
         || arguments.at(1) == QStringLiteral("verify-novel-second")) && arguments.size() == 5) {
        try {
            return verifyNovel(database, std::filesystem::path(arguments.at(3).toStdWString()),
                               std::filesystem::path(arguments.at(4).toStdWString()),
                               arguments.at(1) == QStringLiteral("verify-novel-second") ? 1 : 0);
        } catch (const std::exception&) {
            std::cerr << "小说抽样发生内部错误，已停止；不自动重试或输出私人详情。\n";
            return 9;
        }
    }
    if (arguments.at(1) == QStringLiteral("verify-novel-chapter") && arguments.size() == 6) {
        bool parsed = false;
        const auto chapter = arguments.at(5).toULongLong(&parsed);
        if (!parsed || chapter < 1 || chapter > std::numeric_limits<std::size_t>::max()) {
            std::cerr << "章节序号必须是从1开始的正整数；没有发送请求。\n";
            return 7;
        }
        try {
            return verifyNovel(database, std::filesystem::path(arguments.at(3).toStdWString()),
                               std::filesystem::path(arguments.at(4).toStdWString()), 0,
                               static_cast<std::size_t>(chapter - 1));
        } catch (const std::exception&) {
            std::cerr << "小说章节抽样发生内部错误，已停止；不自动重试或输出私人详情。\n";
            return 9;
        }
    }
    if (arguments.at(1) == QStringLiteral("configure-deepseek")) return configureDeepSeek(database);
    if (arguments.at(1) == QStringLiteral("test")) {
        const auto id = arguments.size() >= 4 ? arguments.at(3).toStdString() : std::string{"provider-deepseek"};
        return testProvider(database, id);
    }
    std::cerr << "Unknown provider command.\n";
    return 1;
}
