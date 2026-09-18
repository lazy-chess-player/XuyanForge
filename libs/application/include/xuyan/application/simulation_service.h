#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

class SimulationService {
public:
    explicit SimulationService(const std::filesystem::path& database_path);

    xuyan::domain::Result<xuyan::domain::CommitView> open();
    xuyan::domain::Result<xuyan::domain::CommitView> step(const std::string& command_id);
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(const std::string& command_id, bool paused);
    xuyan::domain::Result<xuyan::domain::CommitView> forkCurrent(const std::string& command_id,
                                                                 const std::string& name);
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> branches();
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    xuyan::domain::Result<xuyan::domain::SimulationSession> createSession(
        const std::string& command_id, const std::string& branch_id, int max_turns,
        bool continuous, int max_calls, std::vector<xuyan::domain::ActorModelBinding> actors = {});
    xuyan::domain::Result<xuyan::domain::SimulationSession> session(const std::string& session_id);
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> sessions();
    xuyan::domain::Result<xuyan::domain::SimulationSession> stepSessionMock(
        const std::string& command_id, const std::string& session_id);
    xuyan::domain::Result<xuyan::domain::SimulationSession> runSessionMock(
        const std::string& command_id, const std::string& session_id);
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    xuyan::domain::Result<xuyan::domain::SimulationSession> directorIntervene(
        const std::string& command_id, const std::string& session_id, const std::string& actor_id,
        const std::string& speech, const std::string& operation = "speak",
        const std::string& target_id = {}, bool holder_consented = false);
    xuyan::domain::Result<int> recoverInterruptedSessions();

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
