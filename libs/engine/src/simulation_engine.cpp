#include "xuyan/engine/simulation_engine.h"

#include <sstream>

namespace xuyan::engine {

xuyan::domain::Result<xuyan::domain::ActorContext> buildActorContext(
    const xuyan::domain::ScenarioState& state, const std::string& input_commit_id, const std::string& actor_id) {
    const auto* actor = xuyan::domain::findCharacter(state, actor_id);
    if (actor == nullptr) return xuyan::domain::Result<xuyan::domain::ActorContext>::failure(
        {xuyan::domain::ErrorCode::missing_context, "人物不在当前分支状态中", false, "刷新人物绑定"});
    xuyan::domain::ActorContext context; context.actor_id = actor_id; context.input_commit_id = input_commit_id;
    context.visible_facts = {"灰港正值暴雨", "翌日将举行议和谈判", "议和印章当前持有人：" + state.seal_holder_id};
    if (actor->knows_gate_closure) context.visible_facts.push_back("北门今夜封闭");
    if (actor->knows_seal_forgery) context.visible_facts.push_back("议和印章存在伪造迹象");
    std::ostringstream serialized; serialized << "actor=" << actor_id << "\ninput_commit=" << input_commit_id;
    for (const auto& fact : context.visible_facts) serialized << "\nfact=" << fact;
    context.serialized = serialized.str();
    return xuyan::domain::Result<xuyan::domain::ActorContext>::success(std::move(context));
}

xuyan::domain::Result<xuyan::domain::ActorIntent> SessionMockProvider::propose(
    const xuyan::domain::ActorContext& context, const xuyan::domain::ScenarioState& state) const {
    xuyan::domain::ActorIntent intent; intent.actor_id = context.actor_id; intent.input_commit_id = context.input_commit_id;
    if (state.turn == 0) {
        intent.speech = "暴雨封住渡口，明日谈判前先共同核对议和凭证。";
        intent.public_reason = "只使用公开天气与谈判信息。";
    } else if (state.turn == 1) {
        intent.operation = "inspect_seal"; intent.speech = "我先在灯下检查印章。";
        intent.public_reason = "当前持有人可以检查唯一印章。";
    } else if (state.turn == 2) {
        intent.operation = "reveal_seal_forgery"; intent.target_id = "actor-xucheng";
        intent.speech = "许代表，印章可能被人动过。此事先只在你我之间核验。";
        intent.public_reason = "只向指定接收者传播已知观察。"; intent.ends_scene = true;
    } else {
        intent.speech = "我暂时没有新的可验证行动。"; intent.public_reason = "无状态变化。";
    }
    return xuyan::domain::validateActorIntent(std::move(intent));
}

} // namespace xuyan::engine
