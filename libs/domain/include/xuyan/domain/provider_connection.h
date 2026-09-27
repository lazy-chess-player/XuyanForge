#pragma once

#include "xuyan/domain/scenario.h"

#include <string>

namespace xuyan::domain {

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
