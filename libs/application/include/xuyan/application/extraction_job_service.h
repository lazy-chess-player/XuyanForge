#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

class ExtractionJobService {
public:
    /** @brief 绑定存放提取任务、预算和候选资料的工作区数据库。 */
    explicit ExtractionJobService(std::filesystem::path database_path);
    /** @brief 按已校正章节切片并创建持久化任务；绑定远程连接时只保存配置快照，不发起请求。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> create(
        const std::string& command_id, const std::string& source_id,
        std::size_t maximum_codepoints = 6000, std::size_t overlap_codepoints = 200,
        int max_requests = 0, int output_token_limit_per_request = 1200,
        const std::string& provider_connection_id = {});
    /** @brief 列出工作区内所有可恢复的提取任务及其当前进度。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> list();
    /** @brief 仅列出指定世界的可恢复解析任务，避免加载无关世界的步骤。 */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listForWorld(const std::string& world_id);
    /** @brief 读取指定任务的步骤、预算、提供商快照和状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> load(const std::string& job_id);
    /** @brief 按期望修订请求取消任务，并持久化未执行步骤的取消状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancel(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 原子领取一个待执行步骤并消耗相应请求预算；失败不返回步骤。 */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimNext(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /** @brief 按步骤尝试次数提交完成、失败或未知状态，拒绝过期回报。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishStep(
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
