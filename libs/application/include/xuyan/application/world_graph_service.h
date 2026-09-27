#pragma once

#include "xuyan/domain/world_graph.h"

#include <filesystem>

namespace xuyan::application {

class WorldGraphService {
public:
    /** @brief 绑定世界事件、关系和地点图所在的数据库。 */
    explicit WorldGraphService(std::filesystem::path database_path);
    /** @brief 按期望修订保存带时间语义的事件节点。 */
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    /** @brief 按叙事顺序或故事时间列出事件，可限制最大故事时间。 */
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimeline(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time = std::nullopt);
    /** @brief 按期望修订保存有方向与可见范围的实体关系。 */
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    /** @brief 只列出请求角色在指定故事时间有权看到的关系。 */
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    /** @brief 保存地点父子结构与可选图像坐标，拒绝环形层级。 */
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocation(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    /** @brief 保存地点间有方向的行程路线和旅行时间。 */
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    /** @brief 加载世界地图所需的地点层级与路线。 */
    xuyan::domain::Result<xuyan::domain::MapView> loadMap(const std::string& world_id);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
