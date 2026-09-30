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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

/* 功能：通过 CLI 固定翻译上下文取得简体中文文案，保留未来翻译入口。
 * 参数：message 为非空、零结尾的中文静态词条，借用至返回。
 * 返回：拥有型 UTF-8 文案，未加载译文时使用原词条。
 * 失败：分配异常传播。副作用：只读 Qt 翻译表，在 CLI 主线程使用，不处理用户输入/协议值。 */
std::string cliText(const char* message) {
    return QCoreApplication::translate("ProviderCli", message).toStdString();
}

/* 功能：映射生成状态为中文显示，不改变内部状态判定。
 * 参数：status 为调用内借用的内部状态字节视图，可空。
 * 返回：拥有型中文标签，未知值统一显示未知状态。失败：分配异常传播。
 * 副作用：只读映射，主线程使用，不把未经映射的厂商状态回显为选项。 */
std::string statusLabel(std::string_view status) {
    if (status == "completed") return cliText("已完成");
    if (status == "incomplete") return cliText("输出不完整");
    if (status == "refusal") return cliText("提供商拒绝");
    if (status == "error") return cliText("失败");
    return cliText("未知状态");
}

/* 功能：映射厂商类型为可读名称。参数：kind 为调用内借用的内部连接类型，可空。
 * 返回：中文类型名或厂商真实品牌名，未知值为未知提供商。失败：分配异常传播。
 * 副作用：不修改连接或发送协议，仅主线程显示。 */
std::string providerLabel(std::string_view kind) {
    if (kind == "deepseek") return "DeepSeek";
    if (kind == "openai") return "OpenAI";
    if (kind == "anthropic") return "Anthropic";
    if (kind == "gemini") return "Gemini";
    if (kind == "local") return cliText("本地模型");
    if (kind == "openai-compatible") return cliText("兼容接口");
    return cliText("未知提供商");
}

/* 功能：把内部传输/厂商失败分类映射为中文摘要，不输出响应正文。
 * 参数：kind 为调用内有效的失败标识，可空。返回：拥有型中文标签，未知分类明确回退。
 * 失败：分配异常传播。副作用：不重试、不修改状态；在 CLI 主线程显示。 */
std::string failureLabel(std::string_view kind) {
    if (kind == "cancelled") return cliText("已取消");
    if (kind == "timeout_unknown") return cliText("超时，调用结果未知");
    if (kind == "authentication") return cliText("凭据认证失败");
    if (kind == "transient_http") return cliText("提供商暂时不可用");
    if (kind == "invalid_request") return cliText("请求无效");
    if (kind == "network") return cliText("网络失败");
    if (kind == "output_truncated") return cliText("输出被截断");
    if (kind == "refusal" || kind == "content_filter" || kind == "prompt_blocked") return cliText("内容被拒绝");
    if (kind == "empty_output") return cliText("没有有效输出");
    if (kind == "provider_error" || kind == "failed") return cliText("提供商执行失败");
    return cliText("未知失败原因");
}

/* 功能：覆盖并清空 CLI 暂存凭据，缩短明文在内存中的存留。
 * 参数：secret 为调用方拥有的明文字节串引用，可空，清理后 size 为 0。
 * 返回：无。失败：无。副作用：Windows 用系统不可优化擦除；其他平台为尽力覆盖，不清除其他副本。
 * 线程：同步执行，不保存引用；调用方须确保没有其他线程读取该字符串。 */
void clearSecret(std::string& secret) noexcept {
#ifdef _WIN32
    if (!secret.empty()) SecureZeroMemory(secret.data(), secret.size());
#else
    std::fill(secret.begin(), secret.end(), '\0');
#endif
    secret.clear();
}

/* 凭据栈帧的清理守卫，观察本地字符串，不拥有系统凭据；仅在 configureDeepSeek 使用，不复制或逃逸。 */
struct SecretCleanup {
    /* 借用已构造的秘密字符串；它必须后于守卫销毁，默认值无，析构负责覆盖活跃字节。 */
    std::string& secret;
    /* 功能：在所有返回及异常路径清理暂存凭据。参数：无。返回：释放暂存明文，无返回值。
     * 失败：无。副作用：覆盖并清空 secret，不删除持久化凭据；在配置调用线程执行。 */
    ~SecretCleanup() noexcept { clearSecret(secret); }
};

