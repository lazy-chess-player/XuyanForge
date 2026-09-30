#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/provider_connection.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {


/*
 * 职责：冻结原文或主干派生输入的模式、密度和算法身份。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionInputConfig {
    // 模型输入内部模式，原文或显式主干视图，不能隐式切换。默认"raw"。
    std::string mode{"raw"};
    // 主干保留密度内部值，和算法版本一起冻结。默认"none"。
    std::string density{"none"};
    // 派生模型输入的算法版本，参与缓存身份。默认"source-v1"。
    std::string algorithm_version{"source-v1"};
};

/*
 * 功能：拒绝未知或互相矛盾的输入参数，避免恢复任务时静默采用新算法。
 * 参数：
 *   config：待校验的ExtractionInputConfig值，按值持有，不修改调用者原对象。只接受原文raw/none/source-v1或主干backbone及三档密度/backbone-v1的完整组合；不生成正文或选择模型。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ExtractionInputConfig> validateExtractionInputConfig(ExtractionInputConfig config);

/*
 * 职责：一个原文片段的持久化解析检查点、尝试及输出。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionStep {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 产生候选或步骤的解析任务稳定标识。默认空串。
    std::string job_id;
    // 本任务中从1开始的步骤顺序号，与步骤表/候选关联一致；默认0表示未规划。
    int ordinal{0};
    // 原文Unicode码点的零基起点，半开范围包含此位置。默认0。
    std::size_t start_codepoint{0};
    // 原文Unicode码点的零基终点，半开范围不包含此位置。默认0。
    std::size_t end_codepoint{0};
    // 步骤原文片段的内容摘要，用于缓存与恢复核对。默认空串。
    std::string chunk_hash;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"ready"。
    std::string status{"ready"};
    // 步骤被认领的尝试次数，迟到结果须核对此值。默认0。
    int attempt{0};
    // 已校验的步骤输出JSON正文，仅作为数据持久化。默认空串。
    std::string output_json;
    // 该步骤或回合的中文失败说明，不存密钥或完整模型原始响应。默认空串。
    std::string error_message;
};

/*
 * 职责：解析任务的请求/词元硬上限与消费或估计统计。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionBudget {
    // 输入词元估计总量，不是实际账单或原文字数。默认0。
    std::size_t estimated_input_tokens{0};
    // 每次请求的最大输出词元数。默认1200。
    int output_token_limit_per_request{1200};
    // 任务允许的请求次数硬上限。默认0。
    int max_requests{0};
    // 任务已消耗的请求次数，未知结果也不能擅自返还。默认0。
    int consumed_requests{0};
    // 抽样阶段允许处理的步骤数。默认0。
    int sample_steps{0};
    // 是否有可用价格来源；false时不能把估计成本展示成可靠价格。默认false。
    bool price_known{false};
    // 估计费用整数，以百万分之一货币单位表示，不等同账单。默认0。
    std::int64_t estimated_cost_microunits{0};
    // 费用使用的货币内部代码，价格未知时可为空。默认空串。
    std::string currency;
};

/*
 * 职责：抽样和人工审核统计；证据有效不能替代模型语义质量。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionQualityReport {
    // 已统计的抽样候选数量。默认0。
    int sampled_candidates{0};
    // 逐字证据定位通过的候选数量，不证明语义正确。默认0。
    int evidence_valid{0};
    // 人工接受的候选数量。默认0。
    int accepted{0};
    // 人工拒绝的候选数量。默认0。
    int rejected{0};
    // 尚未得出人工结论的候选数量。默认0。
    int unresolved{0};
    // 是否完成相应真实质量验证，不能因Schema通过而置真。默认false。
    bool model_quality_verified{false};
};


/*
 * 职责：无历史步骤正文的任务轻量检查点，用于有界批次调度。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionJobState {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 小说来源的稳定标识，用于原文回查。默认空串。
    std::string source_id;
    // 持久化生命周期的内部状态值，界面必须另映射中文。默认"queued"。
    std::string status{"queued"};
    // 冻结的候选结构协议版本，不在任务恢复时自动升级。默认"candidate-v1"。
    std::string schema_version{"candidate-v1"};
    // 冻结的提示词版本，参与缓存和模型请求身份。默认"extract-v1"。
    std::string prompt_version{"extract-v1"};
    // 冻结或绑定的模型连接稳定标识。默认空串。
    std::string provider_connection_id;
    // 提供商的真实模型标识，传输时保持原值。默认空串。
    std::string model_id;
    // 不含凭据的连接配置摘要，用于执行前核对。默认空串。
    std::string provider_connection_fingerprint;
    // 任务规划的步骤总数。默认0。
    int total_steps{0};
    // 任务已成功结算的步骤数。默认0。
    int completed_steps{0};
    // 用户已请求取消的标志，调度在检查点停止后续步骤。默认false。
    bool cancel_requested{false};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 本任务冻结的额度配置与当前消费统计。按对应值对象默认构造初始化。
    ExtractionBudget budget;
    // 本任务冻结的原文/主干输入语义。按对应值对象默认构造初始化。
    ExtractionInputConfig input;
    // 本任务冻结的模型生成参数，不包含凭据。按对应值对象默认构造初始化。
    ProviderGenerationConfig generation;
    // 查询快照内是否存在可认领步骤。默认false。
    bool has_ready_step{false};
    // 查询快照内是否有在途步骤。默认false。
    bool has_running_step{false};
    // 是否存在失败/未知等需要人工介入的步骤。默认false。
    bool requires_attention{false};
};


/*
 * 职责：编辑或显式完整查询使用的任务及全部历史步骤快照。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionJob : ExtractionJobState {
    // 完整任务中的全部步骤及历史输出；批次调度应使用轻量检查点。默认空集合，不预填资料。
    std::vector<ExtractionStep> steps;
};

/*
 * 功能：校验解析任务状态、步骤与预算的一致性。
 * 参数：
 *   job：待校验的ExtractionJob值，按值持有，不修改调用者原对象。校验输入/生成组合、模型连接快照、1—100000个顺序步骤及范围摘要；次数及单请求输出上限1—1000000；抽样步数不超过总数；价格未知不能附带费用；重算轻量总数和可执行/需处理标志。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ExtractionJob> validateExtractionJob(ExtractionJob job);

} // namespace xuyan::domain
