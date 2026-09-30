#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：固定人物卡/世界版本的人物实例及其适配、记忆和冲突。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct CharacterInstance {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所绑定人物卡的稳定标识。默认空串。
    std::string blueprint_id;
    // 所绑定人物卡的固定版本号，不随卡片最新版自动变化。默认0。
    int blueprint_version{0};
    // 所绑定的不可变世界版本标识。默认空串。
    std::string world_version_id;
    // 所绑定的历史快照稳定标识，实例和分支校验要求非空。默认空串。
    std::string snapshot_id;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 人物适配当前世界的JSON字段，和原人物卡分开保存。默认"{}"。
    std::string adaptation_json{"{}"};
    // 人物知识限制的内部策略值，界面应映射中文名称。默认"strict"。
    std::string knowledge_policy{"strict"};
    // 人物实例记忆JSON正文，不修改小说原文。默认"{}"。
    std::string memory_json{"{}"};
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"ready"。
    std::string status{"ready"};
    // 人物适配时尚未解决的冲突说明列表。默认空集合，不预填资料。
    std::vector<std::string> conflicts;
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 职责：固定分支根的世界与人物绑定，防止运行中偷换基线。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct BranchRootBinding {
    // 推演分支的稳定标识。默认空串。
    std::string branch_id;
    // 所绑定的不可变世界版本标识。默认空串。
    std::string world_version_id;
    // 所绑定的历史快照稳定标识，实例和分支校验要求非空。默认空串。
    std::string snapshot_id;
    // 历史处理策略的内部值，决定独立分支等语义。默认"branching"。
    std::string history_mode{"branching"};
    // 分支根明确绑定的人物实例标识集合。默认空集合，不预填资料。
    std::vector<std::string> character_instance_ids;
    // 固定分支根绑定内容的摘要，供后续一致性校验。默认空串。
    std::string root_hash;
};

/*
 * 功能：校验人物实例与其世界版本、知识策略等绑定信息。
 * 参数：
 *   instance：待校验的CharacterInstance值，按值持有，不修改调用者原对象。实例、人物卡、世界版本、快照和名称均须存在，卡片版本为正；适配/记忆须为对象外形、知识策略受支持；按去重后的冲突列表重新计算就绪状态，不查询实际引用。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<CharacterInstance> validateCharacterInstance(CharacterInstance instance);
/*
 * 功能：校验分支根节点关联的世界和人物快照。
 * 参数：
 *   binding：待校验的BranchRootBinding值，按值持有，不修改调用者原对象。分支/版本/快照非空、摘要64字符、人物集合非空、历史模式受支持；排序人物标识并拒绝重复；不验证引用是否实际存在。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<BranchRootBinding> validateBranchRootBinding(BranchRootBinding binding);

} // namespace xuyan::domain
