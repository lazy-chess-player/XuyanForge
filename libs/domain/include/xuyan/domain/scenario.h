#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xuyan::domain {

/* 职责：领域与服务之间稳定传递失败分类；枚举不是界面显示文案，由接收层显示中文原因。 */
enum class ErrorCode {
    // 字段、范围、协议或输入结构不符合对应接口契约。
    validation_failed,
    // 乐观并发预期修订已经过期，不得覆盖最新记录。
    revision_conflict,
    // 行动违反当前规则或权限，需作者调整而非自动重试。
    rule_conflict,
    // 缺少必需的世界、人物、版本或原文上下文。
    missing_context,
    // 数据库、资产或底层读写失败，是否重试由错误对象注明。
    storage_error,
    // 同一幂等命令标识用于不同输入，不能重复写入。
    command_conflict,
    // 跨数据库与系统凭据的补偿失败；必须提示人工核对，禁止当作普通可重试读写错误。
    credential_consistency_failed
};

/* 职责：值语义失败信息，不拥有异常、文件或网络资源；消息和建议不得含密钥或原始模型响应。 */
struct Error {
    // 失败分类，默认输入校验失败；调用者按分类决定冲突/重试策略。
    ErrorCode code{ErrorCode::validation_failed};
    // 面向用户的中文原因，默认空串，由失败产生处填写。
    std::string message;
    // 是否允许在用户介入后重试，默认false；不授权自动重新发送未知请求。
    bool retryable{false};
    // 中文处理建议，默认空串；不作为可执行指令。
    std::string suggested_action;
};

/* 职责：独立拥有成功值或错误的结果容器；工厂保证二者互斥，不借用调用者对象。
 * 约束：公开成员保留既有值接口，直接聚合构造者须维持互斥；ok仅检查是否有成功值。
 */
template <typename T>
struct Result {
    // 成功值，默认空；成功工厂移动输入到此成员，消费者仅在ok为true时读取。
    std::optional<T> value;
    // 失败详情，默认空；失败工厂填入，成功结果不带错误。
    std::optional<Error> error;

    /* 功能：构造成功结果，排除错误值。
     * 参数：result为按值传入的业务结果，将移动到返回容器。
     * 返回：value已设置、error为空的Result。失败：T移动/分配可传播异常。
     * 副作用：仅移动本地参数，不访问外部资源。
     */
    static Result success(T result) { return Result{std::move(result), std::nullopt}; }
    /* 功能：构造失败结果，排除成功值。
     * 参数：failure为按值传入的失败信息，消息须符合中文与敏感数据边界。
     * 返回：value为空、error已设置的Result。失败：值分配可传播异常。
     * 副作用：只移动本地错误，不执行修复或自动重试。
     */
    static Result failure(Error failure) { return Result{std::nullopt, std::move(failure)}; }
    /* 功能：检查结果是否有成功值。参数：无。返回：有value为true，否则false。
     * 失败：不抛异常。副作用：只读本对象，不验证公开成员互斥性或业务数据。
     */
    [[nodiscard]] bool ok() const noexcept { return value.has_value(); }
};

/* 职责：提交快照中的单个人物运行状态，不预置人物；字段对应当前受限规则能力，并非通用规则系统。 */
struct CharacterState {
    // 人物稳定标识，默认空，由显式状态组装填写；关联以此而不是名称为准。
    std::string id;
    // 用户提供的显示名，默认空；不作为固定人物或自动合并线索。
    std::string name;
    // 是否获知当前规则中的关闭信息，默认false，仅合法传播操作改变。
    bool knows_gate_closure{false};
    // 是否获知当前规则中的物品真伪信息，默认false，仅合法检查/传播改变。
    bool knows_seal_forgery{false};
    // 当前受限规则的信任计数，默认0、无物理单位；合法信息传播可递增。
    int trust{0};
};

/* 职责：独立持有一个分支提交的全部运行状态，供摘要、重放和持久化。
 * 生命周期：值对象，无外部资源；默认人物集合为空，不自动提供世界或剧情。
 * 边界：当前操作字段保留已有快照语义；通用用户定义规则另由后续任务实现。
 */
