#pragma once

#include "xuyan/domain/scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

struct WorldVersionMember {
    std::string entity_id;
    int entity_revision{0};
    int retrieval_scope_revision{0};
};

struct WorldVersion {
    std::string id;
    std::string world_id;
    std::string parent_id;
    std::string status{"published"};
    std::string content_hash;
    std::string published_at;
    std::vector<WorldVersionMember> members;
};

struct HistoricalSnapshot {
    std::string id;
    std::string world_version_id;
    std::int64_t story_time{0};
    std::string content_hash;
    std::vector<WorldVersionMember> included_members;
    std::vector<std::string> unresolved_entity_ids;
};

/** @brief 校验已发布世界版本与实体修订清单。 */
Result<WorldVersion> validateWorldVersion(WorldVersion version);
/** @brief 校验指定故事时间的历史快照及未决实体记录。 */
Result<HistoricalSnapshot> validateHistoricalSnapshot(HistoricalSnapshot snapshot);

} // namespace xuyan::domain
