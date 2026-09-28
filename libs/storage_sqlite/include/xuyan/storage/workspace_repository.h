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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace xuyan::storage {

struct WorldTemplate {
    std::string id;
    std::string name;
    std::string source_id;
};

class WorkspaceRepository {
public:
    /** @brief 打开工作区数据库并执行必要的结构迁移。 */
    explicit WorkspaceRepository(const std::filesystem::path& database_path);
    /** @brief 关闭当前工作区数据库连接。 */
    ~WorkspaceRepository();

    WorkspaceRepository(const WorkspaceRepository&) = delete;
    WorkspaceRepository& operator=(const WorkspaceRepository&) = delete;

    /** @brief 用调用方提供的状态显式创建初始分支，已有活动分支时拒绝覆盖。 */
    xuyan::domain::Result<xuyan::domain::CommitView> createRootBranch(
        const std::string& branch_id, const std::string& branch_name,
        const std::string& commit_id, const xuyan::domain::ScenarioState& initial_state);
    /** @brief 读取指定分支的最新提交及其状态快照。 */
    xuyan::domain::Result<xuyan::domain::CommitView> loadHead(const std::string& branch_id);
    /** @brief 按提交标识读取不可变状态快照。 */
    xuyan::domain::Result<xuyan::domain::CommitView> loadCommit(const std::string& commit_id);
    /** @brief 返回当前选中分支的标识。 */
    xuyan::domain::Result<std::string> activeBranchId();
    /** @brief 列出模拟分支及其父分支和最新提交。 */
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> listBranches();
    /** @brief 查找既有命令的提交结果，用于幂等重放。 */
    xuyan::domain::Result<std::optional<xuyan::domain::CommitView>> replayCommand(
        const std::string& command_id);

    /** @brief 在预期提交仍为分支头时原子写入下一状态。 */
    xuyan::domain::Result<xuyan::domain::CommitView> commitStep(
        const std::string& command_id,
        const std::string& payload_hash,
        const xuyan::domain::CommitView& expected,
        const xuyan::domain::ScenarioState& next_state);

    /** @brief 以新提交记录分支的暂停状态。 */
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(
        const std::string& command_id,
        const xuyan::domain::CommitView& expected,
        bool paused);

    /** @brief 从指定提交创建新分支并复制其状态快照。 */
    xuyan::domain::Result<xuyan::domain::CommitView> forkBranch(
        const std::string& command_id,
        const std::string& source_commit_id,
        const std::string& branch_name);

    /** @brief 切换活动分支并返回其最新提交。 */
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    /** @brief 使用 SQLite 在线备份接口保存一致性数据库快照。 */
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    /** @brief 校验并创建世界实体，重复命令保持幂等。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> createEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    /** @brief 按预期修订保存实体，冲突时拒绝覆盖。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> saveEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    /** @brief 读取指定世界实体的当前版本。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> loadEntity(const std::string& entity_id);
    /** @brief 按关键词和类型分页检索世界实体。 */
    xuyan::domain::Result<xuyan::domain::EntityPage> searchEntities(
        const std::string& query, const std::string& kind, int offset, int limit);
    /** @brief 按预期修订软删除世界实体。 */
    xuyan::domain::Result<xuyan::domain::WorldEntity> deleteEntity(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    /** @brief 合并两个实体并保留可逆的来源与目标记录。 */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> mergeEntities(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    /** @brief 按修订校验撤销既有实体合并。 */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitEntityMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision, int target_expected_revision);

    /** @brief 保存小说来源元数据及章节索引，不写入原文字节。 */
    xuyan::domain::Result<xuyan::domain::SourceDocument> saveSource(
        const std::string& command_id, const xuyan::domain::SourceDocument& document);
    /** @brief 创建空白世界模板，不自动附加来源资料。 */
    xuyan::domain::Result<WorldTemplate> createWorldTemplate(const std::string& id, const std::string& name);
    /** @brief 列出工作区已有的世界模板。 */
    xuyan::domain::Result<std::vector<WorldTemplate>> listWorldTemplates();
    /** @brief 将已导入的小说来源关联到指定世界。 */
    xuyan::domain::Result<WorldTemplate> attachWorldSource(const std::string& world_id,
                                                            const std::string& source_id);
    /** @brief 列出当前工作区保存的小说来源元数据。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listSources();
    /** @brief 仅列出指定世界的小说来源及其章节索引。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listSourcesForWorld(
        const std::string& world_id);
    /** @brief 读取指定小说来源与其章节索引。 */
    xuyan::domain::Result<xuyan::domain::SourceDocument> loadSource(const std::string& source_id);
    /** @brief 按章节修订校验替换来源的章节划分。 */
    xuyan::domain::Result<xuyan::domain::SourceDocument> replaceSourceChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

    /** @brief 创建人物卡首版并保存其可编辑字段。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> createBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    /** @brief 基于预期版本写入人物卡的新版本。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> saveBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    /** @brief 读取人物卡指定版本，默认读取最新版。 */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> loadBlueprint(
        const std::string& blueprint_id, int version = -1);
    /** @brief 列出当前工作区的人物卡。 */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> listBlueprints();

    /** @brief 幂等导入已校验的世界实体集合。 */
    xuyan::domain::Result<int> importEntities(const std::string& command_id,
                                              const std::string& package_hash,
                                              std::vector<xuyan::domain::WorldEntity> entities);
    /** @brief 幂等导入人物卡的多个历史版本。 */
    xuyan::domain::Result<int> importBlueprintVersions(
        const std::string& command_id, const std::string& package_hash,
        std::vector<xuyan::domain::CharacterBlueprint> versions);

    /** @brief 按预期修订保存不含明文密钥的模型连接配置。 */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> saveProviderConnection(
        const std::string& command_id, xuyan::domain::ProviderConnection connection, int expected_revision);
    /** @brief 按标识读取模型连接配置。 */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> loadProviderConnection(const std::string& connection_id);
    /** @brief 列出未删除的模型连接配置，结果仍可能包含停用项。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> listProviderConnections();
    /** @brief 按预期修订软删除模型连接配置。 */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> deleteProviderConnection(
        const std::string& command_id, const std::string& connection_id, int expected_revision);

    /** @brief 新增世界资料与原文范围之间的证据引用。 */
    xuyan::domain::Result<xuyan::domain::EvidenceReference> createEvidence(
        const std::string& command_id, xuyan::domain::EvidenceReference evidence);
    /** @brief 列出指定小说来源对应的证据引用。 */
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listEvidenceForSource(
        const std::string& source_id);

    /** @brief 创建持久化解析任务与待执行步骤。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> createExtractionJob(
        const std::string& command_id, xuyan::domain::ExtractionJob job);
    /** @brief 加载任务及其全部解析步骤。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> loadExtractionJob(const std::string& job_id);
    /** @brief 在同一只读快照中读取任务计数和索引停止标志，不读取任何历史步骤正文。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> loadExtractionJobState(const std::string& job_id);
    /** @brief 限量查询最早的待执行步骤，不携带历史输出或错误详情；没有待执行步骤时返回空值。 */
    xuyan::domain::Result<std::optional<xuyan::domain::ExtractionStep>> nextExtractionStep(const std::string& job_id);
    /** @brief 按任务/序号读取单步定位元数据，不查询 output_json 或 error_message。 */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> loadExtractionStepMetadata(const std::string& job_id, int ordinal);
    /** @brief 列出当前工作区的解析任务。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listExtractionJobs();
    /** @brief 仅列出指定世界来源对应的解析任务及步骤。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listExtractionJobsForWorld(
        const std::string& world_id);
    /** @brief 按预期修订请求取消解析任务。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancelExtractionJob(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 原子取消并只返回同事务检查点，供大任务调度避免加载全部步骤。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> cancelExtractionJobState(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 以事务认领下一待执行步骤并递增尝试次数。 */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimExtractionStep(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 按预期尝试次数写入步骤终态与结果。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /** @brief 使用与完整结果相同的幂等事务结算单步，但仅返回任务检查点。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> finishExtractionStepState(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /** @brief 将允许重试的步骤恢复为待执行状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    /** @brief 启动后恢复上次进程中断时遗留的运行中步骤。 */
    xuyan::domain::Result<int> recoverInterruptedExtractionSteps();
    /** @brief 在单一事务中提交步骤输出与经证据校验的候选。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> commitExtractionCandidates(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /** @brief 原子提交同一批候选并返回检查点，不读取前序步骤输出。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> commitExtractionCandidatesState(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /** @brief 按审核状态列出抽取候选。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidates(
        const std::string& review_status);
    /** @brief 在 SQLite 中按世界、可选来源和审核状态过滤并限量读取一页候选。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidatePage> listExtractionCandidatesPage(
        const std::string& world_id, const std::string& source_id, const std::string& review_status,
        int limit, std::int64_t offset);
    /** @brief 限量读取某解析任务产生的候选。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidatesForJob(
        const std::string& job_id, int limit);
    /** @brief 读取单个抽取候选及其原文证据元数据。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> loadExtractionCandidate(
        const std::string& candidate_id);
    /** @brief 按预期修订审核候选，并可同时落库被接受的实体。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> reviewExtractionCandidate(
        const std::string& command_id, xuyan::domain::ExtractionCandidate candidate, int expected_revision,
        std::optional<xuyan::domain::WorldEntity> accepted_entity);
    /** @brief 按修订保存实体的时间与人物可见范围。 */
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveEntityRetrievalScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    /** @brief 在请求者可见范围内检索世界实体。 */
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieveEntities(
        xuyan::domain::RetrievalRequest request);
    /** @brief 将指定世界当前已审核资料发布为不可变版本。 */
    xuyan::domain::Result<xuyan::domain::WorldVersion> publishWorldVersion(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id);
    /** @brief 列出指定世界的已发布版本。 */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> listWorldVersions(const std::string& world_id);
    /** @brief 按标识读取不可变世界版本。 */
    xuyan::domain::Result<xuyan::domain::WorldVersion> loadWorldVersion(const std::string& version_id);
    /** @brief 按故事时间创建世界版本的历史可见快照。 */
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> createHistoricalSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
    /** @brief 按预期修订保存时间线事件。 */
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    /** @brief 按叙事顺序或故事时间列出时间线事件。 */
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimelineEvents(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time);
    /** @brief 按预期修订保存实体间有向关系。 */
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveDirectedRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    /** @brief 按故事时间与请求者权限读取实体关系。 */
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listDirectedRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    /** @brief 按预期修订保存地点层级与地图位置。 */
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocationPlacement(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    /** @brief 按预期修订保存地点之间的可通行路线。 */
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveTravelRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    /** @brief 读取指定世界的地点与路线地图视图。 */
    xuyan::domain::Result<xuyan::domain::MapView> loadMapView(const std::string& world_id);
    /** @brief 创建绑定世界版本的人物实例。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> createCharacterInstance(
        const std::string& command_id, xuyan::domain::CharacterInstance instance);
    /** @brief 读取指定人物实例的当前状态。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> loadCharacterInstance(const std::string& instance_id);
    /** @brief 列出某世界版本绑定的人物实例。 */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> listCharacterInstances(
        const std::string& world_version_id);
    /** @brief 按预期修订保存人物实例的记忆数据。 */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveCharacterInstanceMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    /** @brief 将分支根提交绑定到世界版本及人物实例集合。 */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, xuyan::domain::BranchRootBinding binding);
    /** @brief 读取指定分支的根绑定。 */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> loadBranchRootBinding(const std::string& branch_id);
    /** @brief 创建持久化模拟会话与额度配置。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSimulationSession(
        const std::string& command_id, xuyan::domain::SimulationSession session);
    /** @brief 读取指定模拟会话及其回合。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> loadSimulationSession(const std::string& session_id);
    /** @brief 列出当前工作区的模拟会话。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> listSimulationSessions();
    /** @brief 预留一个模拟回合及模型调用额度。 */
    xuyan::domain::Result<xuyan::domain::SimulationTurn> reserveSimulationTurn(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& actor_id, const std::string& request_hash);
    /** @brief 将人物意图和新状态作为回合结果原子提交。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitSimulationTurn(
        const std::string& command_id, const std::string& turn_id, int expected_session_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state,
        const std::string& draft_narration, int input_tokens, int output_tokens);
    /** @brief 将已提交回合的最终叙述写入记录。 */
    xuyan::domain::Result<xuyan::domain::SimulationTurn> finishSimulationNarration(
        const std::string& command_id, const std::string& turn_id, const std::string& final_narration);
    /** @brief 按修订和操作类型暂停、继续或取消模拟会话。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSimulationSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    /** @brief 将导演干预作为受审计的会话回合提交。 */
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitDirectorIntervention(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state);
    /** @brief 启动后恢复被进程中断的模拟会话与调用预留。 */
    xuyan::domain::Result<int> recoverInterruptedSimulationSessions();

private:
    /** @brief 共用候选提交事务；结果类型仅决定返回完整快照或轻量检查点。 */
    template<class JobResult> xuyan::domain::Result<JobResult> commitExtractionCandidatesImpl(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /** @brief 共用步骤结算事务，保证两种结果接口的幂等和修订语义一致。 */
    template<class JobResult> xuyan::domain::Result<JobResult> finishExtractionStepImpl(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /** @brief 共用取消事务，返回结果的范围不改变取消行为或命令日志。 */
    template<class JobResult> xuyan::domain::Result<JobResult> cancelExtractionJobImpl(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    sqlite3* database_{nullptr};

    /** @brief 创建或升级数据库结构，并保持旧工作区可读取。 */
    void migrate();
};

} // namespace xuyan::storage
