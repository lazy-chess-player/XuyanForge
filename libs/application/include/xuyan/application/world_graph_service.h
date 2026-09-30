#pragma once

#include "xuyan/domain/world_graph.h"

#include <filesystem>

namespace xuyan::application {

/*
 * 职责：保存/读取时间线、实体关系、地点标注及路线；故事时间与叙事顺序分离，未知数值保持可选空值。
 * 资源与生命周期：仅拥有数据库路径；每次调用独立创建并销毁局部仓储，返回值不借用连接。
 * 线程：同步在调用线程执行，无后台任务；调用期间服务须存活，销毁不能与调用并发。
 * 存储边界：业务调用（含查询）可创建父目录/数据库并迁移结构；写入使用仓储事务。
 * 异常边界：业务方法 try 内的标准异常转为 storage_error；实参构造和非标准异常仍可传播。
 */
class WorldGraphService {
public:
    /*
     * 功能：保存世界图操作使用的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在后续调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径构造/分配异常直接传播。
     * 副作用：仅初始化本对象，不保留调用方路径引用。
     */
    explicit WorldGraphService(std::filesystem::path database_path);
    /*
     * 功能：按事件自身修订创建或替换时间线节点及其显式前提/因果/结果引用。
     * 参数：
     *   command_id：输入幂等标识，调用期间借用；同标识不得用于不同世界图记录或负载，调用方应提供非空值。
     *   event：输入完整事件副本；id/world_id/name 非空，名称及 relative_time 各最多 512 字节；
     *     narrative_order 非负且仅为叙事序号，story_time 用世界故事刻度、空值为未知；
     *     truth_status 仅 fact、claim、hypothesis、future_candidate；引用集合移除空项并排序去重，不在此验证因果真实性。
     *     revision 由仓储重算，未知时间不会依据叙事序号补写。
     *   expected_revision：输入事件自身修订，首次创建为 0，更新须等于现存正修订。
     * 返回：修订加 1 的规范化事件；同负载命令重放读取现存事件，可能已被后续编辑。
     * 失败：字段无效为 validation_failed，过期修订为 revision_conflict，不同负载/记录类型为 command_conflict；
     *   数据库打开/迁移、写入或重放读取的标准异常转为 storage_error。
     * 副作用：同事务更新事件、替换全部引用边并写命令；打开可能创建/迁移结构，不自动生成因果。
     */
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    /*
     * 功能：按世界读取时间线，可限制已知故事时间的上界；不分页。
     * 参数：
     *   world_id：输入精确匹配的世界 ID，调用期间借用；不预先校验世界存在，空值仍按精确值查询。
     *   narrative_order：输入排序开关；true 按叙事序号、ID 升序；false 按已知时间优先、故事刻度、ID 升序。
     *   maximum_story_time：输入可选故事刻度上界（包含边界），默认空不过滤；未知时间事件始终保留。
     * 返回：自有事件列表，无匹配为成功空列表；包含各事件的显式引用集合。
     * 失败：数据库打开/迁移、查询或任一事件读取的标准异常为 storage_error。
     * 副作用：只读时间线业务记录，打开可能创建/迁移结构；排序不改写故事日期。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimeline(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time = std::nullopt);
    /*
     * 功能：按关系自身修订保存显式端点、方向、强度、时间和可见性。
     * 参数：
     *   command_id：输入幂等标识，借用至返回；同命令须复用相同规范化关系和预期修订，不能混用图记录类型。
     *   relation：输入完整关系副本；id/world_id/两端 ID 非空，端点须不同且属于同世界未删除实体；
     *     dimension 为 1—256 字节，已知 strength 为 -100—100（无物理单位），空值为未知；
     *     valid_from/valid_to 为闭区间故事刻度，空值无该侧边界，已知下界不得晚于上界。
     *     visibility 仅 public、author、restricted；授权列表移除空项并去重，restricted 须有授权，其他模式清空授权。
     *     evidence_status 仅 evidence/assumption；truth_status 空时分别补 fact/hypothesis，显式 fact 须配 evidence，
     *     claim/hypothesis 须配 assumption；bidirectional 表示一条关系的双向语义，revision 被重算。
     *   expected_revision：输入关系自身修订，首次创建 0，更新匹配当前正修订；不是端点实体修订。
     * 返回：规范化且修订加 1 的关系，或同负载命令对应的现存关系；受兼容规则允许的旧命令只读重放。
     * 失败：字段/组合无效、端点缺失或跨世界、修订冲突、命令冲突分别返回 Result；打开/迁移/存储标准异常为 storage_error。
     * 副作用：同事务写关系本体/语义、替换人物授权并记命令；打开可能创建/迁移结构，不按名称匹配端点。
     */
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    /*
     * 功能：按世界、任一端点、故事时间和显式可见性查询关系，不分页。
     * 参数：
     *   world_id：输入精确匹配的世界 ID，借用至返回；不预先校验世界存在。
     *   entity_id：输入端点 ID，借用至返回；空串不限制端点，否则匹配起点或终点，不按方向标志生成额外记录。
     *   story_time：输入可选世界故事刻度，空值不过滤；有值时保留两侧已知边界内（包含边界）的关系。
     *   actor_id：输入授权人物 ID，借用至返回；作者视角不用此值，人物视角空值仍可读公开关系，当前无非空校验。
     *   author_view：输入作者视角开关；true 略过可见性过滤，false 仅公开或 restricted 中明确授权者可见；调用方负责身份真实性。
     * 返回：按起点、终点、维度、ID 升序的自有列表；无匹配为成功空列表，不预先核验端点当前删除状态。
     * 失败：打开/迁移、查询或任一关系本体/语义读取的标准异常为 storage_error；不额外校验权限参数。
     * 副作用：只读关系业务记录，打开可能创建/迁移结构，不改变授权。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    /*
     * 功能：按标注自身修订保存地点层级、可选图像坐标及真实性语义。
     * 参数：
     *   command_id：输入幂等标识，借用至返回；同命令须保持规范化标注及 expected_revision 一致。
     *   placement：输入标注副本；location_id 须指向未删除的 location 类实体；parent_location_id 空表示无父，
     *     非空须已有地图标注且不能为自身。image_x/image_y 为图像像素坐标，须同时空（未知）或同时非负；
     *     background_asset_ref 最多 1024 字节，空表示无底图，当前不验证图像存在或坐标上界。
     *     evidence_status/truth_status 的合法组合及空真实性规范化同 saveRelation；revision 由仓储重算。
     *     调用方应保证父地点世界相容，当前仅查父标注存在，未单独检查同世界。
     *   expected_revision：输入地点标注自身修订，首次创建为 0，更新匹配现存正修订，不是地点实体修订。
     * 返回：修订加 1 的标注，或同负载命令的现存标注；兼容的旧命令只读重放。
     * 失败：字段无效、地点/父标注缺失、沿最多 128 层祖先检测到环、修订或命令冲突返回 Result；
     *   打开/迁移及存储标准异常为 storage_error，当前不保证检出超过该祖先深度的环。
     * 副作用：同事务写地点本体/真实性及命令，打开可能创建/迁移结构；不读写底图文件、不补未知坐标。
     */
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocation(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    /*
     * 功能：按路线自身修订保存两个已标注地点间的行程和方向。
     * 参数：
     *   command_id：输入幂等标识，借用至返回；同标识须保持相同路线负载，不能混用图记录类型。
     *   route：输入路线副本；id 及两端地点 ID 非空、两端不同且均已有地图标注；
     *     travel_minutes 为空表示未知，有值须为正整数分钟；bidirectional 默认 true 表示双向通行，false 为起点至终点；
     *     evidence_status 仅 evidence/assumption，revision 由仓储重算。调用方须保证端点世界相容，当前未校验同世界。
     *   expected_revision：输入路线自身修订，首次创建为 0，更新须匹配现存正修订。
     * 返回：修订加 1 的路线，或同负载命令对应的现存路线。
     * 失败：字段无效、端点未加入地图、修订/命令冲突返回 Result；打开/迁移及存储标准异常为 storage_error。
     * 副作用：同事务写一条路线和命令，打开可能创建/迁移结构；双向标志不生成反向副本。
     */
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    /*
     * 功能：读取世界地点标注和两端同属该世界的路线，不分页。
     * 参数：world_id：输入精确匹配的世界 ID，借用至返回；不预先校验世界存在，空值仍按精确值查询。
     * 返回：自有地图值，地点按地点 ID、路线按路线 ID 升序；无资料时对应集合为空。
     *   地点仅含未删除实体，路线查询目前不单独排除已删除端点，故两集合不保证引用闭合。
     * 失败：数据库打开/迁移、查询或任一记录/语义读取的标准异常为 storage_error。
     * 副作用：只读地图业务记录，不读写底图文件；打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::MapView> loadMap(const std::string& world_id);
private:
    /* 世界图数据库文件路径，无计量单位；初值为构造实参，无默认实参，空值仍保存。
     * 构造写入，各方法只读；拥有路径值至服务销毁，每次调用独立打开仓储，不缓存图记录。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
