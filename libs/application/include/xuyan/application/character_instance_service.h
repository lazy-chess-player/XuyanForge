#pragma once

#include "xuyan/domain/character_instance.h"

#include <filesystem>

namespace xuyan::application {

class CharacterInstanceService {
public:
    explicit CharacterInstanceService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> instantiate(
        const std::string& command_id, const std::string& blueprint_id, int blueprint_version,
        const std::string& world_version_id, const std::string& snapshot_id,
        const std::string& adaptation_json, const std::string& knowledge_policy);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> load(const std::string& instance_id);
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> list(const std::string& world_version_id);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, std::string branch_id, std::string world_version_id,
        std::string snapshot_id, std::string history_mode, std::vector<std::string> character_instance_ids);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
