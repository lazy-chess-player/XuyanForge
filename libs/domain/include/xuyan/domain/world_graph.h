#pragma once

#include "xuyan/domain/scenario.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：时间、叙事顺序和因果引用独立的事件资料。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct TimelineEvent {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 故事时间坐标，单位由世界体系定义；可选类型的空值表示未知或不设置过滤。默认未知空值。
    std::optional<std::int64_t> story_time;
    // 叙事呈现顺序的整数位置，不等于故事日期。默认0。
    int narrative_order{0};
    // 原文提供的相对时间描述，未知绝对时间时保留描述。默认空串。
    std::string relative_time;
    // 资料真实性内部状态，事实、说法、推断分别保存。默认"fact"。
    std::string truth_status{"fact"};
    // 事件前置条件的引用标识集合。默认空集合，不预填资料。
    std::vector<std::string> prerequisites;
    // 事件前因的引用标识集合，不由章节邻接自动推断。默认空集合，不预填资料。
    std::vector<std::string> causes;
    // 事件后果的引用标识集合。默认空集合，不预填资料。
    std::vector<std::string> results;
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 职责：保留方向、维度、未知数值和可见范围的实体关系。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct DirectedRelation {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 关系起点实体的稳定标识。默认空串。
    std::string from_entity_id;
    // 关系终点实体的稳定标识。默认空串。
    std::string to_entity_id;
    // 关系维度内部值，区分信任等不同关系。默认空串。
    std::string dimension;
    // 人工旧记录的零值保持已知零；小说提取未给出数值时显式置为空。
    // 可选关系强度，无物理单位，合法范围-100—100；默认已知0，未知时须显式置空，不由提取器补零。
    std::optional<int> strength{0};
    // 可选故事时间下界，空值表示未限定，不用现实UTC替代。默认未知空值。
    std::optional<std::int64_t> valid_from;
    // 可选故事时间上界，空值表示未限定。默认未知空值。
    std::optional<std::int64_t> valid_to;
    // 人物可见性的内部策略值，由检索器校验。默认"public"。
    std::string visibility{"public"};
    // 获得显式访问授权的人物标识集合。默认空集合，不预填资料。
    std::vector<std::string> actor_grants;
    // 证据性质内部值，区分有证据、推断或说法。默认"evidence"。
    std::string evidence_status{"evidence"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 该关系或路线是否允许双向解释/通行。默认false。
    bool bidirectional{false};
    // 空值仅用于人工旧调用，校验时按证据状态规范化，持久化时始终写明确状态。
    // 资料真实性内部状态，事实、说法、推断分别保存。默认空值。
    std::string truth_status{};
};

/*
 * 职责：地点层级、可选地图坐标与证据性质。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct LocationPlacement {
    // 地点实体的稳定标识。默认空串。
    std::string location_id;
    // 父地点稳定标识，空串表示没有父节点。默认空串。
    std::string parent_location_id;
    // 可选地图图像横坐标，单位为图像像素；空值表示未知。默认未知空值。
    std::optional<int> image_x;
    // 可选地图图像纵坐标，单位为图像像素；空值表示未知。默认未知空值。
    std::optional<int> image_y;
    // 地图背景图像资产引用，空串表示没有背景。默认空串。
    std::string background_asset_ref;
    // 证据性质内部值，区分有证据、推断或说法。默认"evidence"。
    std::string evidence_status{"evidence"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 资料真实性内部状态，事实、说法、推断分别保存。默认空值。
    std::string truth_status{};
};

/*
 * 职责：地点间的方向和可选旅行耗时。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct TravelRoute {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 路线起点地点稳定标识。默认空串。
    std::string from_location_id;
    // 路线终点地点稳定标识。默认空串。
    std::string to_location_id;
    // 可选旅行分钟数，未知为空值，不补默认耗时。默认未知空值。
    std::optional<int> travel_minutes;
    // 该关系或路线是否允许双向解释/通行。默认true。
    bool bidirectional{true};
    // 证据性质内部值，区分有证据、推断或说法。默认"evidence"。
    std::string evidence_status{"evidence"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 职责：一个世界的地点和路线读取结果。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct MapView {
    // 当前世界的地点放置记录集合。默认空集合，不预填资料。
    std::vector<LocationPlacement> locations;
    // 当前世界的路线记录集合。默认空集合，不预填资料。
    std::vector<TravelRoute> routes;
};


/*
 * 职责：作者明确指定的两侧实体标识与预期修订。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct RelationEndpointSelection {
    // 关系起点实体的稳定标识。默认空串。
    std::string from_entity_id;
    // 作者选择起点实体时读取的修订。默认0。
    int from_revision{0};
    // 关系终点实体的稳定标识。默认空串。
    std::string to_entity_id;
    // 作者选择终点实体时读取的修订。默认0。
    int to_revision{0};
};


/*
 * 职责：审核时原子提交的一种地点或关系专用投影。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct CandidateGraphProjection {
    // 可选关系专用投影；与地点投影互斥。默认未知空值。
    std::optional<DirectedRelation> relation;
    // 可选地点专用投影；不能携带关系端点。默认未知空值。
    std::optional<LocationPlacement> location;
    // 作者明确选择的关系端点和两侧预期修订。默认未知空值。
    std::optional<RelationEndpointSelection> endpoints;
};

/*
 * 功能：校验时间线事件及其前因后果引用。
 * 参数：
 *   event：待校验的TimelineEvent值，按值持有，不修改调用者原对象。标识/世界/名称非空，名称及相对时间最多512字节，叙事顺序非负、真实性受支持；前提/因果/结果集合删除空项并去重，不推断故事时间。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<TimelineEvent> validateTimelineEvent(TimelineEvent event);
/*
 * 功能：校验有向关系、有效时间和可见性。
 * 参数：
 *   relation：待校验的DirectedRelation值，按值持有，不修改调用者原对象。标识/世界/不同端点完整，维度1—256字节，已知强度-100—100；时间、证据/真实性及可见性一致；空真实性按证据规范化，受限授权须非空，其他视图清空授权。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<DirectedRelation> validateDirectedRelation(DirectedRelation relation);
/*
 * 功能：校验地点的层级位置和可选地图坐标。
 * 参数：
 *   placement：待校验的LocationPlacement值，按值持有，不修改调用者原对象。地点非空且不是自身父节点；可选坐标须成对且非负，底图引用最多1024字节；证据/真实性一致，空真实性按证据规范化；不把未知坐标改为零。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<LocationPlacement> validateLocationPlacement(LocationPlacement placement);
/*
 * 功能：校验地点间路线、耗时和方向。
 * 参数：
 *   route：待校验的TravelRoute值，按值持有，不修改调用者原对象。路线及两个不同地点非空，已知旅行分钟为正，证据状态受支持；不校验地点存在或推测耗时。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<TravelRoute> validateTravelRoute(TravelRoute route);

} // namespace xuyan::domain
