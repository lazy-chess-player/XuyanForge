#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

struct ActorModelBinding {
    std::string actor_id;
    std::string provider_connection_id;
    std::string model_id;
};

struct ActorIntent {
    std::string actor_id;
    std::string input_commit_id;
    std::string speech;
    std::string operation{"speak"};
    std::string target_id;
    bool holder_consented{false};
    bool ends_scene{false};
    std::string public_reason;
};

struct ActorContext {
    std::string actor_id;
    std::string input_commit_id;
    std::vector<std::string> visible_facts;
    std::string serialized;
};

struct ProviderCallRecord {
    std::string id;
    std::string turn_id;
    std::string provider_connection_id;
    std::string model_id;
    std::string request_hash;
    std::string status;
    int input_tokens{0};
    int output_tokens{0};
    std::string failure_kind;
};

struct SimulationTurn {
    std::string id;
    std::string session_id;
    int ordinal{0};
    std::string input_commit_id;
    std::string committed_commit_id;
    std::string actor_id;
    std::string status{"requesting"};
    ActorIntent intent;
    std::string draft_narration;
    std::string final_narration;
    std::string error_message;
    ProviderCallRecord call;
    int revision{0};
};

struct SimulationSession {
    std::string id;
    std::string branch_id;
    std::string status{"ready"};
    int max_turns{20};
    bool continuous{false};
    int no_progress_limit{3};
    int max_calls{120};
    int used_calls{0};
    int reserved_calls{0};
    int unknown_calls{0};
    bool pause_requested{false};
    bool cancel_requested{false};
    int revision{0};
    std::vector<ActorModelBinding> actors;
    std::vector<SimulationTurn> turns;
};

/** @brief 校验模型提出的人物意图和所引用的输入提交。 */
Result<ActorIntent> validateActorIntent(ActorIntent intent);
/** @brief 校验会话状态、额度和回合记录的一致性。 */
Result<SimulationSession> validateSimulationSession(SimulationSession session);
/** @brief 将受支持的人物意图转为确定性的情景操作。 */
Result<ProposedOperation> toProposedOperation(const ActorIntent& intent);

} // namespace xuyan::domain
