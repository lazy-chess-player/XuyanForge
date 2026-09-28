#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <functional>
#include <stop_token>

namespace xuyan::application {

/** @brief 在已提交切片的检查点决定继续、暂停或永久取消剩余队列。 */
enum class OfflineBatchAction { proceed, pause, cancel };

/** @brief 区分全书完成、主动暂停、批次让出、取消和需要人工处理的停止原因。 */
enum class OfflineBatchStopReason { completed, paused, step_limit, cancelled, needs_attention };

/** @brief 只包含计数与修订的进度通知，不复制小说正文、候选输出或完整步骤列表。 */
struct OfflineBatchProgress {
    std::string job_id;
    int total_steps{0};
    int completed_steps{0};
    int processed_steps{0};
    int revision{0};
};

/** @brief 有界离线处理选项；通知在调用线程同步执行，回调必须快速返回且自行保护跨线程数据。 */
struct OfflineBatchOptions {
    int maximum_steps{10000};
    // 停止令牌只暂停调度，不删除已提交候选；重建处理器后仍可从检查点继续。
    std::stop_token stop_token;
    std::function<OfflineBatchAction(const OfflineBatchProgress&)> on_progress;
};

/** @brief 返回持久化任务、当前批次处理量与停止原因；暂停和让出不等于全书完成。 */
struct OfflineBatchResult {
    xuyan::domain::ExtractionJob job;
    int processed_steps{0};
    OfflineBatchStopReason reason{OfflineBatchStopReason::step_limit};
};

class MockExtractionProcessor {
public:
    /** @brief 绑定持久化队列以执行完全离线的规则提取，不创建固定世界或样例小说。 */
    explicit MockExtractionProcessor(std::filesystem::path database_path);
    /** @brief 在不访问网络的情况下处理一个待执行切片。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    /**
     * @brief 有界处理小说切片，每片原子提交后通知进度，在检查点暂停或取消。
     * @details 同进程内同一工作区/任务的批次不得重复启动；暂停后可重新调用继续。
     * 不启动线程或网络请求，后台调用方负责线程生命周期；停止令牌不强行中断当前事务。
     * 回调异常返回中文错误并保留已提交检查点，未知或失败步骤不自动重试。
     */
    xuyan::domain::Result<OfflineBatchResult> processBatch(
        const std::string& job_id, const OfflineBatchOptions& options = {});
    /** @brief 至多处理指定数量的离线步骤，并返回最新任务状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processAll(const std::string& job_id, int maximum_steps = 10000);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
