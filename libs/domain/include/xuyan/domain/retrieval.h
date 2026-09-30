#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_entity.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：世界实体的故事时间与人物权限范围。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct EntityRetrievalScope {
    // 被引用的世界实体稳定标识。默认空串。
    std::string entity_id;
    // 可选故事时间下界，空值表示未限定，不用现实UTC替代。默认未知空值。
    std::optional<std::int64_t> valid_from;
    // 可选故事时间上界，空值表示未限定。默认未知空值。
    std::optional<std::int64_t> valid_to;
    // 人物可见性的内部策略值，由检索器校验。默认"public"。
    std::string visibility{"public"};
    // 获得显式访问授权的人物标识集合。默认空集合，不预填资料。
    std::vector<std::string> actor_grants;
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 职责：一次有界的世界资料关键词检索请求。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct RetrievalRequest {
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 用户提供的检索关键词，不拼接SQL。默认空串。
    std::string query;
    // 故事时间坐标，单位由世界体系定义；可选类型的空值表示未知或不设置过滤。默认未知空值。
    std::optional<std::int64_t> story_time;
    // 请求者或执行者人物稳定标识。默认空串。
    std::string actor_id;
    // 是否采用作者权限视角，由调用方明确请求。默认false。
    bool author_view{false};
    // 分页返回的条目上限，单位为条目。默认20。
    int limit{20};
};

/*
 * 职责：通过世界/时间/权限过滤的词法命中。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct RetrievalHit {
    // 检索命中的完整实体副本。按对应值对象默认构造初始化。
    WorldEntity entity;
    // 关键词命中得分，仅表示词法匹配，不代表语义置信度。默认0。
    int lexical_score{0};
};

/*
 * 功能：校验实体可见性、时间范围和授权角色。
 * 参数：
 *   scope：待校验的EntityRetrievalScope值，按值持有，不修改调用者原对象。实体/可见性有效，已知时间下界不晚于上界；授权标识移除空项、排序去重；受限视图必须有授权，其他视图清空授权；不执行检索。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<EntityRetrievalScope> validateEntityRetrievalScope(EntityRetrievalScope scope);
/*
 * 功能：校验世界实体检索请求与结果数量上限。
 * 参数：
 *   request：待校验的RetrievalRequest值，按值持有，不修改调用者原对象。世界非空，查询最多512字节，数量1—100条；非作者视角必须明确人物；不读取资料或证明调用者已获作者权限。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<RetrievalRequest> validateRetrievalRequest(RetrievalRequest request);

} // namespace xuyan::domain
