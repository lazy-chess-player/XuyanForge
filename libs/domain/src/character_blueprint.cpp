#include "xuyan/domain/character_blueprint.h"

#include <algorithm>

namespace xuyan::domain {
namespace {

bool hasControl(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char value) { return value < 0x20 && value != '\t'; });
}

void normalize(std::vector<std::string>& values) {
    values.erase(std::remove_if(values.begin(), values.end(), [](const auto& value) { return value.empty(); }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

Result<CharacterBlueprint> validateBlueprint(CharacterBlueprint blueprint) {
    if (blueprint.name.empty() || blueprint.name.size() > 512 || hasControl(blueprint.name)) {
        return Result<CharacterBlueprint>::failure(
            {ErrorCode::validation_failed, "人物卡名称不能为空、过长或包含控制字符", false, "修改人物卡名称"});
    }
    if (blueprint.summary.size() > 16 * 1024 || blueprint.background.size() > 256 * 1024
        || blueprint.private_notes.size() > 256 * 1024) {
        return Result<CharacterBlueprint>::failure(
            {ErrorCode::validation_failed, "人物卡文本字段超过大小限制", false, "精简或拆分人物卡内容"});
    }
    if (blueprint.values.size() > 128 || blueprint.traits.size() > 128 || blueprint.equipment.size() > 256) {
        return Result<CharacterBlueprint>::failure(
            {ErrorCode::validation_failed, "人物卡列表字段超过数量限制", false, "减少列表项目"});
    }
    if (blueprint.abilities_json.empty() || blueprint.abilities_json.front() != '['
        || blueprint.abilities_json.back() != ']' || blueprint.extensions_json.empty()
        || blueprint.extensions_json.front() != '{' || blueprint.extensions_json.back() != '}') {
        return Result<CharacterBlueprint>::failure(
            {ErrorCode::validation_failed, "能力必须是 JSON 数组，扩展字段必须是 JSON 对象", false, "修正 JSON 字段"});
    }
    normalize(blueprint.values);
    normalize(blueprint.traits);
    normalize(blueprint.equipment);
    return Result<CharacterBlueprint>::success(std::move(blueprint));
}

} // namespace xuyan::domain

