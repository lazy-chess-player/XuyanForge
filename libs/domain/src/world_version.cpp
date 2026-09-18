#include "xuyan/domain/world_version.h"

#include <algorithm>
#include <unordered_set>

namespace xuyan::domain {

Result<WorldVersion> validateWorldVersion(WorldVersion version) {
    if (version.id.empty() || version.world_id.empty() || version.status != "published"
        || version.content_hash.size() != 64 || version.members.empty())
        return Result<WorldVersion>::failure(
            {ErrorCode::validation_failed, "世界版本缺少 ID、成员或有效摘要", false, "重新发布世界版本"});
    std::sort(version.members.begin(), version.members.end(), [](const auto& left, const auto& right) {
        return left.entity_id < right.entity_id;
    });
    std::unordered_set<std::string> ids;
    for (const auto& member : version.members)
        if (member.entity_id.empty() || member.entity_revision < 1 || member.retrieval_scope_revision < 0
            || !ids.insert(member.entity_id).second)
            return Result<WorldVersion>::failure(
                {ErrorCode::validation_failed, "世界版本成员 ID 或修订无效", false, "重新发布世界版本"});
    return Result<WorldVersion>::success(std::move(version));
}

Result<HistoricalSnapshot> validateHistoricalSnapshot(HistoricalSnapshot snapshot) {
    if (snapshot.id.empty() || snapshot.world_version_id.empty() || snapshot.content_hash.size() != 64)
        return Result<HistoricalSnapshot>::failure(
            {ErrorCode::validation_failed, "历史快照缺少版本、ID 或有效摘要", false, "重新构建快照"});
    std::sort(snapshot.included_members.begin(), snapshot.included_members.end(), [](const auto& left, const auto& right) {
        return left.entity_id < right.entity_id;
    });
    std::sort(snapshot.unresolved_entity_ids.begin(), snapshot.unresolved_entity_ids.end());
    snapshot.unresolved_entity_ids.erase(std::unique(snapshot.unresolved_entity_ids.begin(), snapshot.unresolved_entity_ids.end()),
                                         snapshot.unresolved_entity_ids.end());
    return Result<HistoricalSnapshot>::success(std::move(snapshot));
}

} // namespace xuyan::domain
