#include "xuyan/application/simulation_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/engine/mock_provider.h"
#include "xuyan/engine/simulation_engine.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>

namespace xuyan::application {
namespace {

xuyan::domain::Result<xuyan::domain::ScenarioState> advanceState(
    const xuyan::domain::ScenarioState& current, const xuyan::domain::ActorIntent& intent) {
    xuyan::domain::ScenarioState next;
    if (intent.operation == "speak") { next = current; ++next.revision; }
    else {
        auto operation = xuyan::domain::toProposedOperation(intent);
        if (!operation.ok()) return xuyan::domain::Result<xuyan::domain::ScenarioState>::failure(*operation.error);
        auto applied = xuyan::domain::applyOperation(current, *operation.value);
        if (!applied.ok()) return applied;
        next = std::move(*applied.value);
    }
    ++next.turn; ++next.elapsed_ticks; next.completed = intent.ends_scene;
    return xuyan::domain::Result<xuyan::domain::ScenarioState>::success(std::move(next));
}

} // namespace

SimulationService::SimulationService(const std::filesystem::path& database_path) : database_path_(database_path) {}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::open() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.ensureDemo();
}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::step(const std::string& command_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto replay = repository.replayCommand(command_id);
    if (!replay.ok()) {
        return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*replay.error);
    }
    if (replay.value->has_value()) {
        return xuyan::domain::Result<xuyan::domain::CommitView>::success(std::move(**replay.value));
    }
    auto active = repository.activeBranchId();
    if (!active.ok()) return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*active.error);
    auto head = repository.loadHead(*active.value);
    if (!head.ok()) return head;

    xuyan::engine::MockProvider provider;
    auto proposed = provider.next(head.value->state);
    if (!proposed.ok()) {
        return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*proposed.error);
    }
    const auto payload = "simulation.step:" + head.value->branch_id + ':' + head.value->commit_id;
    return repository.commitStep(command_id, payload, *head.value, proposed.value->state);
}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::setPaused(const std::string& command_id,
                                                                               bool paused) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto active = repository.activeBranchId();
    if (!active.ok()) return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*active.error);
    auto head = repository.loadHead(*active.value);
    if (!head.ok()) return head;
    return repository.setPaused(command_id, *head.value, paused);
}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::forkCurrent(const std::string& command_id,
                                                                                 const std::string& name) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto active = repository.activeBranchId();
    if (!active.ok()) return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*active.error);
    auto head = repository.loadHead(*active.value);
    if (!head.ok()) return head;
    return repository.forkBranch(command_id, head.value->commit_id, name);
}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::switchBranch(const std::string& branch_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.switchBranch(branch_id);
}

xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> SimulationService::branches() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listBranches();
}