/* 功能：显式接收一行标准输入凭据并保存官方连接，构造本身不调用模型。
 * 参数：database 为工作区数据库路径，借用至结束，服务可创建/迁移该工作区。
 * 返回：成功 0；无凭据 2；连接列表失败 3；保存失败 4。
 * 失败：服务错误仅显示中文原因；意外异常传播到 main，守卫仍清理暂存密钥。
 * 副作用：读取标准输入、更新系统凭据和非秘密连接元数据、打印状态；不写密钥到数据库。
 * 线程：CLI 主线程同步执行，服务及密钥只在本函数栈帧存活。 */
int configureDeepSeek(const std::filesystem::path& database) {
    std::string secret;
    SecretCleanup cleanup{secret};
    if (!std::getline(std::cin, secret) || secret.empty()) {
        std::cerr << cliText("未从标准输入收到凭据。\n");
        return 2;
    }
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::application::ProviderConnectionService service(database, credentials);
    int revision = 0;
    auto connections = service.list();
    if (!connections.ok()) {
        clearSecret(secret);
        std::cerr << cliText("无法打开模型连接目录。\n");
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
        std::cerr << cliText("DeepSeek 连接配置失败：") << saved.error->message << '\n';
        return 4;
    }
    std::cout << cliText("DeepSeek 连接已配置；凭据仅保存在系统凭据管理器中。\n");
    return 0;
}

/* 功能：显式执行不含小说的单次结构化连接自检，不能作为整书质量验收。
 * 参数：database 为借用的工作区路径；connection_id 为已有连接稳定标识，不为空，借用至返回。
 * 返回：正常且结构合法为 0；服务调用失败为 5；未完成或结构无效为 6。
 * 失败：服务错误打印中文摘要，意外异常由 main 兜底；不自动重试。
 * 副作用：读取系统密钥并产生一次可能计费的请求，服务可更新自检状态；只打印统计，不打印模型正文。
 * 线程：CLI 主线程，Qt 传输嵌套处理本线程事件；局部服务/传输随返回销毁。 */
int testProvider(const std::filesystem::path& database, const std::string& connection_id) {
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::providers::QtProviderTransport transport;
    xuyan::application::ProviderGenerationService service(database, credentials, transport);
    auto report = service.testStructuredGeneration(connection_id, 45000);
    if (!report.ok()) {
        std::cerr << cliText("模型连接自检未完成：") << report.error->message << '\n';
        return 5;
    }
    std::cout << cliText("状态=") << statusLabel(report.value->status)
              << cliText(" 提供商=") << providerLabel(report.value->provider_kind)
              << cliText(" 模型=") << report.value->model_id
              << cliText(" 结构有效=") << (report.value->json_valid ? cliText("是") : cliText("否"))
              << cliText(" 输入词元=") << report.value->input_tokens
              << cliText(" 输出词元=") << report.value->output_tokens
              << cliText(" 耗时毫秒=") << report.value->elapsed_ms;
    if (!report.value->failure_kind.empty()) std::cout << cliText(" 失败原因=") << failureLabel(report.value->failure_kind);
    std::cout << '\n';
    return report.value->status == "completed" && report.value->json_valid ? 0 : 6;
}

/* 人工启动的受限抽样传输器，拥有无跨调用网络资源的 Qt 适配器及本次计数。
 * 本类是显式 CLI 流程的额度守卫，不代表用户已授予新发送权限；同一实例只能串行使用。
 * 失败/未知计费尝试仍计数，销毁不会持久化或恢复额度，不保存明文凭据。 */
