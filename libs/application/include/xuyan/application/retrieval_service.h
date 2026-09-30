#pragma once

#include "xuyan/domain/retrieval.h"

#include <filesystem>

namespace xuyan::application {

/*
 * 职责：维护实体检索权限/故事时间范围，并进行有界词法检索；授权身份由调用方提供。
 * 资源与生命周期：只拥有数据库路径，不缓存实体或索引；每次同步调用的局部仓储在返回前销毁，结果独立持有数据。
 * 线程：全部在调用线程执行，无后台线程；服务须覆盖调用有效期，销毁不能与调用并发。
 * 存储边界：包括查询在内的业务调用可能创建父目录/数据库并迁移结构。
 * 异常边界：try 内的标准异常转为 storage_error，实参构造及非标准异常仍可传播。
 */
class RetrievalService {
public:
    /*
     * 功能：保存检索业务使用的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在后续调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径构造/分配异常直接传播。
     * 副作用：仅初始化本对象，不保留调用方路径引用。
     */
    explicit RetrievalService(std::filesystem::path database_path);
    /*
     * 功能：按独立范围修订创建或替换实体的检索权限及故事时间区间。
     * 参数：
     *   command_id：输入幂等标识，仅调用期间借用；调用方应提供非空值，同标识须复用相同规范化范围。
     *   scope：输入范围副本；entity_id 非空且指向未删除实体；valid_from/valid_to 为世界定义的故事刻度，
     *     空值表示无该侧边界，已知下界不得晚于上界。visibility 仅 public、author、restricted；
     *     actor_grants 删除空项、排序去重，restricted 须至少一项，其他可见性清空授权；scope.revision 被重算。
     *   expected_revision：输入检索范围自身的当前修订，非实体修订；首次创建为 0，已有范围须匹配现存正修订。
     * 返回：规范化范围，写入修订为 expected_revision + 1；同负载重放读取现存范围，不固定为历史修订。
     * 失败：字段/区间/授权无效为 validation_failed，实体缺失为 missing_context，过期范围为 revision_conflict，
     *   命令负载冲突为 command_conflict，数据库打开/迁移或其他标准异常为 storage_error。
     * 副作用：同事务更新范围、替换授权并追加范围/授权历史和命令；打开可能创建/迁移结构，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    /*
     * 功能：先按世界、时间和可见性筛选未删除实体，再在有界候选集内做词法评分。
     * 参数：request：输入请求副本，不保留外部引用；world_id 必须非空；query 最多 512 字节，空串选全部候选；
     *   story_time 默认空不限制时间，有值时用世界故事刻度对闭区间过滤；author_view 默认 false，
     *   此时 actor_id 须非空，仅允许未设范围/公开/明确授权条目；true 时略过权限过滤，调用方负责作者身份，仍过滤时间。
     *   limit 默认 20，合法范围 1—100 条。无范围条目视为公开且不限时间，查询不预先校验世界/人物存在。
     * 返回：只扫描筛选后按实体 ID 排序的前 5000 项，按累计词法分降序、名称升序返回至多 limit 条；
     *   名称/每个别名/每个标签/说明/属性命中分别加 100/90/50/30/10，ASCII 忽略大小写，中文按 UTF-8 子串匹配；
     *   空查询每项为 1 分，无匹配为成功空列表；分数不代表语义置信度，也不保证覆盖全库最佳命中。
     * 失败：世界为空、查询/数量越界或人物视角缺 ID 为 validation_failed；打开/迁移/任一候选读取标准异常为 storage_error。
     * 副作用：只读业务资料，打开可能创建/迁移结构；不发送文本，不验证传入身份的授权真实性。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieve(
        xuyan::domain::RetrievalRequest request);
private:
    /* 检索数据库文件路径，无计量单位；初值为构造实参，无默认实参，空值原样保存。
     * 构造写入，各方法只读以打开局部仓储；拥有路径值至服务销毁，不缓存查询结果或连接。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
