#include "extraction_job_view_model.h"

#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/mock_extraction_processor.h"
#include "xuyan/application/remote_extraction_processor.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"
#include "xuyan/storage/workspace_repository.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>

namespace {
/** @brief 验证操作目标来自当前世界已加载的任务列表。 */
bool visibleJob(const QVariantList& jobs, const QString& job_id) {
    for (const auto& job : jobs)
        if (job.toMap().value(QStringLiteral("id")).toString() == job_id) return true;
    return false;
}
}

/** @brief 序列化数据库已按世界筛选的任务，同时保留步骤状态和预算数值。 */
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
        xuyan::application::ExtractionJobService service(path); auto result = service.recoverInterrupted();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = QString::fromStdString(result.error->message);
            emit self->changed();
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

/** @brief 世界切换时立即移除旧任务，并在后台操作结束后读取新世界任务。 */
void ExtractionJobViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++world_generation_;
    jobs_.clear(); error_text_.clear(); status_text_ = QStringLiteral("任务队列已就绪");
    if (busy_) refresh_after_world_change_ = true;
    else if (!world_id_.isEmpty()) refresh();
    emit changed();
}

/** @brief 应用按当前世界在数据库筛选的后台任务结果。 */
void ExtractionJobViewModel::applyListing(
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> jobs) {
    if (!jobs.ok()) error_text_ = QString::fromStdString(jobs.error->message);
    else jobs_ = maps(*jobs.value);
    emit changed();
}

/** @brief 在任务操作完成后重新读取切换期间的当前世界。 */
void ExtractionJobViewModel::refreshAfterWorldChange() {
    if (!refresh_after_world_change_) return;
    refresh_after_world_change_ = false;
    refresh();
}

/** @brief 异步读取持久化任务及其来源归属。 */
void ExtractionJobViewModel::refresh() {
    if (world_id_.isEmpty()) { jobs_.clear(); emit changed(); return; }
    if (busy_) { refresh_after_world_change_ = true; return; }
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto operation_world = world_id_; const auto world_generation = world_generation_;
    QThreadPool::globalInstance()->start([self, path, operation_world, world_generation] {
        xuyan::application::ExtractionJobService service(path);
        auto result = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            self->applyListing(std::move(result));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::createJob(QString source_id, int chunk_size, int overlap, int max_requests,
                                       int output_token_limit, QString provider_connection_id) {
    if (busy_ || world_id_.isEmpty() || source_id.trimmed().isEmpty()) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在按段落边界建立持久化步骤…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, source_id, chunk_size, overlap, max_requests,
                                          output_token_limit, provider_connection_id, command, operation_world, world_generation] {
        xuyan::application::ExtractionJobService service(path);
        xuyan::domain::Result<xuyan::domain::ExtractionJob> created;
        try {
            xuyan::storage::WorkspaceRepository repository(path);
            auto source = repository.loadSource(source_id.toStdString());
            if (!source.ok()) created = decltype(created)::failure(*source.error);
            else if (QString::fromStdString(source.value->world_id) != operation_world)
                created = decltype(created)::failure({xuyan::domain::ErrorCode::validation_failed,
                    "来源不属于当前世界", false, "重新选择当前世界的小说来源"});
            else created = service.create(command, source_id.toStdString(), static_cast<std::size_t>(chunk_size),
                                          static_cast<std::size_t>(overlap), max_requests, output_token_limit,
                                          provider_connection_id.toStdString());
        } catch (const std::exception&) {
            created = decltype(created)::failure({xuyan::domain::ErrorCode::storage_error,
                "检查来源归属失败", true, "刷新工作区后重试"});
        }
        auto listed = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, created = std::move(created), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!created.ok()) self->error_text_ = QString::fromStdString(created.error->message);
                else self->status_text_ = QStringLiteral("已创建 %1 个可恢复步骤").arg(created.value->total_steps);
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::cancelJob(QString job_id, int revision) {
    if (busy_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, job_id, revision, command, operation_world, world_generation] {
        xuyan::application::ExtractionJobService service(path);
        auto changed = service.cancel(command, job_id.toStdString(), revision);
        auto listed = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!changed.ok()) self->error_text_ = QString::fromStdString(changed.error->message);
                else self->status_text_ = QStringLiteral("取消请求已持久化");
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::retryStep(QString job_id, int ordinal, int attempt) {
    if (busy_ || world_id_.isEmpty() || ordinal <= 0 || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, path, job_id, ordinal, attempt, command, operation_world, world_generation] {
        xuyan::application::ExtractionJobService service(path);
        auto changed = service.retryStep(command, job_id.toStdString(), ordinal, attempt);
        auto listed = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!changed.ok()) self->error_text_ = QString::fromStdString(changed.error->message);
                else self->status_text_ = QStringLiteral("失败/未知步骤已重新排队，不会重跑已完成步骤");
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::runMock(QString job_id) {
    if (busy_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在使用离线规则提取候选…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id, operation_world, world_generation] {
        xuyan::application::MockExtractionProcessor processor(path);
        auto processed = processor.processAll(job_id.toStdString());
        xuyan::application::ExtractionJobService service(path);
        auto listed = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, processed = std::move(processed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!processed.ok()) self->error_text_ = QString::fromStdString(processed.error->message);
                else self->status_text_ = QStringLiteral("离线提取完成；候选已进入校对区，不会自动成为事实");
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::runRemoteSample(QString job_id) {
    if (busy_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear();
    status_text_ = QStringLiteral("正在发送当前 1 个文本块至所选模型并校验返回引文…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id, operation_world, world_generation] {
        xuyan::platform::SystemCredentialStore credentials;
        xuyan::providers::QtProviderTransport transport;
        xuyan::application::RemoteExtractionProcessor processor(path, credentials, transport);
        auto processed = processor.processNext(job_id.toStdString());
        xuyan::application::ExtractionJobService service(path);
        auto listed = service.listForWorld(operation_world.toStdString());
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, processed = std::move(processed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!processed.ok()) self->error_text_ = QString::fromStdString(processed.error->message);
                else if (processed.value->status == "needs_attention")
                    self->status_text_ = QStringLiteral("当前步骤未完成，请查看错误并按需重试；不会自动重发");
                else self->status_text_ = QStringLiteral("已执行 1 步；可定位候选进入人工校对，未自动写入世界");
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::auditJob(QString job_id) {
    if (busy_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在本地复核候选证据样本…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, job_id, operation_world, world_generation] {
        xuyan::application::ExtractionJobService service(path); auto report = service.qualityReport(job_id.toStdString(), 100);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, report = std::move(report), operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!report.ok()) self->error_text_ = QString::fromStdString(report.error->message);
                else self->status_text_ = QStringLiteral("抽样 %1 项：证据可定位 %2，已接受 %3，已拒绝 %4，待校对 %5；模型精确率/召回率尚未实测")
                    .arg(report.value->sampled_candidates).arg(report.value->evidence_valid).arg(report.value->accepted)
                    .arg(report.value->rejected).arg(report.value->unresolved);
            }
            emit self->changed();
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}
