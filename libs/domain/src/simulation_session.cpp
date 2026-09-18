#include "xuyan/domain/simulation_session.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace xuyan::domain {

Result<ActorIntent> validateActorIntent(ActorIntent intent) {
    constexpr std::array operations{std::string_view{"speak"}, std::string_view{"inspect_seal"},
        std::string_view{"reveal_gate_secret"}, std::string_view{"reveal_seal_forgery"}, std::string_view{"transfer_seal"}};
    if (intent.actor_id.empty() || intent.input_commit_id.empty() || intent.speech.size() > 64 * 1024
        || intent.public_reason.size() > 16 * 1024
        || std::find(operations.begin(), operations.end(), intent.operation) == operations.end())
        return Result<ActorIntent>::failure(
            {ErrorCode::validation_failed, "人物意图缺少行动者/输入版本或操作不在白名单", false, "修正意图协议"});
    if ((intent.operation == "reveal_gate_secret" || intent.operation == "reveal_seal_forgery"
         || intent.operation == "transfer_seal") && intent.target_id.empty())
        return Result<ActorIntent>::failure(
            {ErrorCode::validation_failed, "该人物操作缺少目标", false, "指定当前分支中的目标人物"});
    return Result<ActorIntent>::success(std::move(intent));
}

Result<SimulationSession> validateSimulationSession(SimulationSession session) {
    if (session.id.empty() || session.branch_id.empty() || session.max_turns < 1 || session.max_turns > 10000
        || session.no_progress_limit < 1 || session.no_progress_limit > 100
        || session.max_calls < 1 || session.max_calls > 1000000 || session.actors.size() < 2 || session.actors.size() > 16)
        return Result<SimulationSession>::failure(
            {ErrorCode::validation_failed, "推演会话的分支、回合、预算或人物数量无效", false, "至少选择两名人物并设置硬上限"});
    std::unordered_set<std::string> actors;
    for (const auto& binding : session.actors)
        if (binding.actor_id.empty() || binding.provider_connection_id.empty() || binding.model_id.empty()
            || !actors.insert(binding.actor_id).second)
            return Result<SimulationSession>::failure(
                {ErrorCode::validation_failed, "人物模型绑定缺失或重复", false, "为每名人物选择一个授权模型"});
    return Result<SimulationSession>::success(std::move(session));
}

Result<ProposedOperation> toProposedOperation(const ActorIntent& intent) {
    auto valid = validateActorIntent(intent); if (!valid.ok()) return Result<ProposedOperation>::failure(*valid.error);
    ProposedOperation operation; operation.actor_id = intent.actor_id; operation.target_id = intent.target_id;
    operation.holder_consented = intent.holder_consented;
    if (intent.operation == "inspect_seal") operation.type = OperationType::inspect_seal;
    else if (intent.operation == "reveal_gate_secret") operation.type = OperationType::reveal_gate_secret;
    else if (intent.operation == "reveal_seal_forgery") operation.type = OperationType::reveal_seal_forgery;
    else if (intent.operation == "transfer_seal") operation.type = OperationType::transfer_seal;
    else return Result<ProposedOperation>::failure(
        {ErrorCode::validation_failed, "纯发言不产生领域状态操作", false, "只提交对白或选择领域操作"});
    return Result<ProposedOperation>::success(std::move(operation));
}

} // namespace xuyan::domain
