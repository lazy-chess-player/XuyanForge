#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/character_blueprint.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/domain/retrieval.h"
#include "xuyan/domain/world_version.h"
#include "xuyan/domain/world_graph.h"
#include "xuyan/domain/character_instance.h"
#include "xuyan/domain/simulation_session.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/domain/world_entity.h"
#include "xuyan/domain/evidence.h"
#include "xuyan/domain/extraction_job.h"
#include "xuyan/domain/extraction_candidate.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace xuyan::storage {

class WorkspaceRepository {
public:
    explicit WorkspaceRepository(const std::filesystem::path& database_path);
    ~WorkspaceRepository();

    WorkspaceRepository(const WorkspaceRepository&) = delete;
    WorkspaceRepository& operator=(const WorkspaceRepository&) = delete;

    xuyan::domain::Result<xuyan::domain::CommitView> ensureDemo();
    xuyan::domain::Result<xuyan::domain::CommitView> loadHead(const std::string& branch_id);
    xuyan::domain::Result<xuyan::domain::CommitView> loadCommit(const std::string& commit_id);
    xuyan::domain::Result<std::string> activeBranchId();
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> listBranches();
    xuyan::domain::Result<std::optional<xuyan::domain::CommitView>> replayCommand(
        const std::string& command_id);

    xuyan::domain::Result<xuyan::domain::CommitView> commitStep(
        const std::string& command_id,
        const std::string& payload_hash,
        const xuyan::domain::CommitView& expected,
        const xuyan::domain::ScenarioState& next_state);

    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(
        const std::string& command_id,
        const xuyan::domain::CommitView& expected,
        bool paused);

    xuyan::domain::Result<xuyan::domain::CommitView> forkBranch(
        const std::string& command_id,
        const std::string& source_commit_id,
        const std::string& branch_name);

    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    xuyan::domain::Result<xuyan::domain::WorldEntity> createEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    xuyan::domain::Result<xuyan::domain::WorldEntity> saveEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    xuyan::domain::Result<xuyan::domain::WorldEntity> loadEntity(const std::string& entity_id);
    xuyan::domain::Result<xuyan::domain::EntityPage> searchEntities(
        const std::string& query, const std::string& kind, int offset, int limit);
    xuyan::domain::Result<xuyan::domain::WorldEntity> deleteEntity(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> mergeEntities(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitEntityMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision, int target_expected_revision);

    xuyan::domain::Result<xuyan::domain::SourceDocument> saveSource(
        const std::string& command_id, const xuyan::domain::SourceDocument& document);
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listSources();
    xuyan::domain::Result<xuyan::domain::SourceDocument> loadSource(const std::string& source_id);
    xuyan::domain::Result<xuyan::domain::SourceDocument> replaceSourceChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> createBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> saveBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> loadBlueprint(
        const std::string& blueprint_id, int version = -1);
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> listBlueprints();

    xuyan::domain::Result<int> importEntities(const std::string& command_id,
                                              const std::string& package_hash,
                                              std::vector<xuyan::domain::WorldEntity> entities);
    xuyan::domain::Result<int> importBlueprintVersions(
        const std::string& command_id, const std::string& package_hash,
        std::vector<xuyan::domain::CharacterBlueprint> versions);

    xuyan::domain::Result<xuyan::domain::ProviderConnection> saveProviderConnection(
        const std::string& command_id, xuyan::domain::ProviderConnection connection, int expected_revision);
    xuyan::domain::Result<xuyan::domain::ProviderConnection> loadProviderConnection(const std::string& connection_id);
    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> listProviderConnections();
    xuyan::domain::Result<xuyan::domain::ProviderConnection> deleteProviderConnection(
        const std::string& command_id, const std::string& connection_id, int expected_revision);

    xuyan::domain::Result<xuyan::domain::EvidenceReference> createEvidence(
        const std::string& command_id, xuyan::domain::EvidenceReference evidence);
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listEvidenceForSource(
        const std::string& source_id);

    xuyan::domain::Result<xuyan::domain::ExtractionJob> createExtractionJob(
        const std::string& command_id, xuyan::domain::ExtractionJob job);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> loadExtractionJob(const std::string& job_id);
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listExtractionJobs();
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancelExtractionJob(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimExtractionStep(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    xuyan::domain::Result<int> recoverInterruptedExtractionSteps();
    xuyan::domain::Result<xuyan::domain::ExtractionJob> commitExtractionCandidates(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidates(
        const std::string& review_status);
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidatesForJob(
        const std::string& job_id, int limit);
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> loadExtractionCandidate(
        const std::string& candidate_id);
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> reviewExtractionCandidate(
        const std::string& command_id, xuyan::domain::ExtractionCandidate candidate, int expected_revision,
        std::optional<xuyan::domain::WorldEntity> accepted_entity);
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveEntityRetrievalScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieveEntities(
        xuyan::domain::RetrievalRequest request);
    xuyan::domain::Result<xuyan::domain::WorldVersion> publishWorldVersion(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id);
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> listWorldVersions(const std::string& world_id);
    xuyan::domain::Result<xuyan::domain::WorldVersion> loadWorldVersion(const std::string& version_id);
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> createHistoricalSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimelineEvents(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time);
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveDirectedRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listDirectedRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocationPlacement(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveTravelRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    xuyan::domain::Result<xuyan::domain::MapView> loadMapView(const std::string& world_id);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> createCharacterInstance(
        const std::string& command_id, xuyan::domain::CharacterInstance instance);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> loadCharacterInstance(const std::string& instance_id);
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> listCharacterInstances(
        const std::string& world_version_id);
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveCharacterInstanceMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, xuyan::domain::BranchRootBinding binding);
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> loadBranchRootBinding(const std::string& branch_id);
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSimulationSession(
        const std::string& command_id, xuyan::domain::SimulationSession session);
    xuyan::domain::Result<xuyan::domain::SimulationSession> loadSimulationSession(const std::string& session_id);
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> listSimulationSessions();
    xuyan::domain::Result<xuyan::domain::SimulationTurn> reserveSimulationTurn(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& actor_id, const std::string& request_hash);
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitSimulationTurn(
        const std::string& command_id, const std::string& turn_id, int expected_session_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state,
        const std::string& draft_narration, int input_tokens, int output_tokens);
    xuyan::domain::Result<xuyan::domain::SimulationTurn> finishSimulationNarration(
        const std::string& command_id, const std::string& turn_id, const std::string& final_narration);
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSimulationSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitDirectorIntervention(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state);
    xuyan::domain::Result<int> recoverInterruptedSimulationSessions();

private:
    sqlite3* database_{nullptr};

    void migrate();
};

} // namespace xuyan::storage
