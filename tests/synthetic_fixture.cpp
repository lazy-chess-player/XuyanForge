#include "synthetic_fixture.h"

#include "xuyan/domain/hash.h"
#include "xuyan/engine/mock_provider.h"
#include "xuyan/engine/simulation_engine.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <utility>

namespace xuyan::test {
namespace {

/*
 * 功能：将合成意图应用于当前状态，成功后推进回合及逻辑时钟，不替代持久化提交。
 * 参数：current：输入，只读状态引用；intent：输入，待执行人物意图；均仅在调用期间借用。
 * 返回：成功为独立下一状态；说话仅推进修订，其他操作先经领域规则校验。
 * 失败：意图转换或领域操作错误返回失败；复制/分配异常向外传播。
 * 副作用：不修改输入、不读写数据库；结束标记来自 intent，逻辑时钟单位为 tick。
 * 线程与生命周期：调用线程同步执行；返回状态拥有字段，不保存输入引用或在后台执行。
 */
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
    /*
     * 功能：寻找本命令预留的历史回合，避免重放时再消耗测试预算。
     * 参数：turn：输入，当前会话数组中的只读回合引用。
     * 返回：请求摘要以 command_id 加分隔符开头时为 true。
     * 失败：字符串构造异常向外传播；不校验历史回合状态。
     * 副作用：无；同步借用 command_id，闭包不逃逸当前查找。
     */
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
    /*
     * 功能：定位已返回意图但待人工审核的回合，走恢复提交路径。
     * 参数：turn：输入，会话数组中的只读回合引用。
     * 返回：状态为 needs_review 时为 true；无匹配由 find_if 返回尾迭代器。
     * 失败：无显式失败路径。
     * 副作用：无；无捕获，同步调用，不保存回合引用。
     */
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
    /*
     * 功能：从事实提交结果中找回本次预留回合，决定是否完成叙述。
     * 参数：turn：输入，提交后会话中的只读回合引用。
     * 返回：回合 ID 与 reserved 中的预留 ID 一致时为 true。
     * 失败：无显式失败路径；缺失匹配由调用方转为存储错误。
     * 副作用：无；同步借用 reserved，其结果在查找期间保持有效。
     */
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