class SampleTransport final : public xuyan::application::IProviderTransport {
public:
    /* 功能：冻结本次剩余请求额度。参数：maximum_requests 为允许发送次数，非负；0/负值不允许发送。
     * 返回：初始化计数为 0。失败：无。副作用：不发送、不读凭据，实例须在调用线程串行使用。 */
    explicit SampleTransport(int maximum_requests) : maximum_requests_(maximum_requests) {}
    /* 功能：核对官方抽样端点、模型和本次额度后显式发送，工具固定非思考模式。
     * 参数：request 为借用请求，须为指定官方地址及模型，正文最多 20000 字节，输出上限 2400 词元；
     *       credential 为仅在调用内借用的系统密钥；timeout_ms 为正整数毫秒。
     * 返回：传输结果或校验错误，成功 HTTP 响应中可解析的用量累计计入统计。
     * 失败：超次数/模型/字节/输出限制在发送前拒绝；传输异常可传播，已计的尝试不回退。
     * 副作用：可能产生计费请求，最多重写请求副本的思考字段；不重发、不打印响应，不保存秘密。
     * 线程：在 CLI 线程串行调用，局部正文和密钥视图仅在 send 内有效，返回后无悬挂回调。 */
    xuyan::domain::Result<xuyan::application::ProviderTransportResponse> send(
        const xuyan::providers::ProviderHttpRequest& request,
        const std::string& credential, int timeout_ms) override {
        // 在 JSON 解析及复制之前限制输入，异常或拒绝不会消耗发送额度。
        if (request.body.size() > 20000)
            return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
                {xuyan::domain::ErrorCode::validation_failed, cliText("抽样请求正文超出上限"), false,
                 cliText("缩小片段后重新确认")});
        auto body = xuyan::package::parseJson(request.body);
        const auto* model = body.ok() ? body.value->find("model") : nullptr;
        const auto* limit = body.ok() ? body.value->find("max_output_tokens") : nullptr;
        if (attempts_ >= maximum_requests_ || request.url != "https://api.deepseek.com/responses"
            || request.body.size() > 20000 || model == nullptr || !model->isString()
            || model->string() != "deepseek-flash" || limit == nullptr || !limit->isInteger()
            || limit->integer() != 2400) {
            return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
                {xuyan::domain::ErrorCode::validation_failed, cliText("抽样请求超出授权次数、模型或长度限制"), false,
                 cliText("不要自动重试，重新核对授权范围")});
        }
        // 请求体字节上限是保守边界，不冒充精确分词；费用按提供商返回的实际用量估算。
        // 只在此抽样工具明确关闭思考，避免把有限输出预算都用于思考；不暗改正式任务配置。
        auto sample_request = request;
        body.value->object()["reasoning"] = xuyan::package::JsonValue::Object{{"effort", "none"}};
        sample_request.body = xuyan::package::writeJson(*body.value);
        if (sample_request.body.size() > 20000)
            return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
                {xuyan::domain::ErrorCode::validation_failed, cliText("抽样请求正文超出上限"), false, cliText("缩小片段后重新确认")});
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
    /* 功能：查询本实例已进入传输边界的次数。参数：无。返回：从 0 起的累计次数，包括未知/失败。
     * 失败：无。副作用：只读，与 send 串行，计数不代表厂商确认的账单次数。 */
    int attempts() const noexcept { return attempts_; }
    /* 功能：查询本实例已解析的累计输入词元。参数：无。返回：非负 64 位计数，缺失/未知账单未补估。
     * 失败：无。副作用：只读，与 send 串行，不读取模型正文或系统凭据。 */
    std::int64_t inputTokens() const noexcept { return input_tokens_; }
    /* 功能：查询已解析的累计输出词元。参数：无。返回：非负 64 位计数，包括厂商用量定义中的思考词元。
     * 失败：无。副作用：只读，与 send 串行，未收到有效响应的用量不计入。 */
    std::int64_t outputTokens() const noexcept { return output_tokens_; }
private:
    /* 本实例拥有的同步 Qt 适配器，无持久 QObject；send 调用，销毁时无后台任务。 */
    xuyan::providers::QtProviderTransport transport_;
    /* 已进入网络边界的请求次数，初始 0，send 递增，异常也保留；不随失败重置。 */
    int attempts_{0};
    /* 已知输入词元总数，初始 0；send 仅从可解析成功响应累计，64 位避免多次计数溢出。 */
    std::int64_t input_tokens_{0};
    /* 已知输出词元总数，初始 0；send 累计、报告读取，不推断未知用量。 */
    std::int64_t output_tokens_{0};
    /* 本实例允许请求次数，成员初值 0，构造参数冻结；send 比较，不自动扩大。 */
    int maximum_requests_{0};
};

