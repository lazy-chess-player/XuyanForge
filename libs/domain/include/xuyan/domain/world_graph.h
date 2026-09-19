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
    int strength{0};
    std::optional<std::int64_t> valid_from;
    std::optional<std::int64_t> valid_to;
    std::string visibility{"public"};
    std::vector<std::string> actor_grants;
    std::string evidence_status{"evidence"};
    int revision{0};
};

struct LocationPlacement {
    std::string location_id;
    std::string parent_location_id;
    std::optional<int> image_x;
    std::optional<int> image_y;
    std::string background_asset_ref;
    std::string evidence_status{"evidence"};
    int revision{0};
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

Result<TimelineEvent> validateTimelineEvent(TimelineEvent event);
Result<DirectedRelation> validateDirectedRelation(DirectedRelation relation);
Result<LocationPlacement> validateLocationPlacement(LocationPlacement placement);
Result<TravelRoute> validateTravelRoute(TravelRoute route);

} // namespace xuyan::domain
