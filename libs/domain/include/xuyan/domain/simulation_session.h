#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：会话中一个人物与提供商/模型的显式绑定。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ActorModelBinding {
    // 请求者或执行者人物稳定标识。默认空串。
    std::string actor_id;
    // 冻结或绑定的模型连接稳定标识。默认空串。
    std::string provider_connection_id;
    // 提供商的真实模型标识，传输时保持原值。默认空串。
    std::string model_id;
};

/*
 * 职责：模型提出或导演输入的可校验行为意图。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ActorIntent {
    // 请求者或执行者人物稳定标识。默认空串。
    std::string actor_id;
    // 生成上下文或意图时冻结的输入提交标识。默认空串。
    std::string input_commit_id;
    // 人物发言文本，和确定性状态操作分开保存。默认空串。
    std::string speech;
    // 受支持操作的内部协议值；默认speak表示仅发言。默认"speak"。
    std::string operation{"speak"};
    // 操作目标稳定标识，无目标时为空。默认空串。
    std::string target_id;
    // 持有人是否明确同意涉及其物品的操作。默认false。
    bool holder_consented{false};
    // 本回合是否请求结束场景，仍须经过规则校验。默认false。
    bool ends_scene{false};
    // 可公开的行为理由，不包含人物秘密推理过程。默认空串。
    std::string public_reason;
};

/*
 * 职责：已过滤人物可见事实的请求上下文。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ActorContext {
    // 请求者或执行者人物稳定标识。默认空串。
    std::string actor_id;
    // 生成上下文或意图时冻结的输入提交标识。默认空串。
    std::string input_commit_id;
    // 已按人物知识和权限过滤的事实文本集合。默认空集合，不预填资料。
    std::vector<std::string> visible_facts;
    // 供提供商传输使用的序列化角色上下文。默认空串。
    std::string serialized;
};

/*
 * 职责：持久化模型请求身份、终态和实际词元用量。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ProviderCallRecord {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 关联推演回合的稳定标识。默认空串。
    std::string turn_id;
    // 冻结或绑定的模型连接稳定标识。默认空串。
    std::string provider_connection_id;
    // 提供商的真实模型标识，传输时保持原值。默认空串。
    std::string model_id;
    // 冻结模型请求内容摘要，用于核对重复或未知调用。默认空串。
    std::string request_hash;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认空串。
    std::string status;
    // 实际返回的输入词元数，不能用字符数冒充。默认0。
    int input_tokens{0};
    // 实际返回的输出词元数。默认0。
    int output_tokens{0};
    // 调用失败的内部分类，用于恢复和未知状态处理。默认空串。
    std::string failure_kind;
};

/*
 * 职责：单回合意图、状态提交和叙事的分离记录。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct SimulationTurn {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属推演会话稳定标识。默认空串。
    std::string session_id;
    // 本会话中从1开始的回合顺序号，预留回合时分配；默认0表示未预留。
    int ordinal{0};
    // 生成上下文或意图时冻结的输入提交标识。默认空串。
    std::string input_commit_id;
    // 本回合已原子提交的结果提交标识，未提交时为空。默认空串。
    std::string committed_commit_id;
    // 请求者或执行者人物稳定标识。默认空串。
    std::string actor_id;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"requesting"。
    std::string status{"requesting"};
    // 已校验的人物行为意图，不能直接信任模型JSON。按对应值对象默认构造初始化。
    ActorIntent intent;
    // 待审叙事，与已提交状态事实分开。默认空串。
    std::string draft_narration;
    // 已提交回合的最终叙述。默认空串。
    std::string final_narration;
    // 该步骤或回合的中文失败说明，不存密钥或完整模型原始响应。默认空串。
    std::string error_message;
    // 本回合的模型调用记录及用量。按对应值对象默认构造初始化。
    ProviderCallRecord call;
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 职责：持久化会话、角色绑定、额度与停止请求。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct SimulationSession {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 推演分支的稳定标识。默认空串。
    std::string branch_id;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"ready"。
    std::string status{"ready"};
    // 会话允许的最大回合数，达到上限停止。默认20。
    int max_turns{20};
    // 是否显式启用连续推进，默认单步。默认false。
    bool continuous{false};
    // 连续无进展回合的停止阈值。默认3。
    int no_progress_limit{3};
    // 会话允许的模型调用次数硬上限。默认120。
    int max_calls{120};
    // 会话已完成或计入消费的调用次数。默认0。
    int used_calls{0};
    // 已为在途回合预留的调用次数。默认0。
    int reserved_calls{0};
    // 中断后结果无法核实的调用次数，不能自动重发。默认0。
    int unknown_calls{0};
    // 用户暂停请求标志，在途回合结束后生效。默认false。
    bool pause_requested{false};
    // 用户已请求取消的标志，调度在检查点停止后续步骤。默认false。
    bool cancel_requested{false};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 本会话明确绑定的人物与模型连接集合。默认空集合，不预填资料。
    std::vector<ActorModelBinding> actors;
    // 会话已持久化的回合记录，不生成固定响应。默认空集合，不预填资料。
    std::vector<SimulationTurn> turns;
};

/*
 * 功能：校验模型提出的人物意图和所引用的输入提交。
 * 参数：
 *   intent：待校验的ActorIntent值，按值持有，不修改调用者原对象。行动者及输入提交非空；发言最多64KiB、公开理由最多16KiB；操作受当前白名单限制，需要接收者的操作必须带目标；不执行状态操作。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ActorIntent> validateActorIntent(ActorIntent intent);
/*
 * 功能：校验会话状态、额度和回合记录的一致性。
 * 参数：
 *   session：待校验的SimulationSession值，按值持有，不修改调用者原对象。会话/分支非空；回合1—10000、无进展阈值1—100、次数1—1000000、人物2—16个；每个模型绑定完整且人物不重复；不查询连接授权或推进回合。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<SimulationSession> validateSimulationSession(SimulationSession session);
/*
 * 功能：将受支持的人物意图转为确定性的情景操作。
 * 参数：
 *   intent：借用人物意图，先校验再映射，不修改输入。
 * 返回：受支持的确定性领域操作；纯发言或非法意图返回validation_failed，不推进状态。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ProposedOperation> toProposedOperation(const ActorIntent& intent);

} // namespace xuyan::domain
