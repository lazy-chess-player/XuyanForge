#include "xuyan/application/extraction_job_service.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/application/extraction_output_contract.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <vector>

namespace xuyan::application {
namespace {

struct TextBoundaries {
    std::vector<std::size_t> paragraphs;
    std::vector<std::size_t> sentences;
};

/** @brief 单次扫描标准化 UTF-8 正文，建立单换行段尾与句末的码点边界索引。 */
TextBoundaries textBoundaries(std::string_view utf8) {
    TextBoundaries result;
    std::size_t codepoint = 0;
    for (std::size_t offset = 0; offset < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[offset]);
        const auto width = lead < 0x80 ? 1U : lead < 0xe0 ? 2U : lead < 0xf0 ? 3U : 4U;
        const auto current = utf8.substr(offset, width);
        if (current == "\n") result.paragraphs.push_back(codepoint + 1);
        if (current == "。" || current == "！" || current == "？" || current == "；"
            || current == "!" || current == "?" || current == ";") {
            auto sentence_end = codepoint + 1;
            for (auto tail = offset + width; tail < utf8.size();) {
                const auto remaining = utf8.substr(tail);
                if (remaining.starts_with("”") || remaining.starts_with("’")
                    || remaining.starts_with("」") || remaining.starts_with("』")
                    || remaining.starts_with("）")) tail += 3;
                else if (remaining.starts_with('"') || remaining.starts_with(')')) ++tail;
                else break;
                ++sentence_end;
            }
            result.sentences.push_back(sentence_end);
        }
        offset += width; ++codepoint;
    }
    return result;
}

/** @brief 在允许长度区间内选择最靠后的边界；不存在时返回零以触发下级回退。 */
std::size_t lastBoundaryInRange(const std::vector<std::size_t>& boundaries,
                                std::size_t minimum, std::size_t maximum) {
    const auto after = std::upper_bound(boundaries.begin(), boundaries.end(), maximum);
    if (after == boundaries.begin()) return 0;
    const auto boundary = *std::prev(after);
    return boundary >= minimum ? boundary : 0;
}

} // namespace

