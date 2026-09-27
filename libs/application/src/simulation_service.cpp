#include "xuyan/application/simulation_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

SimulationService::SimulationService(const std::filesystem::path& database_path) : database_path_(database_path) {}

xuyan::domain::Result<xuyan::domain::CommitView> SimulationService::open() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto active = repository.activeBranchId();
    if (!active.ok()) return xuyan::domain::Result<xuyan::domain::CommitView>::failure(*active.error);
    return repository.loadHead(*active.value);
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