/* 功能：从已有工作区只读取得指定官方抽样连接，拒绝不符合固定引用/发送策略的配置。
 * 参数：database_path 为数据库文件路径，借用至返回；不得指向需要创建的数据库。
 * 返回：校验过的连接值（不含明文凭据）或领域错误。
 * 失败：打开/查询失败为存储错误，缺连接为上下文错误，策略或连接格式不符为校验错误；分配异常传播。
 * 副作用：只读连接元数据，不迁移数据库、读取小说/旧快照或读取密钥；SQL 为固定语句。
 * 线程：CLI 主线程创建自己的 SQLite 连接，语句先销毁再关闭连接，不跨线程共享句柄。 */
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
            cliText("无法只读抽样连接元数据"), false, cliText("使用已经配置连接的工作区")});
    }
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> query(statement, &sqlite3_finalize);
    const auto stepped = sqlite3_step(query.get());
    if (stepped == SQLITE_DONE) return Result::failure(
        {xuyan::domain::ErrorCode::missing_context, cliText("抽样连接不存在"), false, cliText("先配置模型连接")});
    if (stepped != SQLITE_ROW) return Result::failure(
        {xuyan::domain::ErrorCode::storage_error, cliText("读取抽样连接失败"), false, cliText("检查工作区是否可读")});
    // 七个非秘密文本列按 SQL 顺序复制为独立值；使用字节长度，不让内嵌零字节被静默截断。
    std::array<std::string, 7> fields;
    for (int index = 0; index < 7; ++index) {
        const auto* value = sqlite3_column_text(query.get(), index);
        if (value != nullptr) fields[static_cast<std::size_t>(index)].assign(
            reinterpret_cast<const char*>(value), static_cast<std::size_t>(sqlite3_column_bytes(query.get(), index)));
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
            cliText("抽样连接的凭据引用或发送策略不符合授权范围"), false, cliText("在程序中检查连接配置")});
    return xuyan::domain::validateProviderConnection(std::move(connection));
}

/* 功能：按显式 CLI 命令在全新目录建立抽样工作区，每个选中样本最多处理一次 2000 原文码点。
 * 参数：output_directory 为尚不存在的本机输出目录；connection_database 为已有连接数据库；
 *       novel_path 为外部小说文件，上述路径均借用至返回；first_sample 为选中列表的零起始索引，
 *       0 从首个样本开始、1 仅默认第二个样本；explicit_chapter 为来源章节零起始序号，默认空时选两处。
 * 返回：全选样本完成为 0；准备/连接错误 7；导入/证据/任务错误 8；处理失败或未知为 10。
 * 失败：目录非全新、输入非文件、连接不符/无密钥、章节越界均拒绝；文件及分配异常由 main 兜底。
 * 副作用：仅在此命令显式启动时复制小说到本机抽样工作区、写片段文件/数据库、发送有界真实请求；
 *       不写仓库资源，不打印原文/模型正文，不自动清除失败产物，供用户在本机检查。
 * 线程：CLI 主线程同步使用自己的服务和传输，失败/未知停止后续请求；函数返回前无在途回调。
 * 授权：工具限制不等于用户授权，开发重构不得为了验证本函数自行执行小说抽样命令。 */
