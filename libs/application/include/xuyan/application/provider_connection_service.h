#pragma once

#include "xuyan/application/credential_store.h"
#include "xuyan/domain/provider_connection.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

class ProviderConnectionService {
public:
    /** @brief 绑定工作区元数据与系统凭据存储，两者保持分离。 */
    ProviderConnectionService(std::filesystem::path database_path, ICredentialStore& credentials);

    /** @brief 列出不含秘密值的模型连接配置。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> list();
    /** @brief 按期望修订保存连接；有新秘密时先存凭据，数据库失败则回滚凭据。 */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> save(
        const std::string& command_id, xuyan::domain::ProviderConnection connection,
        int expected_revision, std::optional<std::string> new_secret);
    /** @brief 只报告指定连接是否有凭据，不向界面返回秘密值。 */
    xuyan::domain::Result<bool> hasCredential(const std::string& connection_id);
    /** @brief 软删除连接元数据并移除其系统凭据。 */
    xuyan::domain::Result<bool> remove(const std::string& command_id,
                                       const std::string& connection_id, int expected_revision);

private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
};

} // namespace xuyan::application
