#pragma once

#include "xuyan/domain/extraction_candidate.h"
#include "xuyan/domain/extraction_job.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace xuyan::application {

class CandidateService {
public:
    /** @brief 绑定候选、来源证据及审核状态所在的工作区数据库。 */
    explicit CandidateService(std::filesystem::path database_path);
    /** @brief 校验模型步骤 JSON、逐字引文和码点范围后原子提交候选。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> ingestStepOutput(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    /** @brief 执行同一份字段/原文校验和原子候选提交，仅返回检查点而非历史步骤列表。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> ingestStepOutputState(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    /** @brief 按审核状态列出候选；默认仅列待校对项。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> list(
        const std::string& review_status = "candidate");
    /** @brief 按世界、可选来源与审核状态返回有界候选页和匹配总数。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidatePage> listPage(
        const std::string& world_id, const std::string& source_id,
        const std::string& review_status, int limit, std::int64_t offset);
    /** @brief 提供当前世界已确认实体的精确名称/别名建议，分页返回稳定ID，不自动绑定或合并。 */
    xuyan::domain::Result<xuyan::domain::RelationEndpointMatchPage> matchRelationEndpoints(
        const std::string& world_id, const std::string& mention, int limit = 20, std::int64_t offset = 0);
    /** @brief 以乐观锁审核候选；保留实体逐字别名，事件原子创建带证据时间线，不推定日期或因果。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> review(
        const std::string& command_id, const std::string& candidate_id, int expected_revision,
        const std::string& review_status, const std::string& name,
        const std::string& fields_json, const std::string& provenance_type);
private:
    /** @brief 共用候选校验实现，结果类型决定提交后的快照范围，不放松证据条件。 */
    template<class JobResult> xuyan::domain::Result<JobResult> ingestStepOutputImpl(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
