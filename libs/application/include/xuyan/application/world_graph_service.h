#pragma once

#include "xuyan/domain/world_graph.h"

#include <filesystem>

namespace xuyan::application {

/* 世界时间线、关系与地图用例入口；持有数据库路径，在调用线程建立局部仓储，未知时间/坐标保持未知。 */
class WorldGraphService {
public:
    /*
     * 功能：绑定世界图工作区。参数：database_path 为数据库路径，按值保存。
     * 返回：完成初始化。失败：分配异常传播，不打开数据库。
     * 副作用：无文件访问；线程：同步构造，不拥有后台线程。
     */
    explicit WorldGraphService(std::filesystem::path database_path);
    /*
     * 功能：保存时间线节点。参数：command_id 为幂等命令；event 为事件完整值，未知时间须保持空值；
     * expected_revision 为编辑依据修订，新增规则由仓储校验。
     * 返回：已保存事件。失败：世界、时间、字段、修订或数据库错误返回 Result。
     * 副作用：原子写事件与命令；线程：同步，不把叙事顺序转换为故事日期。
     */
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    /*
     * 功能：按世界查询时间线。参数：world_id 为世界标识；narrative_order 为 true 时按叙事顺序，否则按故事时间；
     * maximum_story_time 为故事刻度上界，默认空不限制，未知时间处理沿用仓储规则。
     * 返回：事件列表，空列表成功。失败：查询或存储错误返回 Result。
     * 副作用：只读时间线；线程：同步，结果为值对象。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimeline(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time = std::nullopt);
    /*
     * 功能：保存实体关系。参数：command_id 为幂等命令；relation 为含端点、方向、可见范围和来源性质的值；
     * expected_revision 为当前修订，新增规则由仓储校验。
     * 返回：持久化关系。失败：端点跨世界、字段/修订无效或存储错误返回 Result。
     * 副作用：同事务写关系与命令；线程：同步，不根据名称自动匹配端点。
     */
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    /*
     * 功能：按权限与时间读取关系。参数：world_id 为世界；entity_id 为端点过滤，空值语义由仓储处理；
     * story_time 为可选故事刻度，空值不限时间；actor_id 为请求角色；author_view 为作者权限开关。
     * 返回：可见关系列表，无记录成功。失败：权限参数或存储错误返回 Result。
     * 副作用：只读关系；线程：同步，调用方须正确提供权限上下文。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    /*
     * 功能：保存地点层级与可选坐标。参数：command_id 为幂等命令；placement 为地点放置值，空坐标表示未知；
     * expected_revision 为现有修订。
     * 返回：已保存地点。失败：地点缺失、层级环、字段或修订/数据库错误返回 Result。
     * 副作用：同事务写放置与命令；线程：同步，不推定坐标或父地点。
     */
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocation(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    /*
     * 功能：保存有方向的地点路线。参数：command_id 为幂等命令；route 为路线值，旅行时间单位由领域类型定义；
     * expected_revision 为当前路线修订。
     * 返回：已保存路线。失败：端点、旅行时间、修订或存储错误返回 Result。
     * 副作用：同事务写路线与命令；线程：同步，不自动添加反向路线。
     */
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    /*
     * 功能：读取世界地图数据。参数：world_id 为世界稳定标识。
     * 返回：地点及路线视图，无资料成功返回空集合。失败：数据库错误返回 Result。
     * 副作用：只读地图，不读写底图文件；线程：同步，结果不借用仓储。
     */
    xuyan::domain::Result<xuyan::domain::MapView> loadMap(const std::string& world_id);
private:
    /* 世界图数据库路径；构造后只读，与服务同寿命，每次调用独立打开仓储。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
