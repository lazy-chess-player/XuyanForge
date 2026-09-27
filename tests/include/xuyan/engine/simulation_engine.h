#pragma once

#include "xuyan/domain/simulation_session.h"

namespace xuyan::engine {

/** @brief 从当前状态生成指定人物可见的事实与序列化上下文。 */
xuyan::domain::Result<xuyan::domain::ActorContext> buildActorContext(
    const xuyan::domain::ScenarioState& state, const std::string& input_commit_id,
    const std::string& actor_id);

/** @brief 为合成情景回归提供确定性的会话意图。 */
class SessionMockProvider {
public:
    /** @brief 为离线会话提出确定性人物意图，不调用外部模型。 */
    xuyan::domain::Result<xuyan::domain::ActorIntent> propose(
        const xuyan::domain::ActorContext& context, const xuyan::domain::ScenarioState& state) const;
};

} // namespace xuyan::engine
