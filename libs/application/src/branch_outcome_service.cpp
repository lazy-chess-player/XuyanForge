#include "xuyan/application/branch_outcome_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace xuyan::application {
namespace {

using xuyan::domain::BranchComparison;
using xuyan::domain::BranchDifference;
using xuyan::domain::CommitView;
using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

std::string booleanText(bool value) { return value ? "true" : "false"; }

void addDifference(std::vector<BranchDifference>& result, std::string field,
                   const std::string& left, const std::string& right) {
    if (left != right) result.push_back({std::move(field), left, right});
}

Result<std::string> writeAtomically(const std::filesystem::path& destination, const std::string& content) {
    if (destination.empty()) return Result<std::string>::failure(
        {ErrorCode::validation_failed, "导出路径不能为空", false, "选择目标文件"});
    try {
        if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
        const auto temporary = destination.parent_path() /
            (destination.filename().string() + ".tmp-" + xuyan::domain::sha256(content).substr(0, 12));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("无法创建导出临时文件");
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output) throw std::runtime_error("写入导出临时文件失败");
        }
        const auto backup = destination.parent_path() /
            (destination.filename().string() + ".previous-" + xuyan::domain::sha256(destination.string()).substr(0, 12));
        const bool replacing = std::filesystem::exists(destination);
        if (replacing) {
            std::error_code ignored; std::filesystem::remove(backup, ignored);
            std::filesystem::rename(destination, backup);
        }
        try { std::filesystem::rename(temporary, destination); }
        catch (...) {
            if (replacing && !std::filesystem::exists(destination) && std::filesystem::exists(backup))
                std::filesystem::rename(backup, destination);
            throw;
        }
        if (replacing) { std::error_code ignored; std::filesystem::remove(backup, ignored); }
        return Result<std::string>::success(destination.string());
    } catch (const std::exception& exception) {
        return Result<std::string>::failure({ErrorCode::storage_error, exception.what(), true, "检查目标路径和磁盘空间"});
    }
}

