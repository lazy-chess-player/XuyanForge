#include "xuyan/domain/provider_connection.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<ProviderConnection> validateProviderConnection(ProviderConnection connection) {
    constexpr std::array kinds{
        std::string_view{"openai"}, std::string_view{"openai-compatible"},
        std::string_view{"anthropic"}, std::string_view{"gemini"}, std::string_view{"local"},
    };
    if (connection.name.empty() || connection.name.size() > 256) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "连接名称不能为空或过长", false, "修改连接名称"});
    if (std::find(kinds.begin(), kinds.end(), connection.kind) == kinds.end()) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "提供商类型无效", false, "选择受支持的提供商类型"});
    if (connection.endpoint.size() > 2048 || connection.endpoint.find_first_of("\r\n\t ") != std::string::npos
        || (connection.endpoint.rfind("https://", 0) != 0 && connection.endpoint.rfind("http://", 0) != 0)) {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "端点必须是有效的 HTTP(S) URL 且不能包含空白", false, "修正端点 URL"});
    }
    if (connection.kind != "local" && connection.endpoint.rfind("https://", 0) != 0) {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "远程提供商必须使用 HTTPS", false, "改用 HTTPS 端点"});
    }
    if (connection.default_model.size() > 256) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "模型标识过长", false, "修改模型标识"});
    if (connection.data_policy != "remote_allowed" && connection.data_policy != "local_only") {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "数据策略无效", false, "选择 remote_allowed 或 local_only"});
    }
    if (connection.data_policy == "local_only" && connection.kind != "local") {
        return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "local_only 策略不能绑定远程提供商", false, "改用本地连接或调整策略"});
    }
    if (connection.data_policy == "local_only") {
        const auto authority = connection.endpoint.substr(connection.endpoint.find("//") + 2);
        const auto boundary = authority.find_first_of(":/");
        const auto host = authority.substr(0, boundary);
        const bool ipv6_loopback = authority.rfind("[::1]", 0) == 0
            && (authority.size() == 5 || authority[5] == ':' || authority[5] == '/');
        const bool loopback = host == "localhost" || host == "127.0.0.1" || ipv6_loopback;
        if (!loopback) return Result<ProviderConnection>::failure(
            {ErrorCode::validation_failed, "local_only 连接必须使用回环地址", false, "改用 localhost、127.0.0.1 或 [::1]"});
    }
    return Result<ProviderConnection>::success(std::move(connection));
}

} // namespace xuyan::domain
