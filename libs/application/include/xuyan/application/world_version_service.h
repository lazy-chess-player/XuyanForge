#pragma once

#include "xuyan/domain/world_version.h"

#include <filesystem>

namespace xuyan::application {

/*
 * 职责：冻结未删除实体的当前修订/检索范围修订为发布版本，并据此生成故事时间过滤后的历史快照。
 * 范围：发布成员不等于整套世界图或小说资产副本，快照的未决项单独保存。
 * 资源与生命周期：仅拥有数据库路径；每次同步调用创建并销毁局部仓储，返回值独立持有数据，不缓存版本。
 * 线程：调用线程执行，无后台任务；服务须覆盖调用有效期，销毁不能与调用并发。
 * 存储边界：包括查询在内的业务调用可创建父目录/数据库并迁移结构。
 * 异常边界：业务方法 try 内的标准异常转为 storage_error，实参构造和非标准异常仍可传播。
 */
class WorldVersionService {
public:
    /*
     * 功能：保存发布版本及历史快照操作使用的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在后续调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径构造/分配异常直接传播。
     * 副作用：仅初始化本对象，不保留调用方路径引用。
     */
    explicit WorldVersionService(std::filesystem::path database_path);
    /*
     * 功能：在一个事务内冻结世界所有未删除实体的头修订及当前检索范围修订，不按实体来源性质再筛选。
     * 参数：所有字符串均为输入，仅调用期间借用。
     *   command_id：非空幂等标识，其摘要前 24 位生成版本 ID；同命令须保持世界、父版本及当前成员摘要一致。
     *   world_id：非空世界 ID，须至少匹配一个未删除实体；查询不另外验证世界目录记录。
     *   parent_id：已有同世界父版本 ID，默认空表示无父版本，不自动取最新版本。
     * 返回：状态 published 的不可变版本，含成员及内容摘要、UTC 发布时间；同负载重放返回原发布版本。
     * 失败：命令/世界为空或成员为空为 validation_failed，父版本缺失/跨世界为 missing_context；
     *   发布后成员摘要变化会使原命令重用成为 command_conflict；打开/迁移/写入标准异常为 storage_error。
     * 副作用：同事务写版本、成员、固定检索范围修订及发布命令；不在此生成历史快照、不复制原文/图记录，
     *   打开可能创建/迁移结构，不覆盖旧版本或调用模型。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> publish(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id = {});
    /*
     * 功能：列出精确匹配世界 ID 的全部发布版本和完整成员，不分页。
     * 参数：world_id：输入世界稳定 ID，借用至返回；不预先校验世界存在，空值仍按精确值查询。
     * 返回：按发布时间、版本 ID 升序的自有列表；无匹配为成功空列表。
     * 失败：打开/迁移、查询或任一版本读取的标准异常为 storage_error。
     * 副作用：只读发布版本业务记录，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> list(const std::string& world_id);
    /*
     * 功能：读取已发布版本及其固定成员修订，不以当前实体头替代历史成员。
     * 参数：version_id：输入已有版本稳定 ID，仅调用期间借用；空值作为查询值，通常无匹配。
     * 返回：独立版本值，含成员、固定范围修订、摘要和 UTC 发布时间；缺失时不返回空版本。
     * 失败：版本缺失由读取异常转为 storage_error；数据库打开/迁移或其他读取标准异常同样返回该错误。
     * 副作用：只读发布版本业务记录，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> load(const std::string& version_id);
    /*
     * 功能：读取发布版本固定的实体/范围历史，按指定故事时间生成快照；当前不按人物权限过滤。
     * 参数：
     *   command_id：输入非空幂等标识，借用至返回；其摘要前 24 位生成快照 ID，同命令须复用版本/时间/快照摘要。
     *   world_version_id：输入非空已发布版本 ID，借用至返回，作为不可变基线。
     *   story_time：输入有符号 64 位世界故事刻度，包含生效/失效边界；没有额外非负限制，不是 UTC 或章节序号。
     * 返回：持久化快照（可无 included_members），含独立的 unresolved_entity_ids 及内容摘要；
     *   实体属性按当前字符串检测命中 story_time_unknown 标记时归未决，其余按发布时范围修订过滤；
     *   未设/未读到范围边界时不过滤，同负载命令可重放。
     * 失败：命令/版本 ID 为空为 validation_failed，命令负载变化为 command_conflict；版本或成员历史缺失、
     *   数据库打开/迁移及其他读写标准异常为 storage_error，时间值本身没有单独的范围校验。
     * 副作用：同事务写快照、成员、未决集合和命令；不改变发布版本，打开可能创建/迁移结构，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> prepareSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
private:
    /* 版本/历史快照数据库文件路径，无计量单位；初值为构造实参，无默认实参，空值仍保存。
     * 构造写入，各方法只读以打开局部仓储；拥有路径值至服务销毁，不持有版本缓存或数据库连接。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
