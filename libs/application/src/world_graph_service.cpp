#include "xuyan/application/world_graph_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

WorldGraphService::WorldGraphService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::TimelineEvent> WorldGraphService::saveTimelineEvent(
    const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveTimelineEvent(command_id, std::move(event), expected_revision); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::TimelineEvent>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> WorldGraphService::listTimeline(
    const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listTimelineEvents(world_id, narrative_order, maximum_story_time); }
    catch (const std::exception& e) { return xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::DirectedRelation> WorldGraphService::saveRelation(
    const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveDirectedRelation(command_id, std::move(relation), expected_revision); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::DirectedRelation>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> WorldGraphService::listRelations(
    const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
    const std::string& actor_id, bool author_view) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listDirectedRelations(
        world_id, entity_id, story_time, actor_id, author_view); }
    catch (const std::exception& e) { return xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::LocationPlacement> WorldGraphService::saveLocation(
    const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveLocationPlacement(command_id, std::move(placement), expected_revision); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::LocationPlacement>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::TravelRoute> WorldGraphService::saveRoute(
    const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveTravelRoute(command_id, std::move(route), expected_revision); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::TravelRoute>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::MapView> WorldGraphService::loadMap(const std::string& world_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).loadMapView(world_id); }
    catch (const std::exception& e) { return xuyan::domain::Result<xuyan::domain::MapView>::failure(
        {xuyan::domain::ErrorCode::storage_error, e.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
