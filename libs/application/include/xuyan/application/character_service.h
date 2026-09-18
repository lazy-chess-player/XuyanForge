#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

class CharacterService {
public:
    explicit CharacterService(std::filesystem::path database_path);

    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> openAndList();
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> create(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> save(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> load(
        const std::string& blueprint_id, int version = -1);
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> list();

private:
    xuyan::domain::Result<bool> ensureSample();
    std::filesystem::path database_path_;
};

} // namespace xuyan::application