int verifyNovel(const std::filesystem::path& output_directory,
                const std::filesystem::path& connection_database,
                const std::filesystem::path& novel_path, std::size_t first_sample,
                std::optional<std::size_t> explicit_chapter = std::nullopt) {
    if (std::filesystem::exists(output_directory) || !std::filesystem::is_regular_file(novel_path)
        || !std::filesystem::is_regular_file(connection_database)) {
        std::cerr << cliText("验证需要全新输出目录、已有模型连接工作区和外部小说文件。\n");
        return 7;
    }
    auto connection = readSampleConnection(connection_database);
    if (!connection.ok() || connection.value->endpoint != "https://api.deepseek.com"
        || connection.value->kind != "deepseek" || connection.value->default_model != "deepseek-flash") {
        std::cerr << cliText("未找到已配置的官方抽样连接；没有发送请求。\n");
        return 7;
    }
    if (!std::filesystem::create_directory(output_directory)) {
        std::cerr << cliText("无法创建全新抽样目录；没有发送请求。\n");
        return 7;
    }
    const auto database = output_directory / "workspace.sqlite";
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::application::ProviderConnectionService providers(database, credentials);
    auto saved = providers.save("sample-connection", *connection.value, 0, std::nullopt);
    auto available = providers.hasCredential("provider-deepseek");
    if (!saved.ok() || !available.ok() || !*available.value) {
        std::cerr << cliText("系统凭据不可用；没有发送请求，请在程序中安全配置连接。\n");
        return 7;
    }
    xuyan::application::SourceImportService sources(database);
    auto source = sources.importTextFile("sample-full-local-source", novel_path, "1", "paid-validation");
    if (!source.ok()) {
        std::cerr << cliText("外部小说导入失败：") << (source.error->code == xuyan::domain::ErrorCode::validation_failed
            ? source.error->message : cliText("工作区存储不可用")) << '\n';
        return 8;
    }
    if (source.value->chapters.empty()) {
        std::cerr << cliText("导入来源没有可抽样章节；没有发送请求。\n");
        return 8;
    }
    const auto count = source.value->chapters.size();
    const std::vector<std::size_t> selected = explicit_chapter
        ? std::vector<std::size_t>{*explicit_chapter}
        : std::vector<std::size_t>{std::min<std::size_t>(1, count - 1), count / 2};
    /* 功能：判定人工选择章节是否越界。参数：index 为零起始章节索引；按值捕获 count 为章节总数。
     * 返回：索引不小于总数为真。失败：无。副作用：只读计数，不访问正文；同步调用、不保存到服务。 */
    const auto outside_source = [count](std::size_t index) { return index >= count; };
    if (first_sample >= selected.size()
        || std::any_of(selected.begin(), selected.end(), outside_source)) {
        std::cerr << cliText("抽样章节超出本地小说章节范围；没有发送请求。\n");
        return 8;
    }
    SampleTransport transport(static_cast<int>(selected.size() - first_sample));
    xuyan::application::ExtractionJobService jobs(database);
    xuyan::application::RemoteExtractionProcessor processor(database, credentials, transport);
    bool complete = true;
    for (std::size_t index = first_sample; index < selected.size(); ++index) {
        const auto& chapter = source.value->chapters[selected[index]];
        // 以码点差额计算有界末端，不把字节数当字符数，也避免起点相加溢出。
        const auto end = chapter.start_codepoint + std::min<std::size_t>(
            chapter.end_codepoint - chapter.start_codepoint, 2000);
        auto fragment = sources.evidenceText(source.value->id, chapter.start_codepoint, end);
        if (!fragment.ok()) {
            std::cerr << cliText("读取抽样证据失败，已停止后续请求。\n");
            return 8;
        }
        const auto name = "sample-" + std::to_string(index + 1);
        const auto path = output_directory / (name + ".txt");
        { std::ofstream output(path, std::ios::binary);
          output.write(fragment.value->data(), static_cast<std::streamsize>(fragment.value->size()));
          output.close();
          if (!output) {
              std::cerr << cliText("写入抽样片段失败，已停止后续请求。\n");
              return 8;
          } }
        auto sample = sources.importTextFile(name, path, "1", "paid-validation");
        if (!sample.ok()) {
            std::cerr << cliText("导入抽样片段失败，已停止后续请求。\n");
            return 8;
        }
        auto job = jobs.create(name + "-job", sample.value->id, 2000, 0, 1, 2400, "provider-deepseek");
        if (!job.ok()) {
            std::cerr << cliText("创建抽样任务失败，已停止后续请求。\n");
            return 8;
        }
        auto processed = processor.processNext(job.value->id);
        auto report = jobs.qualityReport(job.value->id, 100);
        std::cout << cliText("抽样=") << index + 1 << cliText(" 章节区间=") << selected[index] + 1
                  << cliText(" 原文码点=") << end - chapter.start_codepoint
                  << cliText(" 步骤已完成=") << (processed.ok() ? processed.value->completed_steps : 0)
                  << cliText(" 候选=") << (report.ok() ? report.value->sampled_candidates : 0)
                  << cliText(" 证据可定位=") << (report.ok() ? report.value->evidence_valid : 0) << '\n';
        // 未知/失败不自动重发；即使没有完成，传输边界的计数也不会重置。
        if (!processed.ok() || processed.value->status == "needs_attention") {
            complete = false;
            std::cout << cliText("抽样未完成，已停止后续请求；详情请在本机工作区查看，不自动重试。\n");
            break;
        }
    }
    // 沿用工具原有固定费率，只作历史口径估算，不据此承诺当前价格或未知请求账单。
    std::cout << cliText("实际请求=") << transport.attempts() << cliText(" 已知输入词元=") << transport.inputTokens()
              << cliText(" 已知输出词元=") << transport.outputTokens()
              << cliText(" 工具固定费率估算元=") << (transport.inputTokens() * 2.0 + transport.outputTokens() * 8.0) / 1000000.0
              << cliText("（全未命中缓存估算，不是实际账单；未知请求用量不计入）\n");
    return complete ? 0 : 10;
}

} // namespace

