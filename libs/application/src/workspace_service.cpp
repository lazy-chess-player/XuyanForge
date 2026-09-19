#include "xuyan/application/workspace_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

WorkspaceService::WorkspaceService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::EntityPage> WorkspaceService::openAndList(int limit) {
    return search({}, {}, 0, limit);
}

xuyan::domain::Result<xuyan::domain::EntityPage> WorkspaceService::search(
    const std::string& query, const std::string& kind, int offset, int limit) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.searchEntities(query, kind, offset, limit);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::create(
    const std::string& command_id, xuyan::domain::WorldEntity entity) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.createEntity(command_id, std::move(entity));
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::save(
    const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.saveEntity(command_id, std::move(entity), expected_revision);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::load(const std::string& entity_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.loadEntity(entity_id);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::remove(
    const std::string& command_id, const std::string& entity_id, int expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.deleteEntity(command_id, entity_id, expected_revision);
}

xuyan::domain::Result<xuyan::domain::EntityMergeResult> WorkspaceService::merge(
    const std::string& command_id, const std::string& source_id, int source_expected_revision,
    const std::string& target_id, int target_expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.mergeEntities(command_id, source_id, source_expected_revision, target_id, target_expected_revision);
}

xuyan::domain::Result<xuyan::domain::EntityMergeResult> WorkspaceService::splitMerge(
    const std::string& command_id, const std::string& merge_id,
    int source_expected_revision, int target_expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.splitEntityMerge(command_id, merge_id, source_expected_revision, target_expected_revision);
}

} // namespace xuyan::application
