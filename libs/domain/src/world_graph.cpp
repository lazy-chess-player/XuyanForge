#include "xuyan/domain/world_graph.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {
namespace {
bool validEvidence(std::string_view value) { return value == "evidence" || value == "assumption"; }
void normalize(std::vector<std::string>& values) {
    values.erase(std::remove_if(values.begin(), values.end(), [](const auto& v) { return v.empty(); }), values.end());
    std::sort(values.begin(), values.end()); values.erase(std::unique(values.begin(), values.end()), values.end());
}
}

Result<TimelineEvent> validateTimelineEvent(TimelineEvent event) {
    constexpr std::array statuses{std::string_view{"fact"}, std::string_view{"claim"},
        std::string_view{"hypothesis"}, std::string_view{"future_candidate"}};
    if (event.id.empty() || event.world_id.empty() || event.name.empty() || event.name.size() > 512
        || event.narrative_order < 0 || event.relative_time.size() > 512
        || std::find(statuses.begin(), statuses.end(), event.truth_status) == statuses.end())
        return Result<TimelineEvent>::failure(
            {ErrorCode::validation_failed, "时间事件的 ID、名称、顺序或真实性状态无效", false, "修正事件字段"});
    normalize(event.prerequisites); normalize(event.causes); normalize(event.results);
    return Result<TimelineEvent>::success(std::move(event));
}

Result<DirectedRelation> validateDirectedRelation(DirectedRelation relation) {
    constexpr std::array visibility{std::string_view{"public"}, std::string_view{"author"}, std::string_view{"restricted"}};
    if (relation.id.empty() || relation.world_id.empty() || relation.from_entity_id.empty() || relation.to_entity_id.empty()
        || relation.from_entity_id == relation.to_entity_id || relation.dimension.empty() || relation.dimension.size() > 128
        || relation.strength < -100 || relation.strength > 100 || !validEvidence(relation.evidence_status)
        || std::find(visibility.begin(), visibility.end(), relation.visibility) == visibility.end()
        || (relation.valid_from && relation.valid_to && *relation.valid_from > *relation.valid_to))
        return Result<DirectedRelation>::failure(
            {ErrorCode::validation_failed, "定向关系端点、维度、强度、时间或可见性无效", false, "修正关系字段"});
    normalize(relation.actor_grants);
    if (relation.visibility == "restricted" && relation.actor_grants.empty())
        return Result<DirectedRelation>::failure(
            {ErrorCode::validation_failed, "受限关系至少需要一个人物授权", false, "选择可见人物"});
    if (relation.visibility != "restricted") relation.actor_grants.clear();
    return Result<DirectedRelation>::success(std::move(relation));
}

Result<LocationPlacement> validateLocationPlacement(LocationPlacement placement) {
    if (placement.location_id.empty() || placement.location_id == placement.parent_location_id
        || placement.image_x.has_value() != placement.image_y.has_value()
        || (placement.image_x && (*placement.image_x < 0 || *placement.image_y < 0))
        || placement.background_asset_ref.size() > 1024 || !validEvidence(placement.evidence_status))
        return Result<LocationPlacement>::failure(
            {ErrorCode::validation_failed, "地点层级、底图坐标或证据状态无效", false, "修正地点标注"});
    return Result<LocationPlacement>::success(std::move(placement));
}

Result<TravelRoute> validateTravelRoute(TravelRoute route) {
    if (route.id.empty() || route.from_location_id.empty() || route.to_location_id.empty()
        || route.from_location_id == route.to_location_id || (route.travel_minutes && *route.travel_minutes <= 0)
        || !validEvidence(route.evidence_status))
        return Result<TravelRoute>::failure(
            {ErrorCode::validation_failed, "路线端点、行程时间或证据状态无效", false, "修正路线"});
    return Result<TravelRoute>::success(std::move(route));
}

} // namespace xuyan::domain