ExtractionJobService::ExtractionJobService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::create(
    const std::string& command_id, const std::string& source_id,
    std::size_t maximum_codepoints, std::size_t overlap_codepoints,
    int max_requests, int output_token_limit_per_request,
    const std::string& provider_connection_id) {
    if (maximum_codepoints < 500 || maximum_codepoints > 50000 || overlap_codepoints >= maximum_codepoints / 2
        || max_requests < 0 || output_token_limit_per_request < 1 || output_token_limit_per_request > 1000000)
        return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
            {xuyan::domain::ErrorCode::validation_failed, "分块大小或重叠范围无效", false, "使用 500—50000 码点且重叠小于一半"});
    SourceImportService sources(database_path_);
    auto text = sources.loadNormalizedText(source_id);
    if (!text.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*text.error);
    const auto total = xuyan::domain::utf8CodepointCount(*text.value);
    if (total == 0) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "空来源不能创建提取任务", false, "选择包含正文的来源"});
    // 正文只解码一次，先建立段尾/句末索引，再用码点到字节的映射截取每一步。
    const auto boundaries = textBoundaries(*text.value);
    // Normalize once: repeated codepointSlice() rescans the complete novel for every step.
    std::vector<std::uint32_t> byte_offsets;
    byte_offsets.reserve(total + 1);
    for (std::size_t byte = 0; byte < text.value->size();) {
        byte_offsets.push_back(static_cast<std::uint32_t>(byte));
        const auto lead = static_cast<unsigned char>((*text.value)[byte]);
        byte += lead < 0x80 ? 1U : lead < 0xe0 ? 2U : lead < 0xf0 ? 3U : 4U;
    }
    byte_offsets.push_back(static_cast<std::uint32_t>(text.value->size()));
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto source = repository.loadSource(source_id);
    if (!source.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*source.error);
    // 先把章节和章节之间的空隙拆成不交叉的区段，后续切片不会越过章节边界。
    std::vector<std::pair<std::size_t, std::size_t>> segments;
    std::size_t covered = 0;
    for (const auto& chapter : source.value->chapters) {
        if (chapter.start_codepoint > covered) segments.emplace_back(covered, chapter.start_codepoint);
        if (chapter.end_codepoint > chapter.start_codepoint)
            segments.emplace_back(chapter.start_codepoint, chapter.end_codepoint);
        covered = std::max(covered, chapter.end_codepoint);
    }
    if (covered < total) segments.emplace_back(covered, total);
    if (segments.empty()) segments.emplace_back(0, total);
    xuyan::domain::ExtractionJob job;
    job.id = "job-" + xuyan::domain::sha256(command_id).substr(0, 24); job.source_id = source_id;
    // 远程任务只绑定当时的连接指纹；创建任务本身不读取密钥，也不调用模型。
    if (!provider_connection_id.empty()) {
        auto connection = repository.loadProviderConnection(provider_connection_id);
        if (!connection.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*connection.error);
        if (connection.value->deleted || !connection.value->enabled || connection.value->default_model.empty())
            return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
                {xuyan::domain::ErrorCode::validation_failed, "所选模型连接未启用或缺少模型", false,
                 "先在模型连接页完成配置"});
        if (connection.value->kind != "local" && connection.value->data_policy != "remote_allowed")
            return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
                {xuyan::domain::ErrorCode::validation_failed, "当前数据策略不允许远程发送小说", false,
                 "明确允许远程发送后重新创建任务"});
        job.provider_connection_id = provider_connection_id;
        job.model_id = connection.value->default_model;
        job.provider_connection_fingerprint = xuyan::domain::providerConnectionFingerprint(*connection.value);
        // 协议在建任务时固定，并进入既有缓存参数摘要；旧候选不自动升级或重用。
        job.schema_version = typedCandidateSchemaVersion;
        job.prompt_version = typedCandidatePromptVersion;
    }
    int ordinal = 1;
    for (const auto& [segment_start, segment_end] : segments) {
      std::size_t start = segment_start;
      while (start < segment_end) {
        auto end = std::min(segment_end, start + maximum_codepoints);
        if (end < segment_end) {
            const auto minimum = start + maximum_codepoints / 2;
            // 优先在段尾切片；单行长段才退到句尾，过长句最终仍受最大码点数约束。
            const auto paragraph = lastBoundaryInRange(boundaries.paragraphs, minimum, end);
            const auto sentence = paragraph == 0
                ? lastBoundaryInRange(boundaries.sentences, minimum, end) : 0;
            if (paragraph != 0) end = paragraph;
            else if (sentence != 0) end = sentence;
        }
        const auto chunk = std::string_view(*text.value).substr(byte_offsets[start], byte_offsets[end] - byte_offsets[start]);
        xuyan::domain::ExtractionStep step;
        step.job_id = job.id; step.ordinal = ordinal;
        step.id = job.id + "-step-" + std::to_string(ordinal++);
        step.start_codepoint = start; step.end_codepoint = end; step.chunk_hash = xuyan::domain::sha256(chunk);
        job.budget.estimated_input_tokens += (end - start) * 3 / 2 + 1;
        job.steps.push_back(std::move(step));
        if (end == segment_end) break;
        start = std::max(start + 1, end - overlap_codepoints);
      }
    }
    job.budget.max_requests = max_requests > 0 ? max_requests
        : static_cast<int>(job.steps.size()) + std::max(1, static_cast<int>(job.steps.size() / 10));
    job.budget.output_token_limit_per_request = output_token_limit_per_request;
    job.budget.sample_steps = std::min(3, static_cast<int>(job.steps.size()));
    auto valid = xuyan::domain::validateExtractionJob(std::move(job));
    if (!valid.ok()) return valid;
    try { return repository.createExtractionJob(command_id, std::move(*valid.value)); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> ExtractionJobService::list() {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listExtractionJobs(); }
    catch (const std::exception& exception) { return xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> ExtractionJobService::listForWorld(
    const std::string& world_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listExtractionJobsForWorld(world_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::load(const std::string& job_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).loadExtractionJob(job_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::cancel(
    const std::string& command_id, const std::string& job_id, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).cancelExtractionJob(command_id, job_id, expected_revision); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<xuyan::domain::ExtractionStep> ExtractionJobService::claimNext(
    const std::string& command_id, const std::string& job_id, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).claimExtractionStep(command_id, job_id, expected_revision); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionStep>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::finishStep(
    const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
    const std::string& terminal_status, const std::string& output_json, const std::string& error_message) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).finishExtractionStep(
        command_id, job_id, ordinal, expected_attempt, terminal_status, output_json, error_message); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::retryStep(
    const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).retryExtractionStep(command_id, job_id, ordinal, expected_attempt); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}
xuyan::domain::Result<int> ExtractionJobService::recoverInterrupted() {
    try { return xuyan::storage::WorkspaceRepository(database_path_).recoverInterruptedExtractionSteps(); }
    catch (const std::exception& exception) { return xuyan::domain::Result<int>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<xuyan::domain::ExtractionQualityReport> ExtractionJobService::qualityReport(
    const std::string& job_id, int sample_limit) {
    if (sample_limit < 1 || sample_limit > 1000) return xuyan::domain::Result<xuyan::domain::ExtractionQualityReport>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "抽样数量必须为 1—1000", false, "调整抽样数量"});
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto candidates = repository.listExtractionCandidatesForJob(job_id, sample_limit);
        if (!candidates.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionQualityReport>::failure(*candidates.error);
        xuyan::domain::ExtractionQualityReport report;
        SourceImportService sources(database_path_);
        for (const auto& candidate : *candidates.value) {
            ++report.sampled_candidates;
            auto quote = sources.evidenceText(candidate.source_id, candidate.start_codepoint, candidate.end_codepoint);
            if (quote.ok() && *quote.value == candidate.quote
                && xuyan::domain::sha256(*quote.value) == candidate.quote_hash) ++report.evidence_valid;
            if (candidate.review_status == "accepted") ++report.accepted;
            else if (candidate.review_status == "rejected") ++report.rejected;
            else ++report.unresolved;
        }
        return xuyan::domain::Result<xuyan::domain::ExtractionQualityReport>::success(std::move(report));
    } catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionQualityReport>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
