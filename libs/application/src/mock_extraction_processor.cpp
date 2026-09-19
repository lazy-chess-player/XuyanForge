#include "xuyan/application/mock_extraction_processor.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/package/json.h"

#include <algorithm>
#include <array>
#include <vector>

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
