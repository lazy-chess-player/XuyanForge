#pragma once

#include "xuyan/application/credential_store.h"

namespace xuyan::platform {

class SystemCredentialStore final : public xuyan::application::ICredentialStore {
public:
    /** @brief 在系统凭据存储中写入指定引用对应的密钥。 */
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override;
    /** @brief 从系统凭据存储读取引用对应的密钥。 */
    xuyan::domain::Result<std::string> get(const std::string& reference) override;
    /** @brief 删除系统凭据存储中的指定密钥引用。 */
    xuyan::domain::Result<bool> remove(const std::string& reference) override;
};

} // namespace xuyan::platform
