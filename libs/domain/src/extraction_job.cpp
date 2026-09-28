#include "xuyan/domain/extraction_job.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<ExtractionInputConfig> validateExtractionInputConfig(ExtractionInputConfig config) {
    const bool raw = config.mode == "raw" && config.density == "none" && config.algorithm_version == "source-v1";
    const bool backbone = config.mode == "backbone" && config.algorithm_version == "backbone-v1"
        && (config.density == "conservative" || config.density == "balanced" || config.density == "compact");
    if (!raw && !backbone) return Result<ExtractionInputConfig>::failure(
        {ErrorCode::validation_failed, "解析输入模式、密度或算法版本无效", false, "重新选择输入模式并创建任务"});
    return Result<ExtractionInputConfig>::success(std::move(config));
}

Result<ExtractionJob> validateExtractionJob(ExtractionJob job) {
    const auto input = validateExtractionInputConfig(job.input);
    if (!input.ok()) return Result<ExtractionJob>::failure(*input.error);
    if (job.input.mode != "raw" && job.provider_connection_id.empty())
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "主干模型输入需要明确绑定模型连接", false, "离线任务保留原文模式"});
    if (job.id.empty() || job.source_id.empty() || job.steps.empty() || job.steps.size() > 100000)
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "提取任务缺少 ID、来源或有效步骤", false, "重新创建任务"});
    if ((job.provider_connection_id.empty() && (!job.model_id.empty() || !job.provider_connection_fingerprint.empty()))
        || (!job.provider_connection_id.empty()
            && (job.model_id.empty() || job.provider_connection_fingerprint.size() != 64)))
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "提取任务的模型连接快照无效", false, "重新选择模型连接并创建任务"});
    std::size_t previous_start = 0;
    for (std::size_t index = 0; index < job.steps.size(); ++index) {
        auto& step = job.steps[index];
        if (step.id.empty() || step.job_id != job.id || step.ordinal != static_cast<int>(index + 1)
            || step.start_codepoint >= step.end_codepoint || (index > 0 && step.start_codepoint <= previous_start)
            || step.chunk_hash.size() != 64)
            return Result<ExtractionJob>::failure(
                {ErrorCode::validation_failed, "提取步骤范围、顺序或摘要无效", false, "重新分块"});
        previous_start = step.start_codepoint;
    }
    job.total_steps = static_cast<int>(job.steps.size());
    job.has_ready_step = std::any_of(job.steps.begin(), job.steps.end(), [](const auto& step) { return step.status == "ready"; });
    job.has_running_step = std::any_of(job.steps.begin(), job.steps.end(), [](const auto& step) { return step.status == "running"; });
    job.requires_attention = job.has_running_step || job.status == "needs_attention"
        || std::any_of(job.steps.begin(), job.steps.end(), [](const auto& step) {
            return step.status == "failed" || step.status == "unknown";
        });
    if (job.budget.max_requests <= 0 || job.budget.max_requests > 1000000
        || job.budget.output_token_limit_per_request < 1 || job.budget.output_token_limit_per_request > 1000000
        || job.budget.sample_steps < 0 || job.budget.sample_steps > job.total_steps
        || (!job.budget.price_known && (job.budget.estimated_cost_microunits != 0 || !job.budget.currency.empty())))
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "提取调用、输出 token 或价格预算无效", false, "设置正数硬上限；价格未知时不要填写零费用"});
    return Result<ExtractionJob>::success(std::move(job));
}

} // namespace xuyan::domain
