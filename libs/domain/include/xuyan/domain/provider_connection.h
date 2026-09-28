#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>

namespace xuyan::domain {

/** @brief 不含凭据的生成语义；默认沿用厂商思考行为，结构化输出模式采用当前已验证适配器。 */
struct ProviderGenerationConfig {
    std::string reasoning_effort{"provider_default"};
    std::string output_format{"provider_schema_v1"};
};

/** @brief 校验生成配置；显式思考强度目前只对 DeepSeek Responses 开放，其他厂商不能静默忽略。 */
Result<ProviderGenerationConfig> validateProviderGenerationConfig(
    ProviderGenerationConfig config, std::string_view provider_kind = {});

struct ProviderConnection {
    std::string id;
    std::string name;
    std::string kind;
    std::string endpoint;
    std::string default_model;
    std::string credential_ref;
    std::string data_policy{"remote_allowed"};
    bool enabled{true};
    bool deleted{false};
    int revision{0};
};

/** @brief 校验模型连接配置，拒绝无效提供商及不安全地址。 */
Result<ProviderConnection> validateProviderConnection(ProviderConnection connection);
/** @brief 计算不含密钥的连接配置指纹，用于识别任务执行期间的配置变化。 */
std::string providerConnectionFingerprint(const ProviderConnection& connection);

} // namespace xuyan::domain
