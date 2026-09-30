#include "xuyan/application/branch_outcome_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <fstream>
#include <set>
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

/*
 * 功能：把布尔状态转换为分支差异中的稳定协议文本。
 * 参数：value：输入，待转换布尔值，无默认值。
 * 返回：独立字符串 true 或 false；用于既有差异值，也用于当前场景摘要，不负责界面词条映射。
 * 失败：字符串分配异常可传播。
 * 副作用：仅内存转换，不修改状态或访问外部资源。
 * 线程与生命周期：调用线程同步执行，无引用、线程或回调资源。
 */
std::string booleanText(bool value) { return value ? "true" : "false"; }

/*
 * 功能：比较同一字段在左右分支的文本值，仅有差异时追加记录。
 * 参数：
 *   result：输出，调用期间借用的差异数组，保留已有条目，无默认值、无独立条数上限。
 *   field：输入，内部字段键，按值接收，仅有差异时移入条目，无默认值，不作中文化或合法性校验。
 *   left：输入，左侧序列化值，无默认值，只借用至返回，允许空串。
 *   right：输入，右侧序列化值，无默认值，只借用至返回，允许空串。
 * 返回：无；两值相同不追加条目。
 * 失败：容器扩容或字符串复制异常可传播。
 * 副作用：有差异时追加一条拥有字段/值副本的记录，可能使 result 原有元素引用失效。
 * 线程与生命周期：调用线程同步执行，不保留任何输入引用。
 */
void addDifference(std::vector<BranchDifference>& result, std::string field,
                   const std::string& left, const std::string& right) {
    if (left != right) result.push_back({std::move(field), left, right});
}

/*
 * 功能：先写同级临时文件，已有目标先改名为 previous，再重命名新文件；第二次重命名失败尝试恢复旧目标。
 * 参数：
 *   destination：输入，非空目标文件路径，无默认值，只借用至返回；调用方须独占目标和同级辅助文件名。
 *   content：输入，全部导出字节，无默认值，允许空串，只借用至写入结束；字节数转为 streamsize 后写入。
 * 返回：成功 Result 含目标路径字符串；失败含 validation_failed 或可重试 storage_error 及操作建议。
 * 失败：空路径明确拒绝；创建父目录、打开/write/flush、哈希或重命名标准异常转为错误。
 *   恢复旧目标也可能失败；关闭时错误不单独检查，重命名后返回值分配失败仍可能已写入目标。
 * 副作用：创建父目录，截断哈希命名的临时文件；替换前及成功后尝试删除同名 previous，忽略删除错误。
 *   失败不统一清理临时/previous；两次重命名之间目标可暂时不存在，不提供整个替换流程或崩溃的原子性保证。
 *   不加密、脱敏或调用持久化屏障；辅助文件可能保留用户正文，错误消息可能含本地路径。
 * 线程与生命周期：调用线程同步执行，无锁、取消或回调；输出流在首次重命名前关闭，不保存输入引用。
 */
Result<std::string> writeAtomically(const std::filesystem::path& destination, const std::string& content) {
    if (destination.empty()) return Result<std::string>::failure(
        {ErrorCode::validation_failed, "导出路径不能为空", false, "选择目标文件"});
    try {
        if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
        /* 临时名取内容哈希前 12 位，不是随机独占名；相同目标/内容重复导出可截断同名残留文件。 */
        const auto temporary = destination.parent_path() /
            (destination.filename().string() + ".tmp-" + xuyan::domain::sha256(content).substr(0, 12));
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("无法创建导出临时文件");
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output) throw std::runtime_error("写入导出临时文件失败");
        }
        /* previous 名仅取决于目标路径，替换前会尝试删除同名文件；不是长期备份，调用方须独占该辅助名。 */
        const auto backup = destination.parent_path() /
            (destination.filename().string() + ".previous-" + xuyan::domain::sha256(destination.string()).substr(0, 12));
        const bool replacing = std::filesystem::exists(destination);
        if (replacing) {
            std::error_code ignored; std::filesystem::remove(backup, ignored);
            std::filesystem::rename(destination, backup);
        }
        /* 旧目标已移走后再发布新文件；异常时仅在目标不存在且 previous 尚在时尝试回滚。 */
        try { std::filesystem::rename(temporary, destination); }
        catch (...) {
            if (replacing && !std::filesystem::exists(destination) && std::filesystem::exists(backup))
                std::filesystem::rename(backup, destination);
            throw;
        }
        if (replacing) { std::error_code ignored; std::filesystem::remove(backup, ignored); }
        return Result<std::string>::success(destination.string());
    } catch (const std::exception& exception) {
        /* 仅返回错误，不删除剩余临时文件或 previous；回滚异常可能替代原重命名错误。 */
        return Result<std::string>::failure({ErrorCode::storage_error, exception.what(), true, "检查目标路径和磁盘空间"});
    }
}