xuyan::domain::Result<std::string> SimulationService::backupTo(const std::filesystem::path& destination) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.backupTo(destination);
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::createSession(
    const std::string& command_id, const std::string& branch_id, int max_turns,
    bool continuous, int max_calls, std::vector<xuyan::domain::ActorModelBinding> actors) {
    if (actors.empty()) {
        actors = {{"actor-xucheng", "mock", "grey-harbor-fixed-v1"},
                  {"actor-shentang", "mock", "grey-harbor-fixed-v1"}};
    }
    xuyan::domain::SimulationSession value;
    value.id = "session-" + xuyan::domain::sha256(command_id + '|' + branch_id).substr(0, 20);
    value.branch_id = branch_id;
    value.max_turns = max_turns;
    value.continuous = continuous;
    value.max_calls = max_calls;
    value.actors = std::move(actors);
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.createSimulationSession(command_id, std::move(value));
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::session(const std::string& session_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.loadSimulationSession(session_id);
}

xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> SimulationService::sessions() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listSimulationSessions();
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::stepSessionMock(
    const std::string& command_id, const std::string& session_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
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
            if (!narration.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*narration.error);
            return repository.loadSimulationSession(session_id);
        }
        if (replay_turn->status == "completed") return loaded;
        return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(
            {xuyan::domain::ErrorCode::rule_conflict, "该命令已有未决或结果未知的模型调用", false, "先恢复并人工核对该会话"});
    }
    if (current_session.status != "ready" && current_session.status != "running")
        return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(
            {xuyan::domain::ErrorCode::rule_conflict, "当前会话不能继续执行", false, "恢复会话或新建推演"});
    auto head = repository.loadHead(current_session.branch_id);
    if (!head.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*head.error);
    const auto pending = std::find_if(current_session.turns.begin(), current_session.turns.end(), [](const auto& turn) {
        return turn.status == "needs_review";
    });
    if (pending != current_session.turns.end()) {
        auto next = advanceState(head.value->state, pending->intent);
        if (!next.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*next.error);
        auto committed = repository.commitSimulationTurn(
            command_id + ":resume-commit", pending->id, current_session.revision, pending->intent,
            std::move(*next.value), pending->draft_narration, pending->call.input_tokens, pending->call.output_tokens);
        if (!committed.ok()) return committed;
        auto narration = repository.finishSimulationNarration(
            command_id + ":resume-narrate", pending->id, pending->draft_narration);
        if (!narration.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*narration.error);
        return repository.loadSimulationSession(session_id);
    }
    const auto actor_id = head.value->state.turn == 0 ? std::string{"actor-xucheng"} : std::string{"actor-shentang"};
    auto context = xuyan::engine::buildActorContext(head.value->state, head.value->commit_id, actor_id);
    if (!context.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*context.error);
    const auto request_hash = command_id + ':' + xuyan::domain::sha256(context.value->serialized);
    auto reserved = repository.reserveSimulationTurn(
        command_id + ":reserve", session_id, current_session.revision, actor_id, request_hash);
    if (!reserved.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*reserved.error);
    xuyan::engine::SessionMockProvider provider;
    auto proposed = provider.propose(*context.value, head.value->state);
    if (!proposed.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*proposed.error);
    auto next = advanceState(head.value->state, *proposed.value);
    if (!next.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*next.error);
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
        return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(
            {xuyan::domain::ErrorCode::storage_error, "事实提交后未找到对应推演回合", false, "重新打开工作区"});
    if (committed_turn->status == "needs_review") return committed;
    auto narration = repository.finishSimulationNarration(
        command_id + ":narrate", committed_turn->id, committed_turn->draft_narration);
    if (!narration.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*narration.error);
    return repository.loadSimulationSession(session_id);
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::runSessionMock(
    const std::string& command_id, const std::string& session_id) {
    auto current = session(session_id);
    if (!current.ok()) return current;
    int iteration = 0;
    while (current.value->continuous && (current.value->status == "ready" || current.value->status == "running")) {
        current = stepSessionMock(command_id + ':' + std::to_string(iteration++), session_id);
        if (!current.ok()) return current;
    }
    return current;
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::controlSession(
    const std::string& command_id, const std::string& session_id, int expected_revision,
    const std::string& action) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.controlSimulationSession(command_id, session_id, expected_revision, action);
}

xuyan::domain::Result<xuyan::domain::SimulationSession> SimulationService::directorIntervene(
    const std::string& command_id, const std::string& session_id, const std::string& actor_id,
    const std::string& speech, const std::string& operation, const std::string& target_id,
    bool holder_consented) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto loaded = repository.loadSimulationSession(session_id);
    if (!loaded.ok()) return loaded;
    auto head = repository.loadHead(loaded.value->branch_id);
    if (!head.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*head.error);
    xuyan::domain::ActorIntent intent;
    intent.actor_id = actor_id; intent.input_commit_id = head.value->commit_id; intent.speech = speech;
    intent.operation = operation; intent.target_id = target_id; intent.holder_consented = holder_consented;
    intent.public_reason = "导演显式介入";
    auto valid = xuyan::domain::validateActorIntent(std::move(intent));
    if (!valid.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*valid.error);
    xuyan::domain::ScenarioState next;
    if (valid.value->operation == "speak") { next = head.value->state; ++next.revision; }
    else {
        auto proposed = xuyan::domain::toProposedOperation(*valid.value);
        if (!proposed.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*proposed.error);
        auto applied = xuyan::domain::applyOperation(head.value->state, *proposed.value);
        if (!applied.ok()) return xuyan::domain::Result<xuyan::domain::SimulationSession>::failure(*applied.error);
        next = std::move(*applied.value);
    }
    ++next.turn; ++next.elapsed_ticks;
    return repository.commitDirectorIntervention(command_id, session_id, loaded.value->revision,
                                                  std::move(*valid.value), std::move(next));
}

xuyan::domain::Result<int> SimulationService::recoverInterruptedSessions() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.recoverInterruptedSimulationSessions();
}

} // namespace xuyan::application