std::vector<std::string> chainAfter(xuyan::storage::WorkspaceRepository& repository,
                                    const std::string& head, const std::string& ancestor) {
    std::vector<std::string> result;
    auto cursor = head;
    while (!cursor.empty() && cursor != ancestor) {
        result.push_back(cursor);
        auto commit = repository.loadCommit(cursor);
        if (!commit.ok()) throw std::runtime_error(commit.error->message);
        cursor = commit.value->parent_commit_id;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void addCost(BranchComparison& comparison, const xuyan::domain::SimulationSession& session, bool left) {
    int input = 0, output = 0;
    for (const auto& turn : session.turns) { input += turn.call.input_tokens; output += turn.call.output_tokens; }
    if (left) {
        comparison.left_calls += session.used_calls; comparison.left_unknown_calls += session.unknown_calls;
        comparison.left_input_tokens += input; comparison.left_output_tokens += output;
    } else {
        comparison.right_calls += session.used_calls; comparison.right_unknown_calls += session.unknown_calls;
        comparison.right_input_tokens += input; comparison.right_output_tokens += output;
    }
}

std::string stateSummary(const CommitView& head) {
    const auto* xu = xuyan::domain::findCharacter(head.state, "actor-xucheng");
    const auto* shen = xuyan::domain::findCharacter(head.state, "actor-shentang");
    std::ostringstream out;
    out << "回合 " << head.state.turn << "；印章持有人 " << head.state.seal_holder_id
        << "；已检查=" << booleanText(head.state.seal_inspected)
        << "；许澄信任=" << (xu ? xu->trust : 0) << "；沈棠信任=" << (shen ? shen->trust : 0);
    return out.str();
}

} // namespace

BranchOutcomeService::BranchOutcomeService(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

Result<BranchComparison> BranchOutcomeService::compare(
    const std::string& left_branch_id, const std::string& right_branch_id) {
    if (left_branch_id.empty() || right_branch_id.empty() || left_branch_id == right_branch_id)
        return Result<BranchComparison>::failure(
            {ErrorCode::validation_failed, "请选择两个不同分支", false, "重新选择比较分支"});
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto left = repository.loadHead(left_branch_id); auto right = repository.loadHead(right_branch_id);
        if (!left.ok()) return Result<BranchComparison>::failure(*left.error);
        if (!right.ok()) return Result<BranchComparison>::failure(*right.error);
        std::unordered_set<std::string> left_ancestors;
        auto cursor = left.value->commit_id;
        while (!cursor.empty()) {
            left_ancestors.insert(cursor);
            auto commit = repository.loadCommit(cursor); if (!commit.ok()) return Result<BranchComparison>::failure(*commit.error);
            cursor = commit.value->parent_commit_id;
        }
        cursor = right.value->commit_id;
        while (!cursor.empty() && !left_ancestors.contains(cursor)) {
            auto commit = repository.loadCommit(cursor); if (!commit.ok()) return Result<BranchComparison>::failure(*commit.error);
            cursor = commit.value->parent_commit_id;
        }
        if (cursor.empty()) return Result<BranchComparison>::failure(
            {ErrorCode::missing_context, "两个分支没有共同提交", false, "选择同一工作区内的相关分支"});
        BranchComparison result; result.left_branch_id = left_branch_id; result.right_branch_id = right_branch_id;
        result.common_commit_id = cursor; result.left_head_commit_id = left.value->commit_id; result.right_head_commit_id = right.value->commit_id;
        result.left_commit_ids = chainAfter(repository, result.left_head_commit_id, cursor);
        result.right_commit_ids = chainAfter(repository, result.right_head_commit_id, cursor);
        addDifference(result.differences, "turn", std::to_string(left.value->state.turn), std::to_string(right.value->state.turn));
        addDifference(result.differences, "elapsed_ticks", std::to_string(left.value->state.elapsed_ticks), std::to_string(right.value->state.elapsed_ticks));
        addDifference(result.differences, "seal_holder_id", left.value->state.seal_holder_id, right.value->state.seal_holder_id);
        addDifference(result.differences, "seal_inspected", booleanText(left.value->state.seal_inspected), booleanText(right.value->state.seal_inspected));
        addDifference(result.differences, "completed", booleanText(left.value->state.completed), booleanText(right.value->state.completed));
        for (const auto& actor_id : {std::string{"actor-xucheng"}, std::string{"actor-shentang"}}) {
            const auto* l = xuyan::domain::findCharacter(left.value->state, actor_id);
            const auto* r = xuyan::domain::findCharacter(right.value->state, actor_id);
            if (l && r) {
                addDifference(result.differences, actor_id + ".knows_gate_closure", booleanText(l->knows_gate_closure), booleanText(r->knows_gate_closure));
                addDifference(result.differences, actor_id + ".knows_seal_forgery", booleanText(l->knows_seal_forgery), booleanText(r->knows_seal_forgery));
                addDifference(result.differences, actor_id + ".trust", std::to_string(l->trust), std::to_string(r->trust));
            }
        }
        auto sessions = repository.listSimulationSessions();
        if (!sessions.ok()) return Result<BranchComparison>::failure(*sessions.error);
        for (const auto& session : *sessions.value) {
            if (session.branch_id == left_branch_id) addCost(result, session, true);
            if (session.branch_id == right_branch_id) addCost(result, session, false);
        }
        return Result<BranchComparison>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<BranchComparison>::failure({ErrorCode::storage_error, exception.what(), true, "重新打开工作区"});
    }
}

Result<std::string> BranchOutcomeService::exportBranch(
    const std::string& branch_id, const std::filesystem::path& destination,
    const std::string& format, bool include_technical_log) {
    if (format != "markdown" && format != "json" && format != "text")
        return Result<std::string>::failure({ErrorCode::validation_failed, "未知导出格式", false, "选择 markdown、json 或 text"});
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto head = repository.loadHead(branch_id); if (!head.ok()) return Result<std::string>::failure(*head.error);
        auto branches = repository.listBranches(); if (!branches.ok()) return Result<std::string>::failure(*branches.error);
        auto branch = std::find_if(branches.value->begin(), branches.value->end(), [&](const auto& value) { return value.id == branch_id; });
        if (branch == branches.value->end()) return Result<std::string>::failure(
            {ErrorCode::missing_context, "找不到导出分支", false, "刷新分支列表"});
        auto sessions = repository.listSimulationSessions(); if (!sessions.ok()) return Result<std::string>::failure(*sessions.error);
        std::vector<xuyan::domain::SimulationTurn> turns;
        int calls = 0, unknown = 0, input_tokens = 0, output_tokens = 0;
        for (const auto& session : *sessions.value) if (session.branch_id == branch_id) {
            calls += session.used_calls; unknown += session.unknown_calls;
            for (const auto& turn : session.turns) {
                input_tokens += turn.call.input_tokens; output_tokens += turn.call.output_tokens;
                if (turn.status == "completed" && !turn.committed_commit_id.empty()) turns.push_back(turn);
            }
        }
        std::vector<std::string> commit_order;
        auto cursor = head.value->commit_id;
        while (!cursor.empty()) {
            commit_order.push_back(cursor);
            auto commit = repository.loadCommit(cursor);
            if (!commit.ok()) return Result<std::string>::failure(*commit.error);
            cursor = commit.value->parent_commit_id;
        }
        std::reverse(commit_order.begin(), commit_order.end());
        std::unordered_map<std::string, std::size_t> commit_position;
        for (std::size_t index = 0; index < commit_order.size(); ++index) commit_position[commit_order[index]] = index;
        std::sort(turns.begin(), turns.end(), [&](const auto& a, const auto& b) {
            return commit_position[a.committed_commit_id] < commit_position[b.committed_commit_id];
        });
        if (format == "json") {
            JsonValue::Array records;
            for (const auto& turn : turns) records.emplace_back(JsonValue::Object{
                {"turn_id", turn.id}, {"actor_id", turn.actor_id}, {"input_commit_id", turn.input_commit_id},
                {"committed_commit_id", turn.committed_commit_id}, {"speech", turn.intent.speech},
                {"operation", turn.intent.operation}, {"target_id", turn.intent.target_id}, {"narration", turn.final_narration}});
            JsonValue document(JsonValue::Object{
                {"schema_version", "branch-export-v1"}, {"branch_id", branch_id}, {"branch_name", branch->name},
                {"head_commit_id", head.value->commit_id}, {"state_hash", head.value->state_hash},
                {"state_summary", stateSummary(*head.value)}, {"records", std::move(records)},
                {"usage", JsonValue::Object{{"calls", calls}, {"unknown_calls", unknown},
                    {"input_tokens", input_tokens}, {"output_tokens", output_tokens}}}});
            return writeAtomically(destination, xuyan::package::writeJson(document));
        }
        std::ostringstream out;
        if (format == "markdown") out << "# " << branch->name << "\n\n";
        out << "基线分支：" << branch_id << "\n提交：" << head.value->commit_id << "\n状态摘要：" << stateSummary(*head.value) << "\n";
        if (format == "markdown") out << "\n## 场景记录\n\n";
        for (const auto& turn : turns) {
            if (format == "markdown") out << "### " << turn.actor_id << "\n\n";
            out << turn.intent.speech << "\n";
            if (!turn.final_narration.empty() && turn.final_narration != turn.intent.speech) out << turn.final_narration << "\n";
            if (include_technical_log) out << "[" << turn.intent.operation << " · " << turn.committed_commit_id << "]\n";
            out << "\n";
        }
        if (format == "markdown") out << "## 用量摘要\n\n";
        out << "调用 " << calls << "，输入 token " << input_tokens << "，输出 token " << output_tokens << "，unknown " << unknown << "。\n";
        return writeAtomically(destination, out.str());
    } catch (const std::exception& exception) {
        return Result<std::string>::failure({ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"});
    }
}

Result<std::string> BranchOutcomeService::exportDiagnostics(const std::filesystem::path& destination) {
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto branches = repository.listBranches(); if (!branches.ok()) return Result<std::string>::failure(*branches.error);
        auto sessions = repository.listSimulationSessions(); if (!sessions.ok()) return Result<std::string>::failure(*sessions.error);
        JsonValue::Array session_rows;
        for (const auto& session : *sessions.value) session_rows.emplace_back(JsonValue::Object{
            {"id_hash", xuyan::domain::sha256(session.id)}, {"status", session.status}, {"turns", static_cast<int>(session.turns.size())},
            {"used_calls", session.used_calls}, {"unknown_calls", session.unknown_calls}, {"revision", session.revision}});
        JsonValue report(JsonValue::Object{{"schema_version", "diagnostics-v1"},
            {"privacy", "content fields and local identifiers omitted"},
            {"branch_count", static_cast<int>(branches.value->size())}, {"session_count", static_cast<int>(sessions.value->size())},
            {"sessions", std::move(session_rows)}});
        return writeAtomically(destination, xuyan::package::writeJson(report));
    } catch (const std::exception& exception) {
        return Result<std::string>::failure({ErrorCode::storage_error, exception.what(), true, "重新打开工作区"});
    }
}

Result<xuyan::domain::WorldVersion> BranchOutcomeService::adoptAsWorldVersion(
    const std::string& command_id, const std::string& branch_id,
    const std::string& world_id, const std::string& title) {
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto head = repository.loadHead(branch_id);
        if (!head.ok()) return Result<xuyan::domain::WorldVersion>::failure(*head.error);
        xuyan::domain::WorldEntity material;
        material.id = "entity-" + xuyan::domain::sha256("simulation-result|" + branch_id + '|' + head.value->commit_id).substr(0, 20);
        material.world_id = world_id; material.kind = "event"; material.name = title;
        material.description = stateSummary(*head.value);
        material.attributes_json = xuyan::package::writeJson(JsonValue::Object{
            {"xuyan_provenance_type", "simulation_result"}, {"xuyan_truth_status", "candidate"},
            {"branch_id", branch_id}, {"head_commit_id", head.value->commit_id}, {"state_hash", head.value->state_hash}});
        auto created = repository.createEntity(command_id + ":material", std::move(material));
        if (!created.ok()) return Result<xuyan::domain::WorldVersion>::failure(*created.error);
        auto versions = repository.listWorldVersions(world_id);
        if (!versions.ok()) return Result<xuyan::domain::WorldVersion>::failure(*versions.error);
        const auto parent = versions.value->empty() ? std::string{} : versions.value->back().id;
        return repository.publishWorldVersion(command_id + ":version", world_id, parent);
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::WorldVersion>::failure({ErrorCode::storage_error, exception.what(), true, "重新打开工作区"});
    }
}

} // namespace xuyan::application
