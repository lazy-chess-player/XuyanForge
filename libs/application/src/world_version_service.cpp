#include "xuyan/application/world_version_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

WorldVersionService::WorldVersionService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::WorldVersion> WorldVersionService::publish(
    const std::string& command_id, const std::string& world_id, const std::string& parent_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).publishWorldVersion(command_id, world_id, parent_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::WorldVersion>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> WorldVersionService::list(const std::string& world_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listWorldVersions(world_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::WorldVersion> WorldVersionService::load(const std::string& version_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).loadWorldVersion(version_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::WorldVersion>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> WorldVersionService::prepareSnapshot(
    const std::string& command_id, const std::string& world_version_id, std::int64_t story_time) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).createHistoricalSnapshot(
        command_id, world_version_id, story_time); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::HistoricalSnapshot>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
