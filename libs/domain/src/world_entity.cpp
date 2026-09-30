#include "xuyan/domain/world_entity.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {
namespace {

/*
 * 功能：检查世界条目字段的禁用控制字符。
 * 参数：
 *   value：借用待检查字节文本，允许制表符。
 * 返回：有禁用控制字节为true，否则false。
 * 失败：约束内的纯计算不产生业务异常；调用者须遵守参数前置条件。
 * 副作用：只读输入。
 */
bool containsControl(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 && character != '\t';
    });
}

/*
 * 功能：清理条目的标签或别名列表，去空、排序并去重。
 * 参数：
 *   values：调用者拥有的可变列表；不能依赖原顺序或元素引用。
 * 返回：无；更新原集合。
 * 失败：字符串或容器分配可抛标准异常。
 * 副作用：修改列表，不自动关联实体。
 */
void normalizeList(std::vector<std::string>& values) {
    values.erase(std::remove_if(values.begin(), values.end(), [](const auto& value) { return value.empty(); }), values.end());
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

bool isSupportedEntityKind(std::string_view kind) {
    constexpr std::array kinds{
        std::string_view{"character"}, std::string_view{"faction"}, std::string_view{"location"},
        std::string_view{"item"}, std::string_view{"rule"}, std::string_view{"event"},
        std::string_view{"culture"}, std::string_view{"technology"}, std::string_view{"other"},
    };
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}


Result<WorldEntity> validateEntity(WorldEntity entity) {
    if (entity.name.empty() || entity.name.size() > 512 || containsControl(entity.name)) {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "条目名称不能为空、过长或包含控制字符", false, "修改条目名称"});
    }
    if (!isSupportedEntityKind(entity.kind)) {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "不支持的条目类型", false, "选择已支持类型或其他类型"});
    }
    if (entity.description.size() > 1024 * 1024) {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "条目说明超过 1 MiB 上限", false, "拆分或精简条目"});
    }
    if (entity.aliases.size() > 128 || entity.tags.size() > 128) {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "别名或标签数量超过上限", false, "减少别名或标签"});
    }
    for (const auto& value : entity.aliases) {
        // 与类型化提取的逐字标识上限一致，不在人工接受时截断合法别名。
        if (value.size() > 512 || containsControl(value)) {
            return Result<WorldEntity>::failure(
                {ErrorCode::validation_failed, "别名过长或包含控制字符", false, "修改别名"});
        }
    }
    for (const auto& value : entity.tags) {
        if (value.size() > 128 || containsControl(value)) {
            return Result<WorldEntity>::failure(
                {ErrorCode::validation_failed, "标签过长或包含控制字符", false, "修改标签"});
        }
    }
    if (entity.attributes_json.size() > 1024 * 1024 || entity.attributes_json.empty()
        || entity.attributes_json.front() != '{' || entity.attributes_json.back() != '}') {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "扩展属性必须是大小受限的 JSON 对象", false, "修正扩展属性"});
    }
    if (entity.review_status != "candidate" && entity.review_status != "accepted"
        && entity.review_status != "rejected" && entity.review_status != "conflicted") {
        return Result<WorldEntity>::failure(
            {ErrorCode::validation_failed, "审核状态无效", false, "选择有效审核状态"});
    }
    normalizeList(entity.aliases);
    normalizeList(entity.tags);
    return Result<WorldEntity>::success(std::move(entity));
}

} // namespace xuyan::domain
