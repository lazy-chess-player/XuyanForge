#pragma once

#include "xuyan/domain/extraction_job.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>
#include <cstdint>

class ExtractionJobViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:
    /** @brief 初始化工作区任务服务并恢复被中断的步骤状态。 */
    explicit ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    /** @brief 返回当前世界的解析任务列表。 */
    QVariantList jobs() const { return jobs_; }
    /** @brief 返回任务服务是否正在执行操作。 */
    bool busy() const noexcept { return busy_; }
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
    /** @brief 对任务候选做本地证据质量抽样，不发送远程请求。 */
    Q_INVOKABLE void auditJob(QString job_id);
signals:
    void changed();
private:
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
    QString error_text_;
    QString status_text_{QStringLiteral("任务队列已就绪")};
};