/*
 * 功能：沿提交父链从头结点回溯到指定祖先，再反转为祖先之后的提交顺序。
 * 参数：
 *   repository：输入，已打开仓储的非拥有引用，无默认值，须有效至调用结束。
 *   head：输入，起点提交 ID，无默认值，只借用至返回，空值返回空列表。
 *   ancestor：输入，截止提交 ID，无默认值，只借用至返回；空值表示回溯到根之后的空父 ID。
 * 返回：从旧到新的提交 ID 数组，不含祖先；head 等于 ancestor 返回空，祖先不在链上时返回整条已遍历链。
 * 失败：仓储 Result 失败转为 runtime_error，分配异常传播；依赖父链无环，没有循环检测或长度上限。
 * 副作用：只读提交，不修改仓储；返回数组拥有 ID 副本。
 * 线程与生命周期：调用线程同步执行，不保存仓储或输入字符串引用。
 */
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

/*
 * 功能：把一个会话已记录的调用和输入/输出令牌数累加到分支比较的一侧。
 * 参数：
 *   comparison：输出，待累计的比较对象，无默认值，只借用至返回，不清零已有计数。
 *   session：输入，会话值对象，无默认值，只借用至返回；由调用方筛选分支，本函数累加全部回合，不按状态过滤。
 *   left：输入，无默认值；true 累加左侧，false 累加右侧。
 * 返回：无。
 * 失败：不主动返回业务错误；使用 int 累计且不检测溢出，调用方须确保回合令牌和累计调用数可表示。
 * 副作用：增加已用/未知调用数（单位次）和输入/输出令牌数（单位 token），不计算金额、不写仓储。
 * 线程与生命周期：调用线程同步执行，不保留会话或比较对象引用。
 */
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

/*
 * 功能：仅从既有场景字段生成回合、seal 持有人/检查状态和逐人物信任概览，未实现通用世界状态摘要。
 * 参数：head：输入，提交视图，无默认值，只借用至返回；人物名称为空时使用其 ID，不做真实性或可见权限校验。
 * 返回：独立拥有的摘要，包含用户名称/稳定 ID、既有整数信任值及 true/false 文本；不含其他状态字段。
 * 失败：流/字符串分配异常可传播，不单独检查流状态，未验证场景数据完整性。
 * 副作用：只读内存状态，不脱敏、不修改事实或生成原文证据；摘要可暴露人物名称和资源持有人。
 * 线程与生命周期：调用线程同步执行，不保留提交引用，局部文本流随退出释放。
 */
