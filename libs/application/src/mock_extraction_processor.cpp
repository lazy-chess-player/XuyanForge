#include "xuyan/application/mock_extraction_processor.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/package/json.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace xuyan::application {
namespace {

/** @brief 生成离线步骤的稳定命令 ID，避免恢复后重复提交。 */
std::string commandId(std::string_view prefix, const std::string& job_id, int revision) {
    return std::string(prefix) + '-' + xuyan::domain::sha256(job_id + '|' + std::to_string(revision)).substr(0, 24);
}

/** @brief 用短锁保护进程内正在执行的工作区/任务集合，不持锁执行 SQL 或调用用户回调。 */
struct BatchRegistry {
    std::mutex mutex;
    std::unordered_set<std::string> active;
};

/** @brief 返回进程内唯一的离线批次登记表。 */
BatchRegistry& batchRegistry() {
    static BatchRegistry registry;
    return registry;
}

/** @brief 以作用域登记防止同一任务的批次重复启动，异常返回也会释放登记。 */
class BatchLease {
public:
    /** @brief 按规范化数据库路径和任务 ID 登记一次批次，不阻塞等待已有批次。 */
    BatchLease(const std::filesystem::path& database, const std::string& job_id)
        : key_(std::filesystem::weakly_canonical(database).generic_string() + '\n' + job_id) {
        auto& registry = batchRegistry();
        std::lock_guard lock(registry.mutex);
        acquired_ = registry.active.insert(key_).second;
    }
    /** @brief 仅移除本对象成功取得的登记，不干扰其他正在运行的批次。 */
    ~BatchLease() {
        if (acquired_) {
            auto& registry = batchRegistry();
            std::lock_guard lock(registry.mutex);
            registry.active.erase(key_);
        }
    }
    BatchLease(const BatchLease&) = delete;
    BatchLease& operator=(const BatchLease&) = delete;
    /** @brief 判断是否成功取得本工作区/任务的独占批次登记。 */
    bool acquired() const noexcept { return acquired_; }
private:
    std::string key_;
    bool acquired_{false};
};

} // namespace

MockExtractionProcessor::MockExtractionProcessor(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::ExtractionJob> MockExtractionProcessor::processNext(const std::string& job_id) {
    ExtractionJobService jobs(database_path_);
    auto job = jobs.load(job_id);
    if (!job.ok()) return job;
    if (job.value->status == "completed" || job.value->status == "cancelled") return job;
    if (job.value->schema_version != "candidate-v1" || job.value->prompt_version != "extract-v1")
        return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
            {xuyan::domain::ErrorCode::validation_failed, "此任务不能用离线规则替代类型化模型提取", false,
             "使用任务绑定的模型协议或新建离线任务"});
    auto claimed = jobs.claimNext(commandId("mock-claim", job_id, job.value->revision), job_id, job.value->revision);
    if (!claimed.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*claimed.error);
    SourceImportService sources(database_path_);
    auto chunk = sources.evidenceText(job.value->source_id,
                                      claimed.value->start_codepoint, claimed.value->end_codepoint);
    if (!chunk.ok()) {
        // 已领取步骤必须落盘为终结失败，不能因本地资产损坏而永久停在运行中。
        return jobs.finishStep(commandId("mock-missing-source", job_id, job.value->revision), job_id,
                               claimed.value->ordinal, claimed.value->attempt, "failed", {},
                               "原文片段无法读取；请检查工作区资产后重试");
    }

    // 只对原文句段打启发式分数，离线样本不会读取凭据或访问网络。
    struct Sentence {
        std::size_t start_byte;
        std::size_t end_byte;
        std::size_t start_cp;
        std::size_t end_cp;
        int score;
    };
    constexpr std::array<std::string_view, 15> action_terms{
        "决定", "发现", "得知", "获得", "失去", "击败", "死亡", "杀死", "救出", "抵达",
        "离开", "开始", "结束", "承认", "背叛"};
    std::vector<Sentence> ranked;
    const auto& input = *chunk.value;
    std::size_t sentence_start_byte = 0, sentence_start_cp = 0, codepoint = 0;
    for (std::size_t byte = 0; byte < input.size();) {
        const auto lead = static_cast<unsigned char>(input[byte]);
        const auto width = lead < 0x80 ? 1U : lead < 0xe0 ? 2U : lead < 0xf0 ? 3U : 4U;
        const bool terminal = input[byte] == '\n' || input.compare(byte, 3, "。") == 0
            || input.compare(byte, 3, "！") == 0 || input.compare(byte, 3, "？") == 0;
        byte += width; ++codepoint;
        if (!terminal && byte < input.size()) continue;
        const auto sentence = std::string_view(input).substr(sentence_start_byte, byte - sentence_start_byte);
        int score = 0;
        for (const auto term : action_terms) if (sentence.find(term) != std::string_view::npos) score += 2;
        if (sentence.find("因为") != std::string_view::npos || sentence.find("因此") != std::string_view::npos) ++score;
        if (sentence.find("仿佛") != std::string_view::npos || sentence.find("颜色") != std::string_view::npos) --score;
        if (score > 0 && codepoint - sentence_start_cp >= 5 && codepoint - sentence_start_cp <= 180)
            ranked.push_back({sentence_start_byte, byte, sentence_start_cp, codepoint, score});
        sentence_start_byte = byte; sentence_start_cp = codepoint;
    }
    // 最多挑选五个动作句，再恢复原文顺序以便人工对照证据。
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });
    if (ranked.size() > 5) ranked.resize(5);
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.start_byte < right.start_byte;
    });
    xuyan::package::JsonValue::Array candidates;
    for (const auto& sentence : ranked) {
        const auto quote = input.substr(sentence.start_byte, sentence.end_byte - sentence.start_byte);
        auto title = xuyan::domain::codepointSlice(quote, 0, std::min<std::size_t>(36, sentence.end_cp - sentence.start_cp));
        if (!title.ok()) continue;
        candidates.emplace_back(xuyan::package::JsonValue::Object{
            {"type", "event"}, {"name", *title.value},
            {"fields", xuyan::package::JsonValue::Object{{"extraction_method", "offline_action_sentence"},
                                                          {"confidence", "requires_review"}}},
            {"start_codepoint", static_cast<std::int64_t>(claimed.value->start_codepoint + sentence.start_cp)},
            {"end_codepoint", static_cast<std::int64_t>(claimed.value->start_codepoint + sentence.end_cp)},
            {"quote", quote}, {"provenance_type", "model_inference"}});
    }
    const auto output = xuyan::package::writeJson(xuyan::package::JsonValue::Object{
        {"schema_version", "candidate-v1"}, {"prompt_version", "extract-v1"}, {"candidates", std::move(candidates)}});
    CandidateService ingestion(database_path_);
    auto committed = ingestion.ingestStepOutput(
        commandId("mock-commit", job_id, job.value->revision), job_id, claimed.value->ordinal, claimed.value->attempt, output);
    if (!committed.ok()) {
        jobs.finishStep(commandId("mock-fail", job_id, job.value->revision), job_id, claimed.value->ordinal,
                        claimed.value->attempt, "failed", {}, committed.error->message);
    }
    return committed;
}

