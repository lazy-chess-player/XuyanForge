#include "xuyan/domain/character_instance.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<CharacterInstance> validateCharacterInstance(CharacterInstance instance) {
    constexpr std::array policies{std::string_view{"strict"}, std::string_view{"public_only"}, std::string_view{"author_selected"}};
    if (instance.id.empty() || instance.blueprint_id.empty() || instance.blueprint_version < 1
        || instance.world_version_id.empty() || instance.snapshot_id.empty() || instance.name.empty()
        || instance.adaptation_json.empty() || instance.adaptation_json.front() != '{' || instance.adaptation_json.back() != '}'
        || instance.memory_json.empty() || instance.memory_json.front() != '{' || instance.memory_json.back() != '}'
        || std::find(policies.begin(), policies.end(), instance.knowledge_policy) == policies.end()
        || (instance.status != "ready" && instance.status != "needs_resolution"))
        return Result<CharacterInstance>::failure(
            {ErrorCode::validation_failed, "人物实例的卡片、世界、适配、知识策略或状态无效", false, "修正入场配置"});
    std::sort(instance.conflicts.begin(), instance.conflicts.end());
    instance.conflicts.erase(std::unique(instance.conflicts.begin(), instance.conflicts.end()), instance.conflicts.end());
    if (instance.conflicts.empty()) instance.status = "ready"; else instance.status = "needs_resolution";
    return Result<CharacterInstance>::success(std::move(instance));
}

Result<BranchRootBinding> validateBranchRootBinding(BranchRootBinding binding) {
    constexpr std::array modes{std::string_view{"original_constrained"}, std::string_view{"branching"}, std::string_view{"sandbox"}};
    if (binding.branch_id.empty() || binding.world_version_id.empty() || binding.snapshot_id.empty()
        || binding.root_hash.size() != 64 || binding.character_instance_ids.empty()
        || std::find(modes.begin(), modes.end(), binding.history_mode) == modes.end())
        return Result<BranchRootBinding>::failure(
            {ErrorCode::validation_failed, "分支根的版本、快照、人物或历史模式无效", false, "修正分支配置"});
    std::sort(binding.character_instance_ids.begin(), binding.character_instance_ids.end());
    if (std::adjacent_find(binding.character_instance_ids.begin(), binding.character_instance_ids.end()) != binding.character_instance_ids.end())
        return Result<BranchRootBinding>::failure(
            {ErrorCode::validation_failed, "分支根包含重复人物实例", false, "移除重复实例"});
    return Result<BranchRootBinding>::success(std::move(binding));
}

} // namespace xuyan::domain
