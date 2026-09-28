#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

/** @brief 冻结单片模型输入语义；原文是默认值，主干筛选必须显式指定密度和算法版本。 */
struct ExtractionInputConfig {
    std::string mode{"raw"};
    std::string density{"none"};
    std::string algorithm_version{"source-v1"};
};

/** @brief 拒绝未知或互相矛盾的输入参数，避免恢复任务时静默采用新算法。 */
Result<ExtractionInputConfig> validateExtractionInputConfig(ExtractionInputConfig config);

struct ExtractionStep {
    std::string id;
    std::string job_id;
    int ordinal{0};
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
    std::string chunk_hash;
    std::string status{"ready"};
    int attempt{0};
    std::string output_json;
    std::string error_message;
};

struct ExtractionBudget {
    std::size_t estimated_input_tokens{0};
    int output_token_limit_per_request{1200};
    int max_requests{0};
    int consumed_requests{0};
    int sample_steps{0};
    bool price_known{false};
    std::int64_t estimated_cost_microunits{0};
    std::string currency;
};

struct ExtractionQualityReport {
    int sampled_candidates{0};
    int evidence_valid{0};
    int accepted{0};
    int rejected{0};
    int unresolved{0};
    bool model_quality_verified{false};
};

/** @brief 无步骤正文的任务检查点；供调度读取计数、连接快照和停止条件。 */
struct ExtractionJobState {
    std::string id;
    std::string source_id;
    std::string status{"queued"};
    std::string schema_version{"candidate-v1"};
    std::string prompt_version{"extract-v1"};
    std::string provider_connection_id;
    std::string model_id;
    std::string provider_connection_fingerprint;
    int total_steps{0};
    int completed_steps{0};
    bool cancel_requested{false};
    int revision{0};
    ExtractionBudget budget;
    ExtractionInputConfig input;
    bool has_ready_step{false};
    bool has_running_step{false};
    bool requires_attention{false};
};

/** @brief 供编辑和完整结果查询使用的任务快照，显式包含全部步骤及历史输出。 */
struct ExtractionJob : ExtractionJobState {
    std::vector<ExtractionStep> steps;
};

/** @brief 校验解析任务状态、步骤与预算的一致性。 */
Result<ExtractionJob> validateExtractionJob(ExtractionJob job);

} // namespace xuyan::domain
