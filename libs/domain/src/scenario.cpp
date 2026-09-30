#include "xuyan/domain/scenario.h"
#include "xuyan/domain/hash.h"

#include <algorithm>
#include <sstream>
#include <limits>

namespace xuyan::domain {
namespace {

/* 功能：在状态副本中按稳定标识读取可修改人物，不猜测名称。
 * 参数：state为调用者拥有的可变状态；id为调用期间借用的稳定标识。
 * 返回：首个匹配项的非拥有指针，无匹配为nullptr；容器重分配或状态销毁后失效。
 * 失败：只读扫描，无业务错误容器。副作用：不创建或修改人物，调用者负责指针使用期。
 */
CharacterState* mutableCharacter(ScenarioState& state, const std::string& id) {
    const auto it = std::find_if(state.characters.begin(), state.characters.end(),
                                 [&id](const CharacterState& item) { return item.id == id; });
    return it == state.characters.end() ? nullptr : &*it;
}

/* 功能：组装当前操作需要作者介入的规则错误。
 * 参数：message为按值持有的中文原因，移动到结果中，不应包含私有模型响应。
 * 返回：不可自动重试的rule_conflict及中文建议。
 * 失败：字符串分配可抛异常。副作用：只组装值，不执行回合或自动恢复。
 */
Error ruleError(std::string message) {
    return Error{ErrorCode::rule_conflict, std::move(message), false, "修改行动或由作者介入"};
}

} // namespace

const CharacterState* findCharacter(const ScenarioState& state, const std::string& id) {
    const auto it = std::find_if(state.characters.begin(), state.characters.end(),
                                 [&id](const CharacterState& item) { return item.id == id; });
    return it == state.characters.end() ? nullptr : &*it;
}

Result<ScenarioState> applyOperation(const ScenarioState& input, const ProposedOperation& operation) {
    // 修订是有符号整数；在复制/修改前阻止上溢，不制造无法持久化的下一状态。
    if (input.revision == std::numeric_limits<int>::max())
        return Result<ScenarioState>::failure(ruleError("状态修订已达到可表示上限"));
    ScenarioState output = input;
    auto* actor = mutableCharacter(output, operation.actor_id);
    auto* target = mutableCharacter(output, operation.target_id);
    if (actor == nullptr) {
        return Result<ScenarioState>::failure(ruleError("行动者不在当前分支的有效范围内"));
    }

    switch (operation.type) {
    case OperationType::inspect_seal:
        if (output.seal_holder_id != operation.actor_id) {
            return Result<ScenarioState>::failure(ruleError("只有持有人或经持有人同意者才能检查唯一印章"));
        }
        output.seal_inspected = true;
        actor->knows_seal_forgery = true;
        break;
    case OperationType::reveal_gate_secret:
        if (!actor->knows_gate_closure) {
            return Result<ScenarioState>::failure(ruleError("人物不能传播自己尚不知道的封门计划"));
        }
        if (target == nullptr) {
            return Result<ScenarioState>::failure(ruleError("秘密接收者不存在"));
        }
        target->knows_gate_closure = true;
        break;
    case OperationType::reveal_seal_forgery:
        if (!actor->knows_seal_forgery) {
            return Result<ScenarioState>::failure(ruleError("人物不能传播自己尚未发现的印章伪造迹象"));
        }
        if (target == nullptr) {
            return Result<ScenarioState>::failure(ruleError("秘密接收者不存在"));
        }
        if (target->trust == std::numeric_limits<int>::max())
            return Result<ScenarioState>::failure(ruleError("人物信任计数已达到可表示上限"));
        target->knows_seal_forgery = true;
        ++target->trust;
        break;
    case OperationType::transfer_seal:
        if (output.seal_holder_id != operation.actor_id || !operation.holder_consented) {
            return Result<ScenarioState>::failure(ruleError("唯一印章转移必须由当前持有人明确同意"));
        }
        if (target == nullptr) {
            return Result<ScenarioState>::failure(ruleError("印章接收者不存在"));
        }
        output.seal_holder_id = target->id;
        break;
    default:
        return Result<ScenarioState>::failure(ruleError("操作类型不受当前规则支持"));
    }
    ++output.revision;
    return Result<ScenarioState>::success(std::move(output));
}

std::string canonicalState(const ScenarioState& state) {
    std::ostringstream out;
    out << state.revision << '|' << state.turn << '|' << state.elapsed_ticks << '|'
        << state.seal_holder_id << '|' << state.seal_inspected << '|' << state.paused << '|'
        << state.completed << '|' << state.narration;
    auto characters = state.characters;
    std::sort(characters.begin(), characters.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    for (const auto& character : characters) {
        out << '|' << character.id << ':' << character.name << ':' << character.knows_gate_closure
            << ':' << character.knows_seal_forgery << ':' << character.trust;
    }
    return out.str();
}

std::string stateHash(const ScenarioState& state) {
    return sha256(canonicalState(state));
}

} // namespace xuyan::domain
