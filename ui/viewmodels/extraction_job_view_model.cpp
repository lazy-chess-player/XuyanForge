#include "extraction_job_view_model.h"

#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/mock_extraction_processor.h"
#include "xuyan/application/remote_extraction_processor.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

QVariantList ExtractionJobViewModel::maps(const std::vector<xuyan::domain::ExtractionJob>& jobs) {
    QVariantList result;
    for (const auto& job : jobs) {
        QVariantList steps; int problem_ordinal = 0; int problem_attempt = 0;
        for (const auto& step : job.steps) {
            steps.push_back(QVariantMap{{"ordinal", step.ordinal}, {"status", QString::fromStdString(step.status)},
                {"attempt", step.attempt}, {"start", static_cast<qlonglong>(step.start_codepoint)},
                {"end", static_cast<qlonglong>(step.end_codepoint)}});
            if (problem_ordinal == 0 && (step.status == "failed" || step.status == "unknown")) {
                problem_ordinal = step.ordinal; problem_attempt = step.attempt;
            }
        }
        result.push_back(QVariantMap{{"id", QString::fromStdString(job.id)}, {"sourceId", QString::fromStdString(job.source_id)},
            {"status", QString::fromStdString(job.status)}, {"revision", job.revision}, {"total", job.total_steps},
            {"completed", job.completed_steps}, {"cancelRequested", job.cancel_requested}, {"steps", steps},
            {"problemOrdinal", problem_ordinal}, {"problemAttempt", problem_attempt},
            {"estimatedTokens", static_cast<qlonglong>(job.budget.estimated_input_tokens)},
            {"maxRequests", job.budget.max_requests}, {"consumedRequests", job.budget.consumed_requests},
            {"outputTokenLimit", job.budget.output_token_limit_per_request},
            {"providerConnectionId", QString::fromStdString(job.provider_connection_id)},
            {"modelId", QString::fromStdString(job.model_id)},
            {"sampleSteps", job.budget.sample_steps}, {"priceKnown", job.budget.price_known},
            {"schemaVersion", QString::fromStdString(job.schema_version)}, {"promptVersion", QString::fromStdString(job.prompt_version)}});
    }
    return result;
}

ExtractionJobViewModel::ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {
    busy_ = true; QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::application::ExtractionJobService service(path); service.recoverInterrupted(); auto result = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = QString::fromStdString(result.error->message);
            else self->jobs_ = maps(*result.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::refresh() {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::application::ExtractionJobService service(path); auto result = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = QString::fromStdString(result.error->message);
            else self->jobs_ = maps(*result.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::createJob(QString source_id, int chunk_size, int overlap, int max_requests,
                                       int output_token_limit, QString provider_connection_id) {
    if (busy_ || source_id.trimmed().isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在按段落边界建立持久化步骤…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, source_id, chunk_size, overlap, max_requests,
                                          output_token_limit, provider_connection_id, command] {
        xuyan::application::ExtractionJobService service(path);
        auto created = service.create(command, source_id.toStdString(), static_cast<std::size_t>(chunk_size),
                                      static_cast<std::size_t>(overlap), max_requests, output_token_limit,
                                      provider_connection_id.toStdString());
        auto listed = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, created = std::move(created), listed = std::move(listed)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!created.ok()) self->error_text_ = QString::fromStdString(created.error->message);
            else self->status_text_ = QStringLiteral("已创建 %1 个可恢复步骤").arg(created.value->total_steps);
            if (listed.ok()) self->jobs_ = maps(*listed.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::cancelJob(QString job_id, int revision) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, job_id, revision, command] {
        xuyan::application::ExtractionJobService service(path);
        auto changed = service.cancel(command, job_id.toStdString(), revision); auto listed = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!changed.ok()) self->error_text_ = QString::fromStdString(changed.error->message);
            else self->status_text_ = QStringLiteral("取消请求已持久化");
            if (listed.ok()) self->jobs_ = maps(*listed.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::retryStep(QString job_id, int ordinal, int attempt) {
    if (busy_ || ordinal <= 0) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, job_id, ordinal, attempt, command] {
        xuyan::application::ExtractionJobService service(path);
        auto changed = service.retryStep(command, job_id.toStdString(), ordinal, attempt); auto listed = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!changed.ok()) self->error_text_ = QString::fromStdString(changed.error->message);
            else self->status_text_ = QStringLiteral("失败/未知步骤已重新排队，不会重跑已完成步骤");
            if (listed.ok()) self->jobs_ = maps(*listed.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::runMock(QString job_id) {
    if (busy_ || job_id.isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在使用离线 Mock 执行版本化候选提取…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id] {
        xuyan::application::MockExtractionProcessor processor(path);
        auto processed = processor.processAll(job_id.toStdString());
        xuyan::application::ExtractionJobService service(path); auto listed = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, processed = std::move(processed), listed = std::move(listed)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!processed.ok()) self->error_text_ = QString::fromStdString(processed.error->message);
            else self->status_text_ = QStringLiteral("Mock 提取完成；候选已进入校对区，不会自动成为事实");
            if (listed.ok()) self->jobs_ = maps(*listed.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::runRemoteSample(QString job_id) {
    if (busy_ || job_id.isEmpty()) return;
    busy_ = true; error_text_.clear();
    status_text_ = QStringLiteral("正在发送当前 1 个文本块至所选模型并校验返回引文…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id] {
        xuyan::platform::SystemCredentialStore credentials;
        xuyan::providers::QtProviderTransport transport;
        xuyan::application::RemoteExtractionProcessor processor(path, credentials, transport);
        auto processed = processor.processNext(job_id.toStdString());
        xuyan::application::ExtractionJobService service(path); auto listed = service.list();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, processed = std::move(processed), listed = std::move(listed)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!processed.ok()) self->error_text_ = QString::fromStdString(processed.error->message);
            else if (processed.value->status == "needs_attention")
                self->status_text_ = QStringLiteral("当前步骤未完成，请查看错误并按需重试；不会自动重发");
            else self->status_text_ = QStringLiteral("已执行 1 步；可定位候选进入人工校对，未自动写入世界");
            if (listed.ok()) self->jobs_ = maps(*listed.value);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::auditJob(QString job_id) {
    if (busy_ || job_id.isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在本地复核候选证据样本…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id] {
        xuyan::application::ExtractionJobService service(path); auto report = service.qualityReport(job_id.toStdString(), 100);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, report = std::move(report)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!report.ok()) self->error_text_ = QString::fromStdString(report.error->message);
            else self->status_text_ = QStringLiteral("抽样 %1 项：证据可定位 %2，已接受 %3，已拒绝 %4，待校对 %5；模型精确率/召回率尚未实测")
                .arg(report.value->sampled_candidates).arg(report.value->evidence_valid).arg(report.value->accepted)
                .arg(report.value->rejected).arg(report.value->unresolved);
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}