xuyan::domain::Result<OfflineBatchResult> MockExtractionProcessor::processBatch(
    const std::string& job_id, const OfflineBatchOptions& options) {
    using Result = xuyan::domain::Result<OfflineBatchResult>;
    using xuyan::domain::ErrorCode;
    if (options.maximum_steps < 1 || options.maximum_steps > 100000)
        return Result::failure({ErrorCode::validation_failed, "离线执行步骤上限无效", false, "使用 1—100000"});
    try {
        BatchLease lease(database_path_, job_id);
        if (!lease.acquired()) return Result::failure(
            {ErrorCode::rule_conflict, "此小说解析任务已有批次正在执行", false, "等待已有批次停止后继续"});
        ExtractionJobService jobs(database_path_);
        auto current = jobs.load(job_id);
        if (!current.ok()) return Result::failure(*current.error);
        int processed = 0;
        for (;;) {
            // 初始状态及每片已落盘的检查点才发通知；不把整本正文或候选输出送入进度回调。
            auto action = OfflineBatchAction::proceed;
            if (options.on_progress) {
                try {
                    action = options.on_progress({job_id, current.value->total_steps,
                        current.value->completed_steps, processed, current.value->revision});
                } catch (...) {
                    return Result::failure({ErrorCode::validation_failed,
                        "离线解析进度通知失败；已提交切片仍保留", true, "修复进度接收方后从检查点继续"});
                }
            }
            // 最后一个切片已完成时优先报告完成，不能再把完成任务改写成取消。
            if (current.value->status == "completed") return Result::success(
                {std::move(*current.value), processed, OfflineBatchStopReason::completed});
            if (current.value->status == "cancelled") return Result::success(
                {std::move(*current.value), processed, OfflineBatchStopReason::cancelled});
            if (current.value->status == "needs_attention") return Result::success(
                {std::move(*current.value), processed, OfflineBatchStopReason::needs_attention});
            if (action == OfflineBatchAction::cancel) {
                // 在当前事务结束后持久化取消；剩余 ready 切片作废，已经提交的证据与候选不删除。
                auto cancelled = jobs.cancel(commandId("offline-batch-cancel", job_id, current.value->revision),
                                             job_id, current.value->revision);
                if (!cancelled.ok()) return Result::failure(*cancelled.error);
                return Result::success({std::move(*cancelled.value), processed, OfflineBatchStopReason::cancelled});
            }
            if (action == OfflineBatchAction::pause || options.stop_token.stop_requested())
                return Result::success({std::move(*current.value), processed, OfflineBatchStopReason::paused});
            if (processed == options.maximum_steps) return Result::success(
                {std::move(*current.value), processed, OfflineBatchStopReason::step_limit});
            current = processNext(job_id);
            if (!current.ok()) return Result::failure(*current.error);
            ++processed;
        }
    } catch (const std::exception&) {
        // 不暴露路径、SQL 或回调中的私人内容；已提交步骤由原有事务与幂等命令保护。
        return Result::failure({ErrorCode::storage_error, "离线解析批次无法继续", true, "检查工作区后从检查点继续"});
    }
}

xuyan::domain::Result<xuyan::domain::ExtractionJob> MockExtractionProcessor::processAll(
    const std::string& job_id, int maximum_steps) {
    OfflineBatchOptions options;
    options.maximum_steps = maximum_steps;
    auto batch = processBatch(job_id, options);
    if (!batch.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*batch.error);
    return xuyan::domain::Result<xuyan::domain::ExtractionJob>::success(std::move(batch.value->job));
}

} // namespace xuyan::application
