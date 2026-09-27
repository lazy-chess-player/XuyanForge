#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

struct CharacterBlueprint {
    std::string id;
    int version{0};
    std::string name;
    std::string summary;
    std::vector<std::string> values;
    std::vector<std::string> traits;
    std::string long_term_goal;
    std::string short_term_goal;
    std::string speech_style;
    std::string abilities_json{"[]"};
    std::vector<std::string> equipment;
    std::string background;
    std::string private_notes;
    std::string extensions_json{"{}"};
    bool deleted{false};
};

/** @brief 校验并规范化人物卡字段；失败时返回可供界面展示的错误。 */
Result<CharacterBlueprint> validateBlueprint(CharacterBlueprint blueprint);

} // namespace xuyan::domain
