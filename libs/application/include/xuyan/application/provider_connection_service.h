#pragma once

#include "xuyan/application/credential_store.h"
#include "xuyan/domain/provider_connection.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

class ProviderConnectionService {
public:
    ProviderConnectionService(std::filesystem::path database_path, ICredentialStore& credentials);

    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> list();
    xuyan::domain::Result<xuyan::domain::ProviderConnection> save(
        const std::string& command_id, xuyan::domain::ProviderConnection connection,
        int expected_revision, std::optional<std::string> new_secret);
    xuyan::domain::Result<bool> hasCredential(const std::string& connection_id);
    xuyan::domain::Result<bool> remove(const std::string& command_id,
                                       const std::string& connection_id, int expected_revision);

private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
};

} // namespace xuyan::application

