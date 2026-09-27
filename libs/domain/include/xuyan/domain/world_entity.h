#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>
#include <vector>

namespace xuyan::domain {

struct WorldEntity {
    std::string id;
    std::string world_id;
    std::string kind;
    std::string name;
    std::vector<std::string> aliases;
    std::vector<std::string> tags;
    std::string description;
    std::string attributes_json{"{}"};
    std::string review_status{"accepted"};
    int revision{0};
    bool deleted{false};
};

struct EntityPage {
    std::vector<WorldEntity> items;
    int offset{0};
    int total{0};
    bool has_more{false};
};

struct EntityMergeResult {
    std::string merge_id;
    WorldEntity source;
    WorldEntity target;
    bool active{true};
};

/** @brief 校验世界实体的身份、类型与可编辑字段。 */
Result<WorldEntity> validateEntity(WorldEntity entity);
/** @brief 判断实体类型是否属于当前协议支持的集合。 */
bool isSupportedEntityKind(std::string_view kind);

} // namespace xuyan::domain
