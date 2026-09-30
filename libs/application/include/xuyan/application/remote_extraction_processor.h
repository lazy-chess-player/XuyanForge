#pragma once

#include "xuyan/application/provider_generation_service.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <functional>
#include <stop_token>

namespace xuyan::application {

/* 同步检查点回调的调度决定；仅控制之后的步骤，不能撤回在途请求。 */
enum class RemoteBatchAction {
    /* 允许在任务状态和预算再次校验后执行下一片。 */
    proceed,
    /* 暂停当前批次，保留待执行队列，可显式继续。 */
    pause,
    /* 持久化取消剩余队列，保留已提交结果。 */
    cancel
};
/* 远程批次终止原因；作为内部状态，由显示层映射中文。 */
enum class RemoteBatchStopReason {
    /* 全部步骤已提交完成。 */
    completed,
    /* 回调或停止令牌要求暂停。 */
    paused,
    /* 本次处理达到显式步骤上限，剩余队列保留。 */
    step_limit,
    /* 任务已取消或本次请求取消。 */
    cancelled,
    /* 存在失败/未知/未结算状态，需要人工处理。 */
    needs_attention,
    /* 请求预算达到持久化硬上限，不能继续发送。 */
    budget_exhausted
};

/* 检查点通知值；仅含身份、计数及修订，调用线程构造并同步借给回调，不携带正文/秘密。 */
struct RemoteBatchProgress {
    /* 工作区内任务标识，默认空；处理器填入，回调借用期间有效，需跨线程时自行复制。 */
    std::string job_id;
    /* 任务持久化步骤总数，单位片，初始 0，由检查点读取。 */
    int total_steps{0};
    /* 全任务已完成片数，初始 0，不含失败/未知片，由仓储检查点读取。 */
    int completed_steps{0};
    /* 本次批次已结算片数，初始 0，失败/未知的已结算片亦计数；不是任务累计完成量。 */
    int processed_steps{0};
    /* 当前持久化任务修订，初始 0，由检查点填写；回调操作后须重新读取，不能沿用旧修订。 */
    int revision{0};
    /* 任务累计已消耗请求数，初始 0；领取时计入，失败/未知不自动退还。 */
    int consumed_requests{0};
    /* 任务持久化请求硬上限，初始 0；处理器只读取，不因批次继续而扩大。 */
    int maximum_requests{0};
};

/* 显式远程批次选项值；调用方持有至 processBatch 返回，处理器不保存引用或创建线程。 */
struct RemoteBatchOptions {
    /* 本次步骤上限，单位片；默认 0 无效，调用者必须显式填写 1—100000。 */
    int maximum_steps{0};
    /* 默认无关联停止源；停止请求只阻止下一片调度，不能终止已发调用或撤销费用。 */
    std::stop_token stop_token;
    /* 默认空；初始/提交后在调用线程同步通知。参数仅在回调期间有效，异常转错误，不能递归执行同任务。 */
    std::function<RemoteBatchAction(const RemoteBatchProgress&)> on_progress;
};

/* 停止结果值，拥有完整任务快照；返回前复核修订，暂停/预算耗尽不代表全书完成。 */
struct RemoteBatchResult {
    /* 停止时完整持久化任务值，由处理器填入，不借用连接或此前进度对象。 */
    xuyan::domain::ExtractionJob job;
    int processed_steps{0};
    /* 停止原因，默认需人工关注；每个成功返回路径明确填写，界面映射中文。 */
    RemoteBatchStopReason reason{RemoteBatchStopReason::needs_attention};
};

/* 有界远程提取协调器；拥有路径、借用端口，同进程同任务租约互斥。同步调用，不拥有执行线程。 */
class RemoteExtractionProcessor {
public:
    /*
     * 功能：绑定远程提取依赖。参数：database_path 为工作区路径；credentials、transport 为非拥有端口，须长于服务及调用。
     * 返回：完成初始化。失败：路径分配异常传播，构造不读取秘密/原文。
     * 副作用：无网络/数据库访问；线程：同步，调用方遵循传输及凭据端口线程约束。
     */
    RemoteExtractionProcessor(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);
    /*
     * 功能：显式执行下一片当前类型化提取协议，先复核冻结配置、原文哈希及保留映射。
     * 参数：job_id 为已获发送授权的任务标识，不能把单步授权扩大为全书授权。
     * 返回：完整任务快照；请求失败可能作为任务中的 failed/unknown 状态成功返回，需检查状态。
     * 失败：任务租约/协议/连接/证据/预算或提交错误返回 Result，异常隐藏详情。
     * 副作用：领取消耗预算、最多发送一次请求，合法候选及淘汰统计由仓储原子提交；失败不自动重发/退款。
     * 线程与生命周期：调用线程同步等待，进程内租约至返回释放，不支持在途强制取消，端口须持续有效。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    /*
     * 功能：按冻结输入模式串行处理显式限定的远程片段，逐片原子提交。
     * 参数：job_id 为已获批次发送授权的任务；options 为步骤上限、停止令牌及可选检查点回调，借用至返回。
     * 返回：任务快照、本次结算数及停止原因；暂停、步骤上限、预算耗尽保留队列。
     * 失败：上限不在 1—100000、租约冲突、过期协议、回调异常、修订或存储错误返回 Result，保留已提交片段。
     * 副作用：显式发送且可能计费，取消持久化剩余步骤，不自动重试未知/失败请求，不扩大预算。
     * 线程与生命周期：不创建线程；回调在调用线程同步执行，回调后重读取消状态；停止令牌不终止在途调用。
     * 调用方负责端口及 options 生命周期，退出时等待本次调用结束，跨线程通知须复制进度值。
     */
    xuyan::domain::Result<RemoteBatchResult> processBatch(
        const std::string& job_id, const RemoteBatchOptions& options);
private:
    /*
     * 功能：已持有进程内执行租约时共用单步校验/发送/提交。参数：job_id 为任务；模板 JobResult 为完整任务或轻量检查点。
     * 返回：相应持久化结果。失败：前置校验返回错误；领取后异常尝试结算 unknown，结算错误仍传播 Result。
     * 副作用：消耗领取预算、同步发送、原子提交候选或结算失败；线程：仅租约持有者同步调用，不持锁/写事务等待网络。
     */
    template<class JobResult>
    xuyan::domain::Result<JobResult> processNextUnchecked(const std::string& job_id);
    /* 工作区路径，构造后只读，参与规范化租约键，与处理器同寿命，不持有常驻连接。 */
    std::filesystem::path database_path_;
    /* 非拥有凭据端口，仅网关发送前读取，调用方保证线程约束及长于所有执行调用。 */
    ICredentialStore& credentials_;
    /* 非拥有传输端口，同步生成时使用；调用方负责取消/退出协调及端口存活，不交给租约拥有。 */
    IProviderTransport& transport_;
};

} // namespace xuyan::application
