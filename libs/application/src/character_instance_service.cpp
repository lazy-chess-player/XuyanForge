#include "xuyan/application/character_instance_service.h"

#include "xuyan/application/character_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>

namespace xuyan::application {

CharacterInstanceService::CharacterInstanceService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::CharacterInstance> CharacterInstanceService::instantiate(
    const std::string& command_id, const std::string& blueprint_id, int blueprint_version,
    const std::string& world_version_id, const std::string& snapshot_id,
    const std::string& adaptation_json, const std::string& knowledge_policy) {
    auto adaptation = xuyan::package::parseJson(adaptation_json, 16, 2000);
    if (!adaptation.ok() || !adaptation.value->isObject()) return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "能力适配必须是 JSON 对象", false, "修正适配方案"});
    CharacterService cards(database_path_); auto blueprint = cards.load(blueprint_id, blueprint_version);
    if (!blueprint.ok()) return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(*blueprint.error);
    /* 本调用独占的实例草稿；标识/基线由输入填充，记忆默认 {}、冲突默认空、修订默认 0。
     * 以下扫描确定就绪状态，仓储成功创建后设修订 1；生命周期不超过本次调用，结果拥有转移后的字段。
     */
    xuyan::domain::CharacterInstance instance;
    instance.id = "character-instance-" + xuyan::domain::sha256(command_id).substr(0, 24);
    instance.blueprint_id = blueprint_id; instance.blueprint_version = blueprint_version;
    instance.world_version_id = world_version_id; instance.snapshot_id = snapshot_id;
    instance.name = blueprint.value->name; instance.adaptation_json = xuyan::package::writeJson(*adaptation.value);
    instance.knowledge_policy = knowledge_policy;
    /* 卡片能力读取采用相同有界解析；解析失败/非数组或非字符串 key 不登记能力冲突。
     * 仅以适配键存在性判断已映射，不把映射值解释为已验证的世界规则。
     */
    auto abilities = xuyan::package::parseJson(blueprint.value->abilities_json, 16, 2000);
    if (abilities.ok() && abilities.value->isArray()) {
        for (const auto& ability : abilities.value->array()) {
            const auto* key = ability.find("key");
            if (key != nullptr && key->isString() && adaptation.value->find(key->string()) == nullptr)
                instance.conflicts.push_back("能力未适配：" + key->string());
        }
    }
    for (const auto& equipment : blueprint.value->equipment)
        if (adaptation.value->find(equipment) == nullptr) instance.conflicts.push_back("装备未映射：" + equipment);
    instance.status = instance.conflicts.empty() ? "ready" : "needs_resolution";
    try { return xuyan::storage::WorkspaceRepository(database_path_).createCharacterInstance(command_id, std::move(instance)); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::CharacterInstance> CharacterInstanceService::load(const std::string& instance_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).loadCharacterInstance(instance_id); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> CharacterInstanceService::list(const std::string& world_version_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listCharacterInstances(world_version_id); }
    catch (const std::exception& e) { return xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::CharacterInstance> CharacterInstanceService::saveMemory(
    const std::string& command_id, const std::string& instance_id, int expected_revision, const std::string& memory_json) {
    auto parsed = xuyan::package::parseJson(memory_json, 32, 10000);
    if (!parsed.ok() || !parsed.value->isObject()) return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "实例记忆必须是 JSON 对象", false, "修正记忆字段"});
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveCharacterInstanceMemory(
        command_id, instance_id, expected_revision, xuyan::package::writeJson(*parsed.value)); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::CharacterInstance>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::BranchRootBinding> CharacterInstanceService::bindBranchRoot(
    const std::string& command_id, std::string branch_id, std::string world_version_id,
    std::string snapshot_id, std::string history_mode, std::vector<std::string> character_instance_ids) {
    std::sort(character_instance_ids.begin(), character_instance_ids.end());
    /* 本调用独占的根绑定草稿，接收已排序的实例集合；默认字符串/集合为空，历史模式随下方输入覆盖。
     * 不保留传入值的外部引用，成功时由仓储返回带实例修订摘要的独立绑定。
     */
    xuyan::domain::BranchRootBinding binding;
    binding.branch_id = std::move(branch_id); binding.world_version_id = std::move(world_version_id);
    binding.snapshot_id = std::move(snapshot_id); binding.history_mode = std::move(history_mode);
    binding.character_instance_ids = std::move(character_instance_ids);
    /* 此处摘要仅覆盖排序后的入场配置，供仓储初步校验及命令身份使用；
     * 仓储成功绑定时还加入实例当前修订及源卡版本，返回的 root_hash 以该最终值为准。
     */
    std::string payload = binding.branch_id + '|' + binding.world_version_id + '|' + binding.snapshot_id + '|' + binding.history_mode;
    for (const auto& id : binding.character_instance_ids) payload += '|' + id;
    binding.root_hash = xuyan::domain::sha256(payload);
    try { return xuyan::storage::WorkspaceRepository(database_path_).bindBranchRoot(command_id, std::move(binding)); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::BranchRootBinding>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
