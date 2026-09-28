#pragma once

#include "xuyan/domain/extraction_job.h"
#include "xuyan/application/mock_extraction_processor.h"

#include <QObject>
#include <QVariantList>
#include <QThreadPool>
#include <QHash>
#include <QSet>

#include <filesystem>
#include <cstdint>
#include <memory>

class ExtractionJobViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString activeJobId READ activeJobId NOTIFY changed)
    Q_PROPERTY(bool stopping READ stopping NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:
    /** @brief 可替换的同步批次端口；正式程序使用真实处理器，测试可注入不联网的执行器。 */
    using BatchRunner = std::function<xuyan::domain::Result<xuyan::application::OfflineBatchResult>(
        const std::filesystem::path&, const std::string&, const xuyan::application::OfflineBatchOptions&)>;
    /** @brief 初始化工作区任务服务并恢复被中断的步骤状态。 */
    explicit ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent = nullptr,
                                    BatchRunner runner = {});
    /** @brief 退出时阻止新切片调度，并等待自有工作线程保存当前检查点。 */
    ~ExtractionJobViewModel() override;
    /** @brief 返回当前世界的解析任务列表。 */
    QVariantList jobs() const { return jobs_; }
    /** @brief 返回任务服务是否正在执行操作。 */
    bool busy() const noexcept { return busy_; }
    /** @brief 返回是否有全书批次或单步抽样在后台运行。 */
    bool running() const noexcept { return running_; }
    /** @brief 返回运行中的任务编号；切换世界不改变正在执行的任务。 */
    QString activeJobId() const { return active_job_id_; }
    /** @brief 返回是否已请求暂停或取消、正在等待当前片段结算。 */
    bool stopping() const noexcept;
    /** @brief 返回最近一次任务操作的中文错误。 */
    QString errorText() const { return error_text_; }
    /** @brief 返回最近一次任务操作的状态说明。 */
    QString statusText() const { return status_text_; }
    /** @brief 切换当前世界并隐藏其他世界的解析任务。 */
    void setWorldId(QString world_id);
    /** @brief 后台重新读取当前世界的任务及来源归属。 */
    Q_INVOKABLE void refresh();
    /** @brief 按章节切片参数、预算和可选模型连接建立持久化任务。 */
    Q_INVOKABLE void createJob(QString source_id, int chunk_size, int overlap, int max_requests,
                              int output_token_limit, QString provider_connection_id);
    /** @brief 按预期修订请求取消任务。 */
    Q_INVOKABLE void cancelJob(QString job_id, int revision);
    /** @brief 将失败或未知的指定步骤重新排队。 */
    Q_INVOKABLE void retryStep(QString job_id, int ordinal, int attempt);
    /** @brief 在后台使用本地离线规则处理任务的所有待执行步骤。 */
    Q_INVOKABLE void runMock(QString job_id);
    /** @brief 在用户明确确认后向所选模型发送至多一个小说片段。 */
    Q_INVOKABLE void runRemoteSample(QString job_id);
    /** @brief 仅在用户点击后按任务的冻结连接启动整书解析；不重跑已完成片段。 */
    Q_INVOKABLE void startJob(QString job_id);
    /** @brief 请求在当前片段提交后暂停，不中断已发出的模型请求。 */
    Q_INVOKABLE void pauseJob(QString job_id);
    /** @brief 从持久化检查点继续；远程任务仍要求界面明确确认。 */
    Q_INVOKABLE void resumeJob(QString job_id);
    /** @brief 对任务候选做本地证据质量抽样，不发送远程请求。 */
    Q_INVOKABLE void auditJob(QString job_id);
signals:
    void changed();
private:
    // 仅允许确定性回归等待自有线程完成；不向QML暴露线程池或新的执行权限。
    friend struct ExtractionJobViewModelTestAccess;
    struct RunControl;
    /** @brief 在自有串行线程池执行指定上限的批次，统一隔离异常与跨世界回调。 */
    void launchBatch(QString job_id, int maximum_steps, bool require_remote);
    /** @brief 在界面线程处理结束通知，并将检查点末尾迟到的取消补作持久化结算。 */
    void finishBatch(xuyan::domain::Result<xuyan::application::OfflineBatchResult> result,
                     std::uint64_t session, QString job_id, QString operation_world,
                     bool cancellation_settled = false);
    /** @brief 把最近一次检查点计数覆盖到列表，避免刷新时短暂倒退。 */
    void overlayProgress();
    /** @brief 将已由数据库按世界过滤的任务映射为当前列表。 */
    void applyListing(xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> jobs);
    /** @brief 操作完成后补做切换世界期间遗漏的查询。 */
    void refreshAfterWorldChange();
    /** @brief 将指定世界的任务转换成界面列表并保留步骤与预算信息。 */
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionJob>& jobs);
    std::filesystem::path database_path_;
    QVariantList jobs_;
    QString world_id_;
    std::uint64_t world_generation_{0};
    bool refresh_after_world_change_{false};
    bool busy_{false};
    bool retain_error_on_refresh_{false};
    QThreadPool worker_pool_;
    BatchRunner batch_runner_;
    std::shared_ptr<RunControl> run_control_;
    bool running_{false};
    QString active_job_id_;
    QString active_world_id_;
    std::uint64_t run_generation_{0};
    QHash<QString, QVariantMap> progress_;
    QSet<QString> paused_jobs_;
    QString error_text_;
    QString status_text_{QStringLiteral("任务队列已就绪")};
};
