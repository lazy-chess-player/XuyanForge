#include "xuyan/application/mock_extraction_processor.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/package/json.h"

#include <array>

namespace xuyan::application {
namespace {

std::string commandId(std::string_view prefix, const std::string& job_id, int revision) {
    return std::string(prefix) + '-' + xuyan::domain::sha256(job_id + '|' + std::to_string(revision)).substr(0, 24);
}

} // namespace

MockExtractionProcessor::MockExtractionProcessor(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::ExtractionJob> MockExtractionProcessor::processNext(const std::string& job_id) {
    ExtractionJobService jobs(database_path_);
    auto job = jobs.load(job_id);
    if (!job.ok()) return job;
    if (job.value->status == "completed" || job.value->status == "cancelled") return job;
    auto claimed = jobs.claimNext(commandId("mock-claim", job_id, job.value->revision), job_id, job.value->revision);
    if (!claimed.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*claimed.error);
    SourceImportService sources(database_path_);
    auto full_text = sources.loadNormalizedText(job.value->source_id);
    if (!full_text.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*full_text.error);
    auto chunk = xuyan::domain::codepointSlice(*full_text.value, claimed.value->start_codepoint, claimed.value->end_codepoint);
    if (!chunk.ok()) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(*chunk.error);

    struct Pattern { std::string_view text; std::string_view kind; };
    constexpr std::array patterns{Pattern{"许澄", "character"}, Pattern{"沈棠", "character"},
                                  Pattern{"林舟", "character"}, Pattern{"议和印章", "item"}, Pattern{"灰港", "location"}};
    xuyan::package::JsonValue::Array candidates;
    for (const auto& pattern : patterns) {
        std::size_t search = 0;
        while ((search = chunk.value->find(pattern.text, search)) != std::string::npos) {
            const auto local_start = xuyan::domain::utf8CodepointCount(std::string_view(*chunk.value).substr(0, search));
            const auto length = xuyan::domain::utf8CodepointCount(pattern.text);
            candidates.emplace_back(xuyan::package::JsonValue::Object{
                {"type", "entity"}, {"name", std::string(pattern.text)},
                {"fields", xuyan::package::JsonValue::Object{{"kind", std::string(pattern.kind)}}},
                {"start_codepoint", static_cast<std::int64_t>(claimed.value->start_codepoint + local_start)},
                {"end_codepoint", static_cast<std::int64_t>(claimed.value->start_codepoint + local_start + length)},
                {"quote", std::string(pattern.text)}, {"provenance_type", "original_fact"}});
            search += pattern.text.size();
        }
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

xuyan::domain::Result<xuyan::domain::ExtractionJob> MockExtractionProcessor::processAll(
    const std::string& job_id, int maximum_steps) {
    if (maximum_steps < 1 || maximum_steps > 100000) return xuyan::domain::Result<xuyan::domain::ExtractionJob>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "Mock 执行步骤上限无效", false, "使用 1—100000"});
    ExtractionJobService jobs(database_path_);
    auto current = jobs.load(job_id);
    for (int step = 0; current.ok() && step < maximum_steps
         && current.value->status != "completed" && current.value->status != "cancelled"
         && current.value->status != "needs_attention"; ++step) {
        current = processNext(job_id);
    }
    return current;
}

} // namespace xuyan::application
