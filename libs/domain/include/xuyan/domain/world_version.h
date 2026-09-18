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
    std::string world_id{"world-grey-harbor"};
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

Result<WorldVersion> validateWorldVersion(WorldVersion version);
Result<HistoricalSnapshot> validateHistoricalSnapshot(HistoricalSnapshot snapshot);

} // namespace xuyan::domain
