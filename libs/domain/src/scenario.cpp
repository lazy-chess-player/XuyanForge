#include "xuyan/domain/scenario.h"
#include "xuyan/domain/hash.h"

#include <algorithm>
#include <sstream>

namespace xuyan::domain {
namespace {

CharacterState* mutableCharacter(ScenarioState& state, const std::string& id) {
    const auto it = std::find_if(state.characters.begin(), state.characters.end(),
                                 [&id](const CharacterState& item) { return item.id == id; });
    return it == state.characters.end() ? nullptr : &*it;
}

Error ruleError(std::string message) {
    return Error{ErrorCode::rule_conflict, std::move(message), false, "修改行动或由作者介入"};
}

} // namespace

ScenarioState makeGreyHarborInitialState() {
    ScenarioState state;
    state.narration = "谈判前一天傍晚，暴雨笼罩灰港。议和印章由沈棠保管。";
    state.characters = {
        {"actor-xucheng", "许澄", true, false, 0},
        {"actor-shentang", "沈棠", false, false, 0},
    };
    return state;
}

const CharacterState* findCharacter(const ScenarioState& state, const std::string& id) {
    const auto it = std::find_if(state.characters.begin(), state.characters.end(),
                                 [&id](const CharacterState& item) { return item.id == id; });
    return it == state.characters.end() ? nullptr : &*it;
}

Result<ScenarioState> applyOperation(const ScenarioState& input, const ProposedOperation& operation) {
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
