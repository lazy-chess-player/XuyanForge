#include "xuyan/application/character_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

CharacterService::CharacterService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> CharacterService::openAndList() {
    return list();
}

xuyan::domain::Result<xuyan::domain::CharacterBlueprint> CharacterService::create(
    const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.createBlueprint(command_id, std::move(blueprint));
}

xuyan::domain::Result<xuyan::domain::CharacterBlueprint> CharacterService::save(
    const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.saveBlueprint(command_id, std::move(blueprint), expected_version);
}

xuyan::domain::Result<xuyan::domain::CharacterBlueprint> CharacterService::load(
    const std::string& blueprint_id, int version) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.loadBlueprint(blueprint_id, version);
}

xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> CharacterService::list() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listBlueprints();
}

} // namespace xuyan::application
