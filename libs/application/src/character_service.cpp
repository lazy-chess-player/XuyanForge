#include "xuyan/application/character_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

CharacterService::CharacterService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<bool> CharacterService::ensureSample() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto existing = repository.listBlueprints();
    if (!existing.ok()) return xuyan::domain::Result<bool>::failure(*existing.error);
    if (!existing.value->empty()) return xuyan::domain::Result<bool>::success(false);
    xuyan::domain::CharacterBlueprint linzhou;
    linzhou.id = "blueprint-linzhou";
    linzhou.name = "林舟";
    linzhou.summary = "谨慎、重承诺的遗物调查者。";
    linzhou.values = {"不以无辜者换取胜利"};
    linzhou.traits = {"谨慎", "重承诺", "面对权威会质疑"};
    linzhou.long_term_goal = "寻找失散的导师";
    linzhou.short_term_goal = "查明议和印章的来历";
    linzhou.speech_style = "短句，先问证据，很少主动暴露情绪";
    linzhou.abilities_json = "[{\"key\":\"echo\",\"description\":\"触碰物品感知过去残留\",\"cost\":\"消耗专注\"}]";
    linzhou.equipment = {"铜制指针"};
    linzhou.background = "长期从事遗物调查。";
    linzhou.private_notes = "害怕导师已背叛自己。";
    auto created = repository.createBlueprint("seed-blueprint-linzhou", std::move(linzhou));
    if (!created.ok()) return xuyan::domain::Result<bool>::failure(*created.error);
    return xuyan::domain::Result<bool>::success(true);
}

xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> CharacterService::openAndList() {
    auto seeded = ensureSample();
    if (!seeded.ok()) return xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>>::failure(*seeded.error);
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

