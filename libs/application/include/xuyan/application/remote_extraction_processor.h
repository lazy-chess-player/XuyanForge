#pragma once

#include "xuyan/application/provider_generation_service.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <functional>
#include <stop_token>

namespace xuyan::application {

enum class RemoteBatchAction { proceed, pause, cancel };
enum class RemoteBatchStopReason { completed, paused, step_limit, cancelled, needs_attention, budget_exhausted };

/** @brief 仅含持久化计数的检查点通知，不携带原文、模型输出或凭据。 */
struct RemoteBatchProgress {
    std::string job_id;
    int total_steps{0};
    int completed_steps{0};
    int processed_steps{0};
    int revision{0};
    int consumed_requests{0};
    int maximum_requests{0};
};

/** @brief 显式限定本次串行调用；停止令牌只阻止下一次调度，不中断已发出的请求。 */
struct RemoteBatchOptions {
    int maximum_steps{0}; // 调用者必须明确填写 1—100000，不能隐式发送整本小说。
    std::stop_token stop_token;
    std::function<RemoteBatchAction(const RemoteBatchProgress&)> on_progress;
};

/** @brief 返回停止时的持久化快照及本次已结算步骤数；失败或未知步骤也计入处理数。 */
struct RemoteBatchResult {
    xuyan::domain::ExtractionJob job;
    int processed_steps{0};
    RemoteBatchStopReason reason{RemoteBatchStopReason::needs_attention};
};

class RemoteExtractionProcessor {
public:
    /** @brief 绑定工作区、凭据存储和传输端口；构造过程不发送任何请求。 */
    RemoteExtractionProcessor(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);
    /**
     * @brief 仅显式执行下一步类型化远程抽样，并在发送前复核协议、连接快照和原文证据。
     * @details 旧远程任务不自动改版本或重发；整份字段与证据通过后才原子写入待审候选。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    /**
     * @brief 显式按冻结的原文或主干模式执行串行有界批次，每个提交后的检查点通知进度。
     * @details 不自动重试失败/未知步骤，不增加任务的持久化请求预算。同进程同任务
     * 单步和批次互斥；回调同步运行且不得抛出异常，外部取消在下次调度前重新读取。
     */
    xuyan::domain::Result<RemoteBatchResult> processBatch(
        const std::string& job_id, const RemoteBatchOptions& options);
private:
    /** @brief 持有执行租约时共用单步流程；单步返回事务内完整任务，批次仅返回同事务检查点。 */
    template<class JobResult>
    xuyan::domain::Result<JobResult> processNextUnchecked(const std::string& job_id);
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
