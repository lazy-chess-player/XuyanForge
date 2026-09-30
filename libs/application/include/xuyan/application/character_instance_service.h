#pragma once

#include "xuyan/domain/character_instance.h"

#include <filesystem>

namespace xuyan::application {

/*
 * 职责：将人物卡固定版本适配到世界历史快照，保存独立记忆，并固定分支根的人物修订。
 * 资源与生命周期：只拥有数据库路径；局部卡片服务/仓储随同步调用结束销毁，返回实例独立持有数据。
 * 线程：全部在调用线程执行，无后台任务；服务须覆盖调用有效期，销毁不能与调用并发。
 * 存储边界：业务方法打开仓储时可创建父目录/数据库并迁移结构；查询只读实例业务数据。
 * 异常边界：try 内的标准异常转为 storage_error；实参复制、try 外的 JSON/值构造及非标准异常可传播。
 */
class CharacterInstanceService {
public:
    /*
     * 功能：保存人物实例及分支根后续操作的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在业务调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径构造/分配异常直接传播。
     * 副作用：仅初始化本对象，不保留调用方路径引用。
     */
    explicit CharacterInstanceService(std::filesystem::path database_path);
    /*
     * 功能：读取卡片固定版本，以适配对象中键的存在性登记未映射能力/装备，再创建独立入场实例。
     * 参数：所有字符串均为输入，仅调用期间借用。
     *   command_id：幂等命令标识，调用方应提供非空值；其 SHA-256 前 24 位生成实例 ID。
     *   blueprint_id：已有卡片稳定 ID；卡片读取不排除已删除版本。
     *   blueprint_version：固定正版本号，从 1 开始；即使卡片读取支持负数查最新，实例校验仍拒绝负数/0。
     *   world_version_id：非空发布版本 ID，必须与 snapshot_id 所属版本一致。
     *   snapshot_id：非空已保存历史快照 ID，作为入场点。
     *   adaptation_json：JSON 对象正文，解析深度最多 16、节点最多 2000；允许空对象 {}，此时未映射项登记冲突。
     *     键匹配能力的字符串 key 和装备名称；当前只检查键是否存在，不校验映射值的业务含义。
     *   knowledge_policy：知识策略内部值，仅 strict、public_only、author_selected 合法；无默认实参。
     * 返回：修订 1、初始记忆 {} 的实例；冲突为空为 ready，否则成功返回 needs_resolution；
     *   同命令同负载重放读取现存实例（可能已更新记忆），不保证仍为初始修订。
     * 失败：适配 JSON/版本/策略无效、卡片缺失、快照不匹配、命令冲突或存储失败返回 Result；
     *   快照缺失由仓储读异常转为 storage_error，前置卡片读取的打开/迁移异常在 try 外传播。
     * 副作用：读取卡片并同事务写实例、冲突及命令，打开可能创建/迁移结构；不回写卡片，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> instantiate(
        const std::string& command_id, const std::string& blueprint_id, int blueprint_version,
        const std::string& world_version_id, const std::string& snapshot_id,
        const std::string& adaptation_json, const std::string& knowledge_policy);
    /*
     * 功能：读取一个实例的适配、知识策略、独立记忆及冲突列表。
     * 参数：instance_id：输入实例稳定 ID，仅调用期间借用；空值作为查询值，通常无匹配。
     * 返回：独立完整实例值，包含私有记忆；调用方须控制展示和日志范围。
     * 失败：实例缺失、数据库打开/迁移或读取的标准异常转为 storage_error。
     * 副作用：只读实例；打开可能创建/迁移结构，不对记忆作公共日志输出。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> load(const std::string& instance_id);
    /*
     * 功能：查询指定发布版本的全部实例，包含就绪和待解决冲突的实例，不分页。
     * 参数：world_version_id：输入精确匹配的世界版本 ID，仅调用期间借用；不预先校验该版本存在。
     * 返回：按名称、实例 ID 排序的完整值列表（含私有记忆）；无匹配为成功空列表。
     * 失败：数据库打开/迁移或查询的标准异常转为 storage_error。
     * 副作用：只读实例；打开可能创建/迁移结构。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> list(const std::string& world_version_id);
    /*
     * 功能：解析并规范化实例记忆对象，按实例修订防止覆盖并发编辑。
     * 参数：
     *   command_id：输入幂等标识，调用期间借用；重放须保持实例、预期修订及规范化记忆一致。
     *   instance_id：输入已有实例 ID，调用期间借用。
     *   expected_revision：输入当前实例正修订（新实例为 1），必须与存储一致。
     *   memory_json：输入完整替换的 JSON 对象正文，借用至返回；深度最多 32、节点最多 10000，{} 表示清空记忆内容。
     * 返回：记忆替换且修订加 1 的实例；命令重放返回现存实例值。
     * 失败：非法 JSON 为 validation_failed，过期修订为 revision_conflict，不同负载重放为 command_conflict；
     *   实例缺失或打开/迁移/写入标准异常为 storage_error，try 外解析/分配异常仍可传播。
     * 副作用：同事务更新记忆/修订和命令；不修改卡片或实例冲突状态，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    /*
     * 功能：将已有分支的根绑定到发布版本、快照及就绪实例，并固定每个实例的当前修订。
     * 参数：
     *   command_id：输入幂等标识，仅调用期间借用；同标识必须复用相同入场配置。
     *   branch_id：输入非空已有分支 ID，按值接收；分支不能已有根绑定（同命令重放除外）。
     *   world_version_id：输入非空发布版本 ID，按值接收，须与快照和全部实例的版本一致。
     *   snapshot_id：输入非空已存快照 ID，按值接收，须与全部实例的入场快照一致。
     *   history_mode：输入内部模式，按值接收，仅 original_constrained、branching、sandbox 合法。
     *   character_instance_ids：输入非空实例 ID 集合，按值接收并排序；禁止重复，每项须存在且状态为 ready；无显式数量上限。
     * 返回：持久化根绑定，root_hash 由仓储结合实例 ID/修订及卡片 ID/版本重算；同命令重放返回已有绑定。
     * 失败：模式/重复项/实例状态或基线不匹配为 validation_failed，分支缺失为 missing_context，重绑为 revision_conflict，
     *   不同命令负载为 command_conflict；快照/实例缺失及存储标准异常为 storage_error，try 外排序/摘要分配异常可传播。
     * 副作用：同事务写根绑定、实例关联和修订固定记录及命令；不修改分支头状态，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, std::string branch_id, std::string world_version_id,
        std::string snapshot_id, std::string history_mode, std::vector<std::string> character_instance_ids);
private:
    /* 实例/分支数据库文件路径，无计量单位；初值为构造实参，空值原样保存（没有默认实参）。
     * 构造写入，各方法只读以创建局部仓储；拥有路径值至服务销毁，不拥有实例缓存或 SQL 连接。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