struct ScenarioState {
    // 状态修订，默认0；成功规则操作递增，用于识别不同状态。
    int revision{0};
    // 已提交回合序号，默认0；由回合编排路径更新。
    int turn{0};
    // 场景经过的离散时间刻数，默认0；刻单位由当前规则定义，不是秒或UTC。
    int elapsed_ticks{0};
    // 当前唯一受控物品的持有人标识，默认空表示未设置，不预填人物。
    std::string seal_holder_id;
    // 受控物品是否已检查，默认false；合法检查操作置真。
    bool seal_inspected{false};
    // 分支是否暂停，默认false，由会话控制路径更新。
    bool paused{false};
    // 分支是否结束，默认false，和暂停独立。
    bool completed{false};
    // 已提交叙述正文，默认空；不作为规则或可执行指令。
    std::string narration;
    // 本状态拥有的人物副本集合，默认空；只有调用者显式组装，不包含内置资料。
    std::vector<CharacterState> characters;
};

/* 职责：某个分支提交及完整状态的只读值快照；字符串和状态由对象独立拥有。 */
struct CommitView {
    // 所属分支标识，默认空。
    std::string branch_id;
    // 此提交稳定标识，默认空。
    std::string commit_id;
    // 上一提交标识，根提交为空。
    std::string parent_commit_id;
    // 规范化完整状态摘要，加载时核对，默认空。
    std::string state_hash;
    // 当前提交的完整值快照，不借用仓储连接。
    ScenarioState state;
};

/* 职责：分支元数据值对象，关联不可变提交链，不拥有仓储。 */
struct BranchInfo {
    // 分支稳定标识，默认空。
    std::string id;
    // 作者输入的分支显示名称，默认空。
    std::string name;
    // 父分支标识，根分支为空。
    std::string parent_id;
    // 分叉时固定的提交标识，默认空。
    std::string fork_commit_id;
    // 当前头提交标识，提交成功后由仓储推进。
    std::string head_commit_id;
};

/* 职责：当前受限领域规则允许的操作类型；内部协议稳定，不是预置剧情或通用规则完工声明。 */
enum class OperationType {
    // 持有人检查受控物品，获得相应知识。
    inspect_seal,
    // 将已知关闭信息向明确目标传播。
    reveal_gate_secret,
    // 将已知真伪信息向明确目标传播。
    reveal_seal_forgery,
    // 当前持有人明确同意后向目标转移受控物品。
    transfer_seal
};

/* 职责：一次待校验的规则操作值，不能跳过applyOperation直接修改状态。 */
struct ProposedOperation {
    // 操作类型，默认检查；调用者须显式选择符合意图的类型。
    OperationType type{OperationType::inspect_seal};
    // 行动者稳定标识，默认空，须在输入状态中存在。
    std::string actor_id;
    // 接收者稳定标识，默认空；传播和转移操作须存在有效接收者。
    std::string target_id;
    // 当前持有人是否明确同意转移，默认false，不能据推断置真。
    bool holder_consented{false};
};

/* 功能：在输入状态的独立副本上执行当前支持的操作，并校验人物知识和物品权限。
 * 参数：input为借用的完整输入状态；operation为借用的待执行意图，两者均不修改。
 * 返回：成功为修订递增的状态副本；不存在的行动者/目标、知识不足或转移权限不足返回rule_conflict。
 * 失败：未知枚举值、修订或信任计数溢出返回rule_conflict；值复制可抛分配异常。
 * 副作用：只修改返回副本，不写数据库、不调用模型、不提交回合。
 */
Result<ScenarioState> applyOperation(const ScenarioState& input, const ProposedOperation& operation);
/* 功能：生成现有摘要协议使用的确定性状态文本，人物按稳定标识排序。
 * 参数：state为借用的状态，不修改人物原顺序。返回：字段分隔的文本副本，不是可导入JSON。
 * 失败：复制/缓冲分配可抛异常。副作用：仅操作临时副本，既有摘要格式不得静默改变。
 */
std::string canonicalState(const ScenarioState& state);
/* 功能：计算当前规范状态文本的SHA-256摘要。
 * 参数：state为借用完整状态。返回：64字符小写十六进制摘要。
 * 失败：规范化和缓冲分配可抛异常。副作用：无持久化或网络操作。
 */
std::string stateHash(const ScenarioState& state);
/* 功能：按稳定标识读取状态内人物，不按显示名猜测。
 * 参数：state为借用的状态；id为调用期间借用的目标标识。
 * 返回：首个匹配人物的非拥有const指针，不存在返回nullptr；状态或容器变动后指针可能失效。
 * 失败：无业务错误结果。副作用：只读输入，不创建人物。
 */
const CharacterState* findCharacter(const ScenarioState& state, const std::string& id);

} // namespace xuyan::domain
