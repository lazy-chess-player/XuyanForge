#include "xuyan/application/provider_connection_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

using xuyan::domain::ErrorCode;
using xuyan::domain::ProviderConnection;
using xuyan::domain::Result;

ProviderConnectionService::ProviderConnectionService(std::filesystem::path database_path, ICredentialStore& credentials)
    : database_path_(std::move(database_path)), credentials_(credentials) {}

Result<std::vector<ProviderConnection>> ProviderConnectionService::list() {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listProviderConnections(); }
    catch (const std::exception& exception) { return Result<std::vector<ProviderConnection>>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<ProviderConnection> ProviderConnectionService::save(
    const std::string& command_id, ProviderConnection connection, int expected_revision,
    std::optional<std::string> new_secret) {
    if (command_id.empty()) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "命令标识不能为空", false, "重新提交"});
    if (connection.id.empty()) connection.id = "provider-" + xuyan::domain::sha256(command_id).substr(0, 16);
    connection.credential_ref = "XuyanForge/provider/" + connection.id;
    auto validated = xuyan::domain::validateProviderConnection(std::move(connection));
    if (!validated.ok()) return validated;
    if (new_secret && (new_secret->empty() || new_secret->size() > 65536)) return Result<ProviderConnection>::failure(
        {ErrorCode::validation_failed, "API Key 不能为空且不能超过 64 KiB", false, "重新输入凭据"});

    std::optional<std::string> previous_secret;
    if (new_secret) {
        auto previous = credentials_.get(validated.value->credential_ref);
        if (previous.ok()) previous_secret = *previous.value;
        auto stored = credentials_.put(validated.value->credential_ref, *new_secret);
        if (!stored.ok()) return Result<ProviderConnection>::failure(*stored.error);
    }
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto saved = repository.saveProviderConnection(command_id, *validated.value, expected_revision);
        if (!saved.ok() && new_secret) {
            if (previous_secret) credentials_.put(validated.value->credential_ref, *previous_secret);
            else credentials_.remove(validated.value->credential_ref);
        }
        return saved;
    } catch (const std::exception& exception) {
        if (new_secret) {
            if (previous_secret) credentials_.put(validated.value->credential_ref, *previous_secret);
            else credentials_.remove(validated.value->credential_ref);
        }
        return Result<ProviderConnection>::failure({ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"});
    }
}

Result<bool> ProviderConnectionService::hasCredential(const std::string& connection_id) {
    try {
        auto loaded = xuyan::storage::WorkspaceRepository(database_path_).loadProviderConnection(connection_id);
        if (!loaded.ok()) return Result<bool>::failure(*loaded.error);
        auto secret = credentials_.get(loaded.value->credential_ref);
        if (!secret.ok()) {
            if (secret.error->code == ErrorCode::missing_context) return Result<bool>::success(false);
            return Result<bool>::failure(*secret.error);
        }
        return Result<bool>::success(!secret.value->empty());
    } catch (const std::exception& exception) { return Result<bool>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

Result<bool> ProviderConnectionService::remove(
    const std::string& command_id, const std::string& connection_id, int expected_revision) {
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto loaded = repository.loadProviderConnection(connection_id);
        if (!loaded.ok()) return Result<bool>::failure(*loaded.error);
        auto removed = repository.deleteProviderConnection(command_id, connection_id, expected_revision);
        if (!removed.ok()) return Result<bool>::failure(*removed.error);
        auto erased = credentials_.remove(loaded.value->credential_ref);
        if (!erased.ok()) return Result<bool>::failure(*erased.error);
        return Result<bool>::success(true);
    } catch (const std::exception& exception) { return Result<bool>::failure(
        {ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
