#include "xuyan/application/extraction_job_service.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>

namespace xuyan::application {
namespace {

std::vector<std::size_t> paragraphBoundaries(std::string_view utf8) {
    std::vector<std::size_t> result;
    std::size_t codepoint = 0;
    for (std::size_t offset = 0; offset < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[offset]);
        const auto width = lead < 0x80 ? 1U : lead < 0xe0 ? 2U : lead < 0xf0 ? 3U : 4U;
        if (utf8[offset] == '\n' && offset + 1 < utf8.size() && utf8[offset + 1] == '\n') result.push_back(codepoint + 2);
        offset += width; ++codepoint;
    }
    return result;
}

} // namespace

ExtractionJobService::ExtractionJobService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::ExtractionJob> ExtractionJobService::create(
    const std::string& command_id, const std::string& source_id,
    std::size_t maximum_codepoints, std::size_t overlap_codepoints,
    int max_requests, int output_token_limit_per_request) {
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
    const auto boundaries = paragraphBoundaries(*text.value);
    xuyan::domain::ExtractionJob job;
    job.id = "job-" + xuyan::domain::sha256(command_id).substr(0, 24); job.source_id = source_id;
    std::size_t start = 0; int ordinal = 1;
    while (start < total) {
        auto end = std::min(total, start + maximum_codepoints);
        if (end < total) {
            const auto minimum = start + maximum_codepoints / 2;
            const auto candidate = std::upper_bound(boundaries.begin(), boundaries.end(), end);
            if (candidate != boundaries.begin()) {
                const auto boundary = *std::prev(candidate);
                if (boundary >= minimum) end = boundary;
            }
        }
        auto chunk = xuyan::domain::codepointSlice(*text.value, start, end);
        if (!chunk.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*chunk.error);
        xuyan::domain::ExtractionStep step;
        step.job_id = job.id; step.ordinal = ordinal;
        step.id = job.id + "-step-" + std::to_string(ordinal++);
        step.start_codepoint = start; step.end_codepoint = end; step.chunk_hash = xuyan::domain::sha256(*chunk.value);
        job.budget.estimated_input_tokens += (end - start) * 3 / 2 + 1;
        job.steps.push_back(std::move(step));
        if (end == total) break;
        start = std::max(start + 1, end - overlap_codepoints);
    }
    job.budget.max_requests = max_requests > 0 ? max_requests
        : static_cast<int>(job.steps.size()) + std::max(1, static_cast<int>(job.steps.size() / 10));
    job.budget.output_token_limit_per_request = output_token_limit_per_request;
    job.budget.sample_steps = std::min(3, static_cast<int>(job.steps.size()));
    auto valid = xuyan::domain::validateExtractionJob(std::move(job));
    if (!valid.ok()) return valid;
    try { return xuyan::storage::WorkspaceRepository(database_path_).createExtractionJob(command_id, std::move(*valid.value)); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> ExtractionJobService::list() {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listExtractionJobs(); }
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
