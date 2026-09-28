#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

class ExtractionJobService {
public:
    /** @brief 绑定存放提取任务、预算和候选资料的工作区数据库。 */
    explicit ExtractionJobService(std::filesystem::path database_path);
    /** @brief 按章节切片并冻结连接/输入参数；默认原文，显式主干仍不发送，token估算保留原文粗略上界。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> create(
        const std::string& command_id, const std::string& source_id,
        std::size_t maximum_codepoints = 6000, std::size_t overlap_codepoints = 200,
        int max_requests = 0, int output_token_limit_per_request = 1200,
        const std::string& provider_connection_id = {},
        const xuyan::domain::ExtractionInputConfig& input = {},
        const xuyan::domain::ProviderGenerationConfig& generation = {});
    /** @brief 列出工作区内所有可恢复的提取任务及其当前进度。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> list();
    /** @brief 仅列出指定世界的可恢复解析任务，避免加载无关世界的步骤。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listForWorld(const std::string& world_id);
    /** @brief 读取指定任务的步骤、预算、提供商快照和状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> load(const std::string& job_id);
    /** @brief 读取不含步骤列表/历史正文的持久化检查点和停止标志。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> loadState(const std::string& job_id);
    /** @brief 只查询下一片待执行步骤的定位元数据；没有待执行片时返回空值。 */
    xuyan::domain::Result<std::optional<xuyan::domain::ExtractionStep>> nextStep(const std::string& job_id);
    /** @brief 按期望修订请求取消任务，并持久化未执行步骤的取消状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancel(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 取消任务并返回同事务轻量检查点，不载入全部历史输出。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> cancelState(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 原子领取一个待执行步骤并消耗相应请求预算；失败不返回步骤。 */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimNext(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 按步骤尝试次数提交完成、失败或未知状态，拒绝过期回报。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /** @brief 结算步骤并仅返回检查点，幂等与预算语义和完整结果接口相同。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> finishStepState(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /** @brief 显式重试失败或未知的步骤；不会自动重发可能已计费的请求。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    /** @brief 启动时把中断的运行中步骤转为需人工关注的未知状态。 */
    xuyan::domain::Result<int> recoverInterrupted();
    /** @brief 抽样复核候选引文的原文位置，并汇总审核状态而不冒称模型质量。 */
    xuyan::domain::Result<xuyan::domain::ExtractionQualityReport> qualityReport(
        const std::string& job_id, int sample_limit = 100);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
