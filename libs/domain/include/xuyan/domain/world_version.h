#pragma once

#include "xuyan/domain/scenario.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：不可变世界版本固定的实体/检索范围历史修订。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct WorldVersionMember {
    // 被引用的世界实体稳定标识。默认空串。
    std::string entity_id;
    // 版本成员固定的实体历史修订。默认0。
    int entity_revision{0};
    // 版本成员固定的可见范围历史修订。默认0。
    int retrieval_scope_revision{0};
};

/*
 * 职责：已发布世界版本的固定成员和内容摘要。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct WorldVersion {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 父世界版本稳定标识，首次发布时为空。默认空串。
    std::string parent_id;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"published"。
    std::string status{"published"};
    // 不可变内容的规范摘要，用于版本和快照一致性。默认空串。
    std::string content_hash;
    // 世界版本发布的UTC时间文本。默认空串。
    std::string published_at;
    // 发布时固定的实体及可见范围修订集合。默认空集合，不预填资料。
    std::vector<WorldVersionMember> members;
};

/*
 * 职责：指定故事时间下的有效成员及尚未确定归属的成员。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct HistoricalSnapshot {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所绑定的不可变世界版本标识。默认空串。
    std::string world_version_id;
    // 此快照明确绑定的故事时间坐标，单位由世界定义；非可选整数，默认0不表示未知或无过滤。
    std::int64_t story_time{0};
    // 不可变内容的规范摘要，用于版本和快照一致性。默认空串。
    std::string content_hash;
    // 在指定故事时间明确有效的版本成员。默认空集合，不预填资料。
    std::vector<WorldVersionMember> included_members;
    // 尚无法确定故事时间归属的实体标识集合。默认空集合，不预填资料。
    std::vector<std::string> unresolved_entity_ids;
};

/*
 * 功能：校验已发布世界版本与实体修订清单。
 * 参数：
 *   version：待校验的WorldVersion值，按值持有，不修改调用者原对象。世界/版本非空、发布状态正确、摘要64字符、成员非空；成员修订合法、实体标识不重复；按标识排序，不验证实际数据库历史。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<WorldVersion> validateWorldVersion(WorldVersion version);
/*
 * 功能：校验指定故事时间的历史快照及未决实体记录。
 * 参数：
 *   snapshot：待校验的HistoricalSnapshot值，按值持有，不修改调用者原对象。快照/世界版本非空、摘要64字符；已包括成员按标识排序，未定归属标识排序去重；不推断未知故事时间或查询引用。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<HistoricalSnapshot> validateHistoricalSnapshot(HistoricalSnapshot snapshot);

} // namespace xuyan::domain
