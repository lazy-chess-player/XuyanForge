#pragma once

#include "xuyan/domain/character_instance.h"

#include <filesystem>

namespace xuyan::application {

/* 人物入场与分支根绑定入口；拥有路径，同步构造值对象并交仓储，实例记忆独立于源卡片。 */
class CharacterInstanceService {
public:
    /*
     * 功能：绑定人物实例工作区。参数：database_path 为数据库路径，按值持有。
     * 返回：初始化路径。失败：分配异常传播，不验证存储。
     * 副作用：无读写；线程：同步构造，不创建后台任务。
     */
    explicit CharacterInstanceService(std::filesystem::path database_path);
    /*
     * 功能：从指定卡片版本创建独立人物实例，未适配能力/装备登记冲突。
     * 参数：command_id 为幂等命令；blueprint_id、blueprint_version 为卡片及指定版本；
     * world_version_id、snapshot_id 为入场基线；adaptation_json 为能力/装备映射 JSON 对象；
     * knowledge_policy 为知识策略内部值，合法集合由领域/仓储校验。
     * 返回：实例，存在适配冲突时仍可返回 needs_resolution 状态。
     * 失败：适配对象/卡片/基线/策略无效或存储错误返回 Result；卡片服务构造异常可能传播。
     * 副作用：写实例和命令，不回写源卡片；线程：同步，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> instantiate(
        const std::string& command_id, const std::string& blueprint_id, int blueprint_version,
        const std::string& world_version_id, const std::string& snapshot_id,
        const std::string& adaptation_json, const std::string& knowledge_policy);
    /*
     * 功能：读取实例及其独立记忆。参数：instance_id 为实例稳定标识。
     * 返回：完整实例值。失败：实例缺失或数据库错误返回 Result。
     * 副作用：只读实例，私有记忆不得作为公共日志；线程：调用线程同步查询。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> load(const std::string& instance_id);
    /*
     * 功能：列出指定入场版本的实例。参数：world_version_id 为世界版本标识。
     * 返回：实例列表，无实例成功返回空列表。失败：数据库错误返回 Result。
     * 副作用：只读实例；线程：同步，结果为自有值对象。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> list(const std::string& world_version_id);
    /*
     * 功能：编辑某实例私有记忆。参数：command_id 为幂等命令；instance_id 为目标；
     * expected_revision 为现有实例修订；memory_json 为有效 JSON 对象，解析深度最多 32、节点最多 10000。
     * 返回：新修订实例。失败：JSON、实例或修订/存储错误返回 Result。
     * 副作用：写实例记忆与命令，不修改卡片；线程：同步事务。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    /*
     * 功能：固定分支根的入场基线并生成稳定摘要。
     * 参数：command_id 为幂等命令；branch_id 为分支；world_version_id 为发布版本；snapshot_id 为历史快照；
     * history_mode 为历史模式内部值；character_instance_ids 为实例标识集合，按值接收并排序后计算摘要。
     * 返回：根绑定值。失败：缺失/不匹配基线、重复或不合法实例、重绑或存储冲突返回 Result。
     * 副作用：持久化根绑定与命令，不静默更换已有根；线程：同步，传入字符串和容器不再借用。
     */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, std::string branch_id, std::string world_version_id,
        std::string snapshot_id, std::string history_mode, std::vector<std::string> character_instance_ids);
private:
    /* 实例及分支数据库路径；构造后只读，与服务同寿命，不持有实例或 SQL 连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
