#pragma once

#include "xuyan/domain/simulation_session.h"

namespace xuyan::engine {

xuyan::domain::Result<xuyan::domain::ActorContext> buildActorContext(
    const xuyan::domain::ScenarioState& state, const std::string& input_commit_id,
    const std::string& actor_id);

class SessionMockProvider {
public:
    xuyan::domain::Result<xuyan::domain::ActorIntent> propose(
        const xuyan::domain::ActorContext& context, const xuyan::domain::ScenarioState& state) const;
};

} // namespace xuyan::engine
