#include "synthetic_fixture.h"

#include "xuyan/domain/hash.h"
#include "xuyan/engine/mock_provider.h"
#include "xuyan/engine/simulation_engine.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <utility>

namespace xuyan::test {
namespace {

/** @brief 应用测试意图并推进回合，保持已提交事实与叙述的分阶段语义。 */
domain::Result<domain::ScenarioState> advanceSyntheticState(
    const domain::ScenarioState& current, const domain::ActorIntent& intent) {
    domain::ScenarioState next;
    if (intent.operation == "speak") { next = current; ++next.revision; }
    else {
        auto operation = domain::toProposedOperation(intent);
        if (!operation.ok()) return domain::Result<domain::ScenarioState>::failure(*operation.error);
        auto applied = domain::applyOperation(current, *operation.value);
        if (!applied.ok()) return applied;
        next = std::move(*applied.value);
    }
    ++next.turn; ++next.elapsed_ticks; next.completed = intent.ends_scene;
    return domain::Result<domain::ScenarioState>::success(std::move(next));
}

} // namespace

domain::ScenarioState makeSyntheticInitialState() {
    domain::ScenarioState state;
    state.seal_holder_id = "actor-shentang";
    state.narration = "谈判前一天傍晚，暴雨笼罩测试场景。议和印章由沈棠保管。";
    state.characters = {
        {"actor-xucheng", "许澄", true, false, 0},
        {"actor-shentang", "沈棠", false, false, 0},
    };
    return state;
}

domain::Result<domain::CommitView> ensureSyntheticBranch(const std::filesystem::path& database_path) {
    storage::WorkspaceRepository repository(database_path);
    auto active = repository.activeBranchId();
    if (active.ok()) return repository.loadHead(*active.value);
    if (active.error->code != domain::ErrorCode::missing_context)
        return domain::Result<domain::CommitView>::failure(*active.error);
    return repository.createRootBranch("branch-main", "测试主线", "commit-root", makeSyntheticInitialState());
}

std::vector<domain::ActorModelBinding> syntheticActors() {
    return {{"actor-xucheng", "mock", "synthetic-test-fixed-v1"},
            {"actor-shentang", "mock", "synthetic-test-fixed-v1"}};
}

domain::Result<domain::CommitView> stepSyntheticBranch(
    const std::filesystem::path& database_path, const std::string& command_id) {
    storage::WorkspaceRepository repository(database_path);
    // 同一命令先返回已提交快照，避免再次调用测试响应器。
    auto replay = repository.replayCommand(command_id);
    if (!replay.ok()) return domain::Result<domain::CommitView>::failure(*replay.error);
    if (replay.value->has_value())
        return domain::Result<domain::CommitView>::success(std::move(**replay.value));
    auto active = repository.activeBranchId();
    if (!active.ok()) return domain::Result<domain::CommitView>::failure(*active.error);
    auto head = repository.loadHead(*active.value);
    if (!head.ok()) return head;
    engine::MockProvider provider;
    auto proposed = provider.next(head.value->state);
    if (!proposed.ok()) return domain::Result<domain::CommitView>::failure(*proposed.error);
    const auto payload = "simulation.step:" + head.value->branch_id + ':' + head.value->commit_id;
    return repository.commitStep(command_id, payload, *head.value, proposed.value->state);
}

domain::Result<domain::SimulationSession> stepSyntheticSession(
    const std::filesystem::path& database_path, const std::string& command_id,
    const std::string& session_id) {
    storage::WorkspaceRepository repository(database_path);
    auto loaded = repository.loadSimulationSession(session_id);
    if (!loaded.ok()) return loaded;
    auto& current_session = *loaded.value;
    const auto replay_turn = std::find_if(current_session.turns.begin(), current_session.turns.end(), [&](const auto& turn) {
        return turn.call.request_hash.starts_with(command_id + ':');
    });
    if (replay_turn != current_session.turns.end()) {
        if (replay_turn->status == "narration_pending") {
            auto narration = repository.finishSimulationNarration(
                command_id + ":narrate", replay_turn->id,
                replay_turn->draft_narration.empty() ? replay_turn->intent.speech : replay_turn->draft_narration);
            if (!narration.ok()) return domain::Result<domain::SimulationSession>::failure(*narration.error);
            return repository.loadSimulationSession(session_id);
        }
        if (replay_turn->status == "completed") return loaded;
        return domain::Result<domain::SimulationSession>::failure(
            {domain::ErrorCode::rule_conflict, "该命令已有未决或结果未知的模型调用", false, "先恢复并人工核对该会话"});
    }
    if (current_session.status != "ready" && current_session.status != "running")
        return domain::Result<domain::SimulationSession>::failure(
            {domain::ErrorCode::rule_conflict, "当前会话不能继续执行", false, "恢复会话或新建推演"});
    auto head = repository.loadHead(current_session.branch_id);
    if (!head.ok()) return domain::Result<domain::SimulationSession>::failure(*head.error);
    const auto pending = std::find_if(current_session.turns.begin(), current_session.turns.end(), [](const auto& turn) {
        return turn.status == "needs_review";
    });
    if (pending != current_session.turns.end()) {
        auto next = advanceSyntheticState(head.value->state, pending->intent);
        if (!next.ok()) return domain::Result<domain::SimulationSession>::failure(*next.error);
        auto committed = repository.commitSimulationTurn(
            command_id + ":resume-commit", pending->id, current_session.revision, pending->intent,
            std::move(*next.value), pending->draft_narration, pending->call.input_tokens, pending->call.output_tokens);
        if (!committed.ok()) return committed;
        auto narration = repository.finishSimulationNarration(
            command_id + ":resume-narrate", pending->id, pending->draft_narration);
        if (!narration.ok()) return domain::Result<domain::SimulationSession>::failure(*narration.error);
        return repository.loadSimulationSession(session_id);
    }
    const auto actor_id = head.value->state.turn == 0 ? std::string{"actor-xucheng"} : std::string{"actor-shentang"};
    auto context = engine::buildActorContext(head.value->state, head.value->commit_id, actor_id);
    if (!context.ok()) return domain::Result<domain::SimulationSession>::failure(*context.error);
    const auto request_hash = command_id + ':' + domain::sha256(context.value->serialized);
    auto reserved = repository.reserveSimulationTurn(
        command_id + ":reserve", session_id, current_session.revision, actor_id, request_hash);
    if (!reserved.ok()) return domain::Result<domain::SimulationSession>::failure(*reserved.error);
    engine::SessionMockProvider provider;
    auto proposed = provider.propose(*context.value, head.value->state);
    if (!proposed.ok()) return domain::Result<domain::SimulationSession>::failure(*proposed.error);
    auto next = advanceSyntheticState(head.value->state, *proposed.value);
    if (!next.ok()) return domain::Result<domain::SimulationSession>::failure(*next.error);
    const auto draft = proposed.value->speech;
    const auto input_tokens = static_cast<int>((context.value->serialized.size() + 3) / 4);
    const auto output_tokens = static_cast<int>((proposed.value->speech.size() + proposed.value->public_reason.size() + 3) / 4);
    auto committed = repository.commitSimulationTurn(
        command_id + ":commit", reserved.value->id, current_session.revision + 1,
        *proposed.value, std::move(*next.value), draft, input_tokens, output_tokens);
    if (!committed.ok()) return committed;
    const auto committed_turn = std::find_if(committed.value->turns.begin(), committed.value->turns.end(), [&](const auto& turn) {
        return turn.id == reserved.value->id;
    });
    if (committed_turn == committed.value->turns.end())
        return domain::Result<domain::SimulationSession>::failure(
            {domain::ErrorCode::storage_error, "事实提交后未找到对应推演回合", false, "重新打开工作区"});
    if (committed_turn->status == "needs_review") return committed;
    auto narration = repository.finishSimulationNarration(
        command_id + ":narrate", committed_turn->id, committed_turn->draft_narration);
    if (!narration.ok()) return domain::Result<domain::SimulationSession>::failure(*narration.error);
    return repository.loadSimulationSession(session_id);
}

} // namespace xuyan::test
