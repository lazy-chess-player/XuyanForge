#include "xuyan/domain/extraction_job.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<ExtractionJob> validateExtractionJob(ExtractionJob job) {
    if (job.id.empty() || job.source_id.empty() || job.steps.empty() || job.steps.size() > 100000)
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "提取任务缺少 ID、来源或有效步骤", false, "重新创建任务"});
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
    if (job.budget.max_requests <= 0 || job.budget.max_requests > 1000000
        || job.budget.output_token_limit_per_request < 1 || job.budget.output_token_limit_per_request > 1000000
        || job.budget.sample_steps < 0 || job.budget.sample_steps > job.total_steps
        || (!job.budget.price_known && (job.budget.estimated_cost_microunits != 0 || !job.budget.currency.empty())))
        return Result<ExtractionJob>::failure(
            {ErrorCode::validation_failed, "提取调用、输出 token 或价格预算无效", false, "设置正数硬上限；价格未知时不要填写零费用"});
    return Result<ExtractionJob>::success(std::move(job));
}

} // namespace xuyan::domain
