#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

class CharacterService {
public:
    /** @brief 绑定保存人物卡版本的工作区数据库。 */
    explicit CharacterService(std::filesystem::path database_path);

    /** @brief 打开工作区并列出可移植人物卡。 */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> openAndList();
    /** @brief 创建独立版本化的人物卡。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> create(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    /** @brief 以期望版本保存人物卡的新版本并保留旧版本。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> save(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    /** @brief 按 ID 与可选版本读取人物卡，默认读取最新版本。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> load(
        const std::string& blueprint_id, int version = -1);
    /** @brief 列出当前工作区可读取的人物卡。 */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> list();

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