std::string stateSummary(const CommitView& head) {
    std::ostringstream out;
    out << "回合 " << head.state.turn << "；唯一物品持有人 " << head.state.seal_holder_id
        << "；已检查=" << booleanText(head.state.seal_inspected);
    for (const auto& actor : head.state.characters)
        out << "；" << (actor.name.empty() ? actor.id : actor.name) << "信任=" << actor.trust;
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
        /* 左头至根的 ID 集合，初始空；沿右父链的首次命中作为共同祖先，依赖单父无环链，不比较世界事实。 */
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
        /* 仅覆盖既有固定场景状态及人物存在/两项知识/信任；英文键和 true/false 为当前内部值，通用化仍属 XF-25 欠账。 */
        addDifference(result.differences, "turn", std::to_string(left.value->state.turn), std::to_string(right.value->state.turn));
        addDifference(result.differences, "elapsed_ticks", std::to_string(left.value->state.elapsed_ticks), std::to_string(right.value->state.elapsed_ticks));
        addDifference(result.differences, "seal_holder_id", left.value->state.seal_holder_id, right.value->state.seal_holder_id);
        addDifference(result.differences, "seal_inspected", booleanText(left.value->state.seal_inspected), booleanText(right.value->state.seal_inspected));
        addDifference(result.differences, "completed", booleanText(left.value->state.completed), booleanText(right.value->state.completed));
        std::set<std::string> actor_ids;
        for (const auto& actor : left.value->state.characters) actor_ids.insert(actor.id);
        for (const auto& actor : right.value->state.characters) actor_ids.insert(actor.id);
        for (const auto& actor_id : actor_ids) {
            /* 借用局部左右头状态中的人物；缺失返回空，不把缺席人物的知识/信任默认成 0，指针不逃逸本次比较。 */
            const auto* l = xuyan::domain::findCharacter(left.value->state, actor_id);
            const auto* r = xuyan::domain::findCharacter(right.value->state, actor_id);
            addDifference(result.differences, actor_id + ".present", booleanText(l != nullptr), booleanText(r != nullptr));
            if (!l || !r) continue;
            addDifference(result.differences, actor_id + ".knows_gate_closure", booleanText(l->knows_gate_closure), booleanText(r->knows_gate_closure));
            addDifference(result.differences, actor_id + ".knows_seal_forgery", booleanText(l->knows_seal_forgery), booleanText(r->knows_seal_forgery));
            addDifference(result.differences, actor_id + ".trust", std::to_string(l->trust), std::to_string(r->trust));
        }
        /* 所有匹配分支的会话均参与用量累计，不限共同祖先之后；查询不与头读取共用显式读快照。 */
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
        /*
         * 功能：在当前仓储返回的分支列表中寻找调用方指定的稳定 ID。
         * 参数：value：输入，本次遍历借用的分支记录，无默认值；branch_id 从外层按引用捕获，算法期间有效。
         * 返回：分支 ID 相同为 true，否则为 false，不比较名称或世界归属。
         * 失败：无主动业务错误，仅比较字符串。
         * 副作用：只读分支及捕获的 ID。
         * 线程与生命周期：调用线程同步执行，闭包不逃逸 find_if，不保存元素引用。
         */
        auto branch = std::find_if(branches.value->begin(), branches.value->end(), [&](const auto& value) { return value.id == branch_id; });
        if (branch == branches.value->end()) return Result<std::string>::failure(
            {ErrorCode::missing_context, "找不到导出分支", false, "刷新分支列表"});
        auto sessions = repository.listSimulationSessions(); if (!sessions.ok()) return Result<std::string>::failure(*sessions.error);
        /* 当前分支会话中状态已完成且提交 ID 非空的回合副本，初始空，无服务内数量上限；尚未核对提交是否在头父链上。 */
        std::vector<xuyan::domain::SimulationTurn> turns;
        /* 全部匹配会话的调用/未知调用（单位次）及所有回合的输入/输出令牌（单位 token），初始 0，不限已导出回合且不检查溢出。 */
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
        /* 当前头父链由根到头的零基提交位置，单位个，初始空；排序用 operator[]，缺失 ID 会生成位置 0。 */
        std::unordered_map<std::string, std::size_t> commit_position;
        for (std::size_t index = 0; index < commit_order.size(); ++index) commit_position[commit_order[index]] = index;
        /*
         * 功能：按已提交的父链位置排列可导出的回合。
         * 参数：
         *   a：输入，待比较左回合，无默认值，排序期间借用。
         *   b：输入，待比较右回合，无默认值，排序期间借用。
         *   commit_position 从外层按引用捕获，在本次排序中有效，存放零基父链位置。
         * 返回：a 的提交位置严格小于 b 为 true；同位置为 false，std::sort 不保证同位置顺序。
         * 失败：缺失 ID 的映射插入可能分配异常；不把缺失提交作为业务错误拒绝。
         * 副作用：operator[] 可为不在头父链的 ID 插入默认位置 0，不修改回合或仓储。
         * 线程与生命周期：调用线程同步执行，闭包不逃逸 sort，不保留回合引用。
         */
        std::sort(turns.begin(), turns.end(), [&](const auto& a, const auto& b) {
            return commit_position[a.committed_commit_id] < commit_position[b.committed_commit_id];
        });
        /* JSON 固定包含技术标识、操作和用户内容，不受 include_technical_log 控制；此为正文导出而非脱敏诊断。 */
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
        /* 诊断仅序列化会话 ID 无盐摘要及状态/计数/修订；哈希仍可关联，状态原样保留，未对自由文本作脱敏。 */
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
        /* 新建的是通用 event 资料，未生成专用时间线记录或原文证据；ID 只绑定分支/头，不含目标 world_id。 */
        xuyan::domain::WorldEntity material;
        material.id = "entity-" + xuyan::domain::sha256("simulation-result|" + branch_id + '|' + head.value->commit_id).substr(0, 20);
        material.world_id = world_id; material.kind = "event"; material.name = title;
        material.description = stateSummary(*head.value);
        /* 保留推演来源与 candidate 性质，进入世界版本不等于原文事实；本入口也不核对分支与目标世界是否对应。 */
        material.attributes_json = xuyan::package::writeJson(JsonValue::Object{
            {"xuyan_provenance_type", "simulation_result"}, {"xuyan_truth_status", "candidate"},
            {"branch_id", branch_id}, {"head_commit_id", head.value->commit_id}, {"state_hash", head.value->state_hash}});
        /* 资料子命令先独立提交；随后读取版本或发布失败不会回滚已创建资料。 */
        auto created = repository.createEntity(command_id + ":material", std::move(material));
        if (!created.ok()) return Result<xuyan::domain::WorldVersion>::failure(*created.error);
        auto versions = repository.listWorldVersions(world_id);
        if (!versions.ok()) return Result<xuyan::domain::WorldVersion>::failure(*versions.error);
        /* 按仓储版本列表末项选择父版本；每次重放重新读取，成功发布后父 ID 已变化，子命令日志可能返回负载冲突。 */
        const auto parent = versions.value->empty() ? std::string{} : versions.value->back().id;
        return repository.publishWorldVersion(command_id + ":version", world_id, parent);
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::WorldVersion>::failure({ErrorCode::storage_error, exception.what(), true, "重新打开工作区"});
    }
}

} // namespace xuyan::application
