#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <functional>
#include <stop_token>

namespace xuyan::application {

/* 离线检查点的调度动作，由回调同步返回，不中断已提交事务。 */
enum class OfflineBatchAction {
    /* 校验任务后继续下一片。 */
    proceed,
    /* 暂停本批次，保留待执行队列。 */
    pause,
    /* 取消剩余步骤，保留已提交候选及证据。 */
    cancel
};

/* 离线批次内部停止原因，由界面映射中文，不把暂停/让出当作全书完成。 */
enum class OfflineBatchStopReason {
    /* 全任务已完成。 */
    completed,
    /* 回调或停止令牌暂停。 */
    paused,
    /* 达到本次有界片数，剩余保留。 */
    step_limit,
    /* 持久化任务取消。 */
    cancelled,
    /* 失败/未知步骤需要人工关注。 */
    needs_attention
};

/* 轻量离线进度值；调用线程同步借给回调，不携带正文/模型输出，跨线程使用须复制。 */
struct OfflineBatchProgress {
    /* 当前任务稳定标识；默认空，由处理器填写，仅回调期间借用。 */
    std::string job_id;
    /* 任务步骤总数，单位片，初始 0，由持久化任务读取。 */
    int total_steps{0};
    /* 全任务累计成功完成片数，初始 0，不含失败/未知片。 */
    int completed_steps{0};
    /* 本次批次已结算片数，初始 0，不等于全任务累计完成数，由循环更新。 */
    int processed_steps{0};
    /* 任务持久化修订，初始 0，回调后需重读任务确认未过期。 */
    int revision{0};
};

/* 离线批次选项值；调用方持有至返回，不被处理器长期保存，不拥有执行线程。 */
struct OfflineBatchOptions {
    /* 本次最多结算片数，默认 10000，限 1—100000；达到后保留队列。 */
    int maximum_steps{10000};
    /* 默认无停止源；只暂停下一片调度，不强制中断原文读取/事务，不删除已提交候选。 */
    std::stop_token stop_token;
    /* 默认空；初始与结算后同步回调，借用进度至返回。异常转错误，跨线程通知自行复制并同步。 */
    std::function<OfflineBatchAction(const OfflineBatchProgress&)> on_progress;
};

/* 离线停止结果值；拥有完整任务快照，供调用方显式继续/处理，不借用仓储。 */
struct OfflineBatchResult {
    /* 停止时任务值；处理器填入，包含已提交状态与步骤，随结果持有。 */
    xuyan::domain::ExtractionJob job;
    int processed_steps{0};
    /* 默认批次让出；成功返回时明确填入实际原因，界面映射中文。 */
    OfflineBatchStopReason reason{OfflineBatchStopReason::step_limit};
};

/* 保留既有名称/API的离线规则提取器；只拥有路径，从用户来源提取动作句，不内置样例或请求网络。 */
class MockExtractionProcessor {
public:
    /*
     * 功能：绑定离线提取工作区。参数：database_path 为按值保存的数据库路径。
     * 返回：初始化完成。失败：路径分配异常传播，构造不校验任务。
     * 副作用：无读写/网络；线程：同步构造，不创建执行线程。
     */
    explicit MockExtractionProcessor(std::filesystem::path database_path);
    /*
     * 功能：按既有离线协议提取一片最多五个动作句，仅产生待审候选。
     * 参数：job_id 为离线任务标识；已完成/取消任务直接返回现状。
     * 返回：提交后的完整任务。失败：类型化模型任务拒绝、领取/原文/校验/提交错误返回 Result，未捕获异常可传播。
     * 副作用：领取步骤、读取原文并原子提交候选，失败尝试结算步骤；不读凭据、不联网。
     * 线程：调用线程同步完成；单步不持有批次租约，调用方避免同时执行同任务，仓储修订仍防止重复领取。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    /*
     * 功能：串行有界执行离线规则提取，在初始及已提交检查点通知进度。
     * 参数：job_id 为任务；options 默认处理最多 10000 片，含停止令牌与同步回调，借用至返回。
     * 返回：完整任务、本次结算数及停止原因。失败：上限不在 1—100000、重复批次、回调/存储错误返回 Result，保留已提交结果。
     * 副作用：逐片写候选及步骤，回调取消持久化剩余队列，暂停不删除结果；失败/未知不自动重试。
     * 线程与生命周期：调用线程执行和回调，不启动线程；进程内批次租约至返回释放，停止令牌不强制打断当前事务。
     * 调用方负责 options 存活、跨线程回调同步及退出等待。
     */
    xuyan::domain::Result<OfflineBatchResult> processBatch(
        const std::string& job_id, const OfflineBatchOptions& options = {});
    /*
     * 功能：兼容接口，通过有界批次执行离线任务并返回快照。
     * 参数：job_id 为任务；maximum_steps 默认 10000，合法 1—100000，非全书无限执行。
     * 返回：停止时完整任务；需读任务状态区分完成与达到上限。失败：processBatch 错误原样返回。
     * 副作用：与离线批次相同，不联网；线程：调用线程同步完成，局部选项只在本次调用存活。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processAll(const std::string& job_id, int maximum_steps = 10000);
private:
    /* 工作区数据库路径；构造后只读，与处理器同寿命，不拥有线程或常驻数据库连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
