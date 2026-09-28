#pragma once

#include "xuyan/domain/scenario.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xuyan::domain {

struct TimelineEvent {
    std::string id;
    std::string world_id;
    std::string name;
    std::optional<std::int64_t> story_time;
    int narrative_order{0};
    std::string relative_time;
    std::string truth_status{"fact"};
    std::vector<std::string> prerequisites;
    std::vector<std::string> causes;
    std::vector<std::string> results;
    int revision{0};
};

struct DirectedRelation {
    std::string id;
    std::string world_id;
    std::string from_entity_id;
    std::string to_entity_id;
    std::string dimension;
    // 人工旧记录的零值保持已知零；小说提取未给出数值时显式置为空。
    std::optional<int> strength{0};
    std::optional<std::int64_t> valid_from;
    std::optional<std::int64_t> valid_to;
    std::string visibility{"public"};
    std::vector<std::string> actor_grants;
    std::string evidence_status{"evidence"};
    int revision{0};
    bool bidirectional{false};
    // 空值仅用于人工旧调用，校验时按证据状态规范化，持久化时始终写明确状态。
    std::string truth_status{};
};

struct LocationPlacement {
    std::string location_id;
    std::string parent_location_id;
    std::optional<int> image_x;
    std::optional<int> image_y;
    std::string background_asset_ref;
    std::string evidence_status{"evidence"};
    int revision{0};
    std::string truth_status{};
};

struct TravelRoute {
    std::string id;
    std::string from_location_id;
    std::string to_location_id;
    std::optional<int> travel_minutes;
    bool bidirectional{true};
    std::string evidence_status{"evidence"};
    int revision{0};
};

struct MapView {
    std::vector<LocationPlacement> locations;
    std::vector<TravelRoute> routes;
};

/** @brief 保存作者明确选择的两个关系端点及预期修订，不接受仅有名称的自动对应。 */
struct RelationEndpointSelection {
    std::string from_entity_id;
    int from_revision{0};
    std::string to_entity_id;
    int to_revision{0};
};

/** @brief 携带一种专用图投影；关系必须带端点选择，地点不得携带关系或端点。 */
struct CandidateGraphProjection {
    std::optional<DirectedRelation> relation;
    std::optional<LocationPlacement> location;
    std::optional<RelationEndpointSelection> endpoints;
};

/** @brief 校验时间线事件及其前因后果引用。 */
Result<TimelineEvent> validateTimelineEvent(TimelineEvent event);
/** @brief 校验有向关系、有效时间和可见性。 */
Result<DirectedRelation> validateDirectedRelation(DirectedRelation relation);
/** @brief 校验地点的层级位置和可选地图坐标。 */
Result<LocationPlacement> validateLocationPlacement(LocationPlacement placement);
/** @brief 校验地点间路线、耗时和方向。 */
Result<TravelRoute> validateTravelRoute(TravelRoute route);

} // namespace xuyan::domain
