#include "xuyan/engine/mock_provider.h"

#include <thread>

namespace xuyan::engine {

MockProvider::MockProvider(std::chrono::milliseconds latency) : latency_(latency) {}

xuyan::domain::Result<MockStepResult> MockProvider::next(const xuyan::domain::ScenarioState& input) const {
    using namespace xuyan::domain;
    if (input.paused) {
        return Result<MockStepResult>::failure(
            Error{ErrorCode::validation_failed, "会话已暂停", false, "先恢复会话再单步运行"});
    }
    if (input.completed || input.turn >= 3) {
        return Result<MockStepResult>::failure(
            Error{ErrorCode::validation_failed, "灰港样例的三个回合已完成", false, "从检查点创建新分支"});
    }

    std::this_thread::sleep_for(latency_);
    ScenarioState output = input;
    MockStepResult result;

    if (input.turn == 0) {
        result.actor_id = "actor-xucheng";
        result.speech = "暴雨封住渡口，明日谈判前先共同核对议和凭证。";
        result.public_explanation = "只使用公开天气与谈判信息；未向沈棠泄露北门封闭计划。";
        output.narration = "许澄提议共同核验凭证。沈棠同意亲自检查手中的议和印章。";
    } else if (input.turn == 1) {
        const ProposedOperation inspect{OperationType::inspect_seal, "actor-shentang", "", true};
        auto changed = applyOperation(output, inspect);
        if (!changed.ok()) {
            return Result<MockStepResult>::failure(*changed.error);
        }
        output = *changed.value;
        result.actor_id = "actor-shentang";
        result.speech = "印泥边缘有二次压印，但我暂不公开结论。";
        result.public_explanation = "检查发生后，伪造迹象只进入沈棠的私密知识。";
        output.narration = "沈棠在灯下检查印章，发现伪造迹象；这项观察尚未公开。";
    } else {
        CharacterState* xu = nullptr;
        for (auto& character : output.characters) {
            if (character.id == "actor-xucheng") xu = &character;
        }
        const auto* shen = findCharacter(output, "actor-shentang");
        if (xu == nullptr || shen == nullptr || !shen->knows_seal_forgery) {
            return Result<MockStepResult>::failure(
                Error{ErrorCode::missing_context, "沈棠尚未获得可传播的印章观察", false, "回到上一检查点"});
        }
        xu->knows_seal_forgery = true;
        xu->trust += 1;
        result.actor_id = "actor-shentang";
        result.speech = "许代表，印章可能被人动过。此事先只在你我之间核验。";
        result.public_explanation = "经合法私下沟通，许澄获得印章信息；北门秘密仍未传播。";
        output.narration = "沈棠私下告知许澄伪造迹象。许澄对她的信任提高。";
        output.completed = true;
    }

    ++output.turn;
    ++output.elapsed_ticks;
    ++output.revision;
    result.state = std::move(output);
    return Result<MockStepResult>::success(std::move(result));
}

} // namespace xuyan::engine
