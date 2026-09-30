#include "xuyan/domain/character_blueprint.h"

#include <algorithm>

namespace xuyan::domain {
namespace {

/*
 * 功能：检查人物卡名称中的禁用控制字符，保留制表符。
 * 参数：
 *   text：借用待检查字节文本，仅调用期间有效。
 * 返回：有非制表符的低位控制字节为true，否则false。
 * 失败：约束内的纯计算不产生业务异常；调用者须遵守参数前置条件。
 * 副作用：只读输入，不规范化或删除文本。
 */
bool hasControl(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](unsigned char value) { return value < 0x20 && value != '\t'; });
}

/*
 * 功能：删除集合的空项，按字节序排序并去重。
 * 参数：
 *   values：可变字符串集合引用；调用者拥有容器，元素顺序允许改变。
 * 返回：无；原集合成为有序唯一的非空项集合。
 * 失败：字符串或容器分配可抛标准异常。
 * 副作用：原地修改容器并使迭代器失效，不读写文件。
 */
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
