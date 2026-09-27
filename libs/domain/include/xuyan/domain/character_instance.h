#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

struct CharacterInstance {
    std::string id;
    std::string blueprint_id;
    int blueprint_version{0};
    std::string world_version_id;
    std::string snapshot_id;
    std::string name;
    std::string adaptation_json{"{}"};
    std::string knowledge_policy{"strict"};
    std::string memory_json{"{}"};
    std::string status{"ready"};
    std::vector<std::string> conflicts;
    int revision{0};
};

struct BranchRootBinding {
    std::string branch_id;
    std::string world_version_id;
    std::string snapshot_id;
    std::string history_mode{"branching"};
    std::vector<std::string> character_instance_ids;
    std::string root_hash;
};

/** @brief 校验人物实例与其世界版本、知识策略等绑定信息。 */
Result<CharacterInstance> validateCharacterInstance(CharacterInstance instance);
/** @brief 校验分支根节点关联的世界和人物快照。 */
Result<BranchRootBinding> validateBranchRootBinding(BranchRootBinding binding);

} // namespace xuyan::domain
