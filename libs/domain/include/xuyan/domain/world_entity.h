#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：用户世界的可编辑资料条目与修订状态。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct WorldEntity {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 实体分类的内部协议值，显示层提供中文名称。默认空串。
    std::string kind;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 当前实体的完整别名列表，仅用于逐字身份线索。默认空集合，不预填资料。
    std::vector<std::string> aliases;
    // 用户维护的标签集合，用于检索，不嵌入样例。默认空集合，不预填资料。
    std::vector<std::string> tags;
    // 世界实体的可编辑说明正文。默认空串。
    std::string description;
    // 实体扩展属性JSON对象文本，由领域校验器限制。默认"{}"。
    std::string attributes_json{"{}"};
    // 人工审核的内部状态值，待审候选不能自动成为确认事实。默认"accepted"。
    std::string review_status{"accepted"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 软删除标志，写入接口维护，已发布历史不因此丢失。默认false。
    bool deleted{false};
};

/*
 * 职责：关键词与分类检索的当前页和分页状态。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct EntityPage {
    // 当前页的轻量条目集合，调用者只显示本页。默认空集合，不预填资料。
    std::vector<WorldEntity> items;
    // 分页的零基条目偏移，不是字节或码点位置。默认0。
    int offset{0};
    // 同一查询范围匹配的条目总数，单位为条目。默认0。
    int total{0};
    // 当前偏移之后是否仍有匹配条目。默认false。
    bool has_more{false};
};

/*
 * 职责：显式实体合并或拆分后的两侧条目和历史标识。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct EntityMergeResult {
    // 显式合并历史的稳定标识。默认空串。
    std::string merge_id;
    // 合并源实体的返回值快照。按对应值对象默认构造初始化。
    WorldEntity source;
    // 合并目标实体的返回值快照。按对应值对象默认构造初始化。
    WorldEntity target;
    // 该合并历史是否仍处于有效合并状态。默认true。
    bool active{true};
};

/*
 * 功能：校验世界实体的身份、类型与可编辑字段。
 * 参数：
 *   entity：待校验的WorldEntity值，按值持有，不修改调用者原对象。名称1—512字节且无禁用控制字符；支持的实体类别和审核状态；描述/扩展各不超过1MiB，扩展检查对象外形；别名/标签各最多128项，单项分别512/128字节；返回副本移除空项、排序去重，不解析完整JSON或自动合并同名实体。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<WorldEntity> validateEntity(WorldEntity entity);
/*
 * 功能：判断实体类型是否属于当前协议支持的集合。
 * 参数：
 *   kind：借用的内部实体类别，不能用中文显示标签代替。
 * 返回：受支持类别为true，未知或空值为false。
 * 失败：未知值正常返回false。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
bool isSupportedEntityKind(std::string_view kind);

} // namespace xuyan::domain
