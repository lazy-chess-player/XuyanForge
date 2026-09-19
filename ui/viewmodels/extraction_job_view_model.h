#pragma once

#include "xuyan/domain/extraction_job.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>

class ExtractionJobViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:
    explicit ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList jobs() const { return jobs_; }
    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QString statusText() const { return status_text_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void createJob(QString source_id, int chunk_size, int overlap, int max_requests,
                              int output_token_limit, QString provider_connection_id);
    Q_INVOKABLE void cancelJob(QString job_id, int revision);
    Q_INVOKABLE void retryStep(QString job_id, int ordinal, int attempt);
    Q_INVOKABLE void runMock(QString job_id);
    Q_INVOKABLE void runRemoteSample(QString job_id);
    Q_INVOKABLE void auditJob(QString job_id);
signals:
    void changed();
private:
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionJob>& jobs);
    std::filesystem::path database_path_;
    QVariantList jobs_;
    bool busy_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("任务队列已就绪")};
};
