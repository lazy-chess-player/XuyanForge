#include "xuyan/domain/provider_connection.h"

#include "xuyan/domain/hash.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace xuyan::domain {


Result<ProviderGenerationConfig> validateProviderGenerationConfig(
    ProviderGenerationConfig config, std::string_view provider_kind) {
    constexpr std::array efforts{std::string_view{"provider_default"}, std::string_view{"none"},
        std::string_view{"low"}, std::string_view{"high"}, std::string_view{"max"}};
    if (config.output_format != "provider_schema_v1"
        || std::find(efforts.begin(), efforts.end(), config.reasoning_effort) == efforts.end()
        || (!provider_kind.empty() && provider_kind != "deepseek" && config.reasoning_effort != "provider_default"))
        return Result<ProviderGenerationConfig>::failure(
            {ErrorCode::validation_failed, "生成思考强度或输出模式不受当前提供商支持", false, "保留默认或显式选择已支持配置并新建任务"});
    return Result<ProviderGenerationConfig>::success(std::move(config));
}

Result<ProviderConnection> validateProviderConnection(ProviderConnection connection) {
    constexpr std::array kinds{
        std::string_view{"openai"}, std::string_view{"openai-compatible"},
        std::string_view{"anthropic"}, std::string_view{"gemini"}, std::string_view{"deepseek"},
        std::string_view{"local"},
    };
    if (connection.name.empty() || connection.name.size() > 256) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "连接名称不能为空或过长", false, "修改连接名称"});
    if (std::find(kinds.begin(), kinds.end(), connection.kind) == kinds.end()) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "提供商类型无效", false, "选择受支持的提供商类型"});
    if (connection.endpoint.size() > 2048 || connection.endpoint.find_first_of("\r\n\t ") != std::string::npos
        || (connection.endpoint.rfind("https://", 0) != 0 && connection.endpoint.rfind("http://", 0) != 0)) {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "接口地址须使用网络地址格式且不能包含空白", false, "修正接口地址"});
    }
    if (connection.kind != "local" && connection.endpoint.rfind("https://", 0) != 0) {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "远程提供商必须使用加密网络地址", false, "改用加密网络接口地址"});
    }
    if (connection.default_model.size() > 256) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "模型标识过长", false, "修改模型标识"});
    if (connection.data_policy != "remote_allowed" && connection.data_policy != "local_only") {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "数据策略无效", false, "选择允许远程发送或仅本地处理策略"});
    }
    if (connection.data_policy == "local_only" && connection.kind != "local") {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "仅本地处理策略不能绑定远程提供商", false, "改用本地连接或调整策略"});
    }
    if (connection.data_policy == "local_only") {
        const auto authority = connection.endpoint.substr(connection.endpoint.find("//") + 2);
        const auto boundary = authority.find_first_of(":/");
        const auto host = authority.substr(0, boundary);
        const bool ipv6_loopback = authority.rfind("[::1]", 0) == 0
            && (authority.size() == 5 || authority[5] == ':' || authority[5] == '/');
        const bool loopback = host == "localhost" || host == "127.0.0.1" || ipv6_loopback;
        if (!loopback) return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "仅本地处理连接必须使用回环地址", false, "改用 localhost、127.0.0.1 或 [::1]"});
    }
    return Result<ProviderConnection>::success(std::move(connection));
}

std::string providerConnectionFingerprint(const ProviderConnection& connection) {
    std::string material;
    /* 功能：将一个配置字段以长度前缀写入指纹材料，避免分隔符导致字段歧义。
     * 参数：field为调用期间借用的配置字节串，不含明文凭据。返回：无。
     * 失败：字符串扩容可抛异常。副作用：修改局部material，回调不逃逸当前函数。
     */
    const auto append = [&material](std::string_view field) {
        material += std::to_string(field.size());
        material.push_back(':');
        material.append(field);
    };
    append(connection.id);
    append(connection.kind);
    append(connection.endpoint);
    append(connection.default_model);
    append(connection.credential_ref);
    append(connection.data_policy);
    append(std::to_string(connection.revision));
    append(connection.enabled ? "1" : "0");
    append(connection.deleted ? "1" : "0");
    return sha256(material);
}

} // namespace xuyan::domain
