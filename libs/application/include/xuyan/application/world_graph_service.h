#pragma once

#include "xuyan/domain/world_graph.h"

#include <filesystem>

namespace xuyan::application {

class WorldGraphService {
public:
    explicit WorldGraphService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimeline(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time = std::nullopt);
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocation(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    xuyan::domain::Result<xuyan::domain::MapView> loadMap(const std::string& world_id);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