/* 功能：建立 CLI 的 Qt 事件环境，按显式命令调度配置、自检或小说抽样。
 * 参数：argc 为进程实参数量，argv 为系统提供的参数数组；QCoreApplication 可处理并调整 Qt 自身参数，
 *       工作区及外部文件路径按系统宽字符串转换，命令名保留既有内部值，不接受密钥命令行参数。
 * 返回：用法错误 1，配置/自检/抽样返回各流程状态，意外异常统一 9；0 仅表示所选流程成功。
 * 失败：章节序号非法在零发送时返回 7；意外异常打印固定中文，不回显私人路径/正文或库异常细节。
 * 副作用：配置会修改系统凭据和元数据；只有自检/抽样命令才联网，未选择有效命令不创建工作区。
 * 线程与生命周期：CLI 主线程拥有 QCoreApplication，局部服务/网络资源在其销毁前结束；不自动重试。
 */
int main(int argc, char* argv[]) {
    try {
        QCoreApplication app(argc, argv);
        const auto arguments = app.arguments();
        if (arguments.size() < 3) {
            std::cerr << cliText("用法：xuyanforge_provider_cli <配置连接|测试连接|小说抽样命令> <工作区> [参数]\n");
            return 1;
        }
        const auto database = std::filesystem::path(arguments.at(2).toStdWString());
        if ((arguments.at(1) == QStringLiteral("verify-novel")
             || arguments.at(1) == QStringLiteral("verify-novel-second")) && arguments.size() == 5) {
            return verifyNovel(database, std::filesystem::path(arguments.at(3).toStdWString()),
                               std::filesystem::path(arguments.at(4).toStdWString()),
                               arguments.at(1) == QStringLiteral("verify-novel-second") ? 1 : 0);
        }
        if (arguments.at(1) == QStringLiteral("verify-novel-chapter") && arguments.size() == 6) {
            bool parsed = false;
            const auto chapter = arguments.at(5).toULongLong(&parsed);
            if (!parsed || chapter < 1 || chapter > std::numeric_limits<std::size_t>::max()) {
                std::cerr << cliText("章节序号必须是从1开始的正整数；没有发送请求。\n");
                return 7;
            }
            return verifyNovel(database, std::filesystem::path(arguments.at(3).toStdWString()),
                               std::filesystem::path(arguments.at(4).toStdWString()), 0,
                               static_cast<std::size_t>(chapter - 1));
        }
        if (arguments.at(1) == QStringLiteral("configure-deepseek")) return configureDeepSeek(database);
        if (arguments.at(1) == QStringLiteral("test")) {
            const auto id = arguments.size() >= 4 ? arguments.at(3).toStdString() : std::string{"provider-deepseek"};
            return testProvider(database, id);
        }
        std::cerr << cliText("未知的模型连接命令或参数数量不正确。\n");
        return 1;
    } catch (const std::exception&) {
        std::cerr << cliText("模型连接工具发生内部错误，已停止；不自动重试或输出私人详情。\n");
        return 9;
    }
}
