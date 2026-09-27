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
    /** @brief 按审核状态列出候选；默认仅列待校对项。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> list(
        const std::string& review_status = "candidate");
    /** @brief 按世界、可选来源与审核状态返回有界候选页和匹配总数。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidatePage> listPage(
        const std::string& world_id, const std::string& source_id,
        const std::string& review_status, int limit, std::int64_t offset);
    /** @brief 以乐观锁保存人工接受、修改、拒绝或冲突标记。 */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> review(
        const std::string& command_id, const std::string& candidate_id, int expected_revision,
        const std::string& review_status, const std::string& name,
        const std::string& fields_json, const std::string& provenance_type);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
