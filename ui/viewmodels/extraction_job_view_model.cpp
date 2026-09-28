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
#include <algorithm>
#include <atomic>

namespace {
/** @brief 验证操作目标来自当前世界已加载的任务列表。 */
bool visibleJob(const QVariantList& jobs, const QString& job_id) {
    for (const auto& job : jobs)
        if (job.toMap().value(QStringLiteral("id")).toString() == job_id) return true;
    return false;
}

/** @brief 统一离线与远程批次的调用端口，连接类型来自数据库而非界面传入值。 */
xuyan::domain::Result<xuyan::application::OfflineBatchResult> executeBatch(
    const std::filesystem::path& path, const std::string& job_id,
    const xuyan::application::OfflineBatchOptions& options) {
    using namespace xuyan::application;
    using Result = xuyan::domain::Result<OfflineBatchResult>;
    auto state = ExtractionJobService(path).loadState(job_id);
    if (!state.ok()) return Result::failure(*state.error);
    if (state.value->provider_connection_id.empty())
        return MockExtractionProcessor(path).processBatch(job_id, options);
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::providers::QtProviderTransport transport;
    RemoteBatchOptions remote;
    remote.maximum_steps = options.maximum_steps;
    remote.stop_token = options.stop_token;
    remote.on_progress = [&](const RemoteBatchProgress& progress) {
        const auto action = options.on_progress({progress.job_id, progress.total_steps,
            progress.completed_steps, progress.processed_steps, progress.revision});
        if (action == OfflineBatchAction::cancel) return RemoteBatchAction::cancel;
        if (action == OfflineBatchAction::pause) return RemoteBatchAction::pause;
        return RemoteBatchAction::proceed;
    };
    auto result = RemoteExtractionProcessor(path, credentials, transport).processBatch(job_id, remote);
    if (!result.ok()) return Result::failure(*result.error);
    auto reason = OfflineBatchStopReason::needs_attention;
    switch (result.value->reason) {
    case RemoteBatchStopReason::completed: reason = OfflineBatchStopReason::completed; break;
    case RemoteBatchStopReason::paused: reason = OfflineBatchStopReason::paused; break;
    case RemoteBatchStopReason::cancelled: reason = OfflineBatchStopReason::cancelled; break;
    case RemoteBatchStopReason::step_limit: reason = OfflineBatchStopReason::step_limit; break;
    default: break; // 预算不足与问题步骤均停止，不能自动重试或扩充预算。
    }
    return Result::success({std::move(result.value->job), result.value->processed_steps, reason});
}

/** @brief 在批次开始时建立章节剩余片段索引，每次进度只处理新完成的片段。 */
class ChapterProgress final {
public:
    /** @brief 从完整持久化步骤重建计数；缓存命中和跨章节校订均按真实区间处理。 */
    ChapterProgress(const xuyan::domain::ExtractionJob& job,
                    const std::vector<xuyan::domain::SourceChapter>& chapters)
        : remaining_(chapters.size()), completed_steps_(job.completed_steps) {
        for (const auto& step : job.steps) {
            std::vector<std::size_t> indices;
            auto first = std::upper_bound(chapters.begin(), chapters.end(), step.start_codepoint,
                [](std::size_t start, const auto& chapter) { return start < chapter.end_codepoint; });
            for (auto chapter = first; chapter != chapters.end() && chapter->start_codepoint < step.end_codepoint; ++chapter) {
                const auto index = static_cast<std::size_t>(chapter - chapters.begin());
                if (step.status != "completed") ++remaining_[index];
                indices.push_back(index);
            }
            if (step.status == "ready") ready_.push_back(std::move(indices));
        }
        completed_chapters_ = static_cast<int>(std::count(remaining_.begin(), remaining_.end(), 0));
    }
    /** @brief 消费串行队列新增的已提交片段，失败或未知片段不计入完成章节。 */
    int advance(int completed_steps) {
        while (completed_steps_ < completed_steps && next_ < ready_.size()) {
            for (auto index : ready_[next_]) if (--remaining_[index] == 0) ++completed_chapters_;
            ++next_; ++completed_steps_;
        }
        return completed_chapters_;
    }
    /** @brief 返回任务来源当前章节总数。 */
    int total() const { return static_cast<int>(remaining_.size()); }
private:
    std::vector<int> remaining_;
    std::vector<std::vector<std::size_t>> ready_;
    std::size_t next_{0};
    int completed_steps_{0};
    int completed_chapters_{0};
};
}

/** @brief 跨线程只共享原子调度意图和停止令牌，不共享界面或数据库对象。 */
struct ExtractionJobViewModel::RunControl {
    std::atomic<xuyan::application::OfflineBatchAction> action{xuyan::application::OfflineBatchAction::proceed};
    std::stop_source stop;
};

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

ExtractionJobViewModel::ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent, BatchRunner runner)
    : QObject(parent), database_path_(std::move(database_path)),
      batch_runner_(runner ? std::move(runner) : BatchRunner(executeBatch)) {
    worker_pool_.setMaxThreadCount(1);
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

/** @brief 在对象成员和数据库路径销毁之前收回批次线程，退出不会另发请求。 */
ExtractionJobViewModel::~ExtractionJobViewModel() {
    if (run_control_) run_control_->stop.request_stop();
    worker_pool_.waitForDone();
    // 关闭窗口会丢弃排队的结束回调；此前明确点击的取消仍须在退出前持久化。
    if (run_control_ && run_control_->action.load() == xuyan::application::OfflineBatchAction::cancel) {
        try {
            xuyan::application::ExtractionJobService service(database_path_);
            const auto id = active_job_id_.toStdString();
            auto state = service.loadState(id);
            if (state.ok() && state.value->status != "completed" && state.value->status != "cancelled")
                service.cancel(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(), id, state.value->revision);
        } catch (...) { /* 析构不抛异常；无法落盘时仍保留原数据库检查点供下次检查。 */ }
    }
}

/** @brief 查询共享意图，不把等待当前 HTTP 请求结束伪装成即时中断。 */
bool ExtractionJobViewModel::stopping() const noexcept {
    return run_control_ && run_control_->action.load() != xuyan::application::OfflineBatchAction::proceed;
}

/** @brief 将已提交计数合并到任务卡片；落后修订的后台刷新不能覆盖更新进度。 */
void ExtractionJobViewModel::overlayProgress() {
    for (auto& item : jobs_) {
        auto map = item.toMap();
        const auto id = map.value("id").toString();
        const auto progress = progress_.value(id);
        if (progress.value("completed").toInt() == map.value("completed").toInt() && !progress.isEmpty()) {
            map.insert("completedChapters", progress.value("completedChapters"));
            map.insert("totalChapters", progress.value("totalChapters"));
        }
        if (!progress.isEmpty() && progress.value("revision").toInt() >= map.value("revision").toInt())
            for (auto it = progress.cbegin(); it != progress.cend(); ++it) map.insert(it.key(), it.value());
        map.insert("paused", paused_jobs_.contains(id));
        item = map;
    }
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
    else { jobs_ = maps(*jobs.value); overlayProgress(); }
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
    busy_ = true;
    if (!retain_error_on_refresh_) error_text_.clear();
    retain_error_on_refresh_ = false;
    emit changed();
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
    if (running_ && job_id == active_job_id_ && world_id_ == active_world_id_) {
        run_control_->action.store(xuyan::application::OfflineBatchAction::cancel);
        status_text_ = QStringLiteral("正在等待当前片段结束后取消；已提交结果保留");
        emit changed();
        return;
    }
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
    if (busy_ || (running_ && active_job_id_ == job_id) || world_id_.isEmpty() || ordinal <= 0 || !visibleJob(jobs_, job_id)) return;
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

/** @brief 兼容离线入口；绑定模型的任务不得误走离线候选协议。 */
void ExtractionJobViewModel::runMock(QString job_id) {
    for (const auto& item : jobs_) {
        const auto job = item.toMap();
        if (job.value("id").toString() == job_id && job.value("providerConnectionId").toString().isEmpty())
            launchBatch(job_id, 100000, false);
    }
}

/** @brief 复用有生命周期保护的批次执行器，仅明确执行一个远程片段。 */
void ExtractionJobViewModel::runRemoteSample(QString job_id) {
    launchBatch(std::move(job_id), 1, true);
}

/** @brief 显式启动全书的剩余步骤，后台读取或页面刷新不会调用此入口。 */
void ExtractionJobViewModel::startJob(QString job_id) {
    launchBatch(std::move(job_id), 100000, false);
}

/** @brief 继续复用同一持久化任务，不重置额度、不重跑完成片段。 */
void ExtractionJobViewModel::resumeJob(QString job_id) {
    startJob(std::move(job_id));
}

/** @brief 请求在检查点暂停；已请求取消时不降级为暂停。 */
void ExtractionJobViewModel::pauseJob(QString job_id) {
    if (!running_ || job_id != active_job_id_ || world_id_ != active_world_id_) return;
    auto expected = xuyan::application::OfflineBatchAction::proceed;
    run_control_->action.compare_exchange_strong(expected, xuyan::application::OfflineBatchAction::pause);
    if (expected == xuyan::application::OfflineBatchAction::cancel) return;
    status_text_ = QStringLiteral("正在等待当前片段结束后暂停；已提交结果保留");
    emit changed();
}

/** @brief 启动自有批次线程，进度仅在所属世界回显，结束后读取当前选择的世界。 */
void ExtractionJobViewModel::launchBatch(QString job_id, int maximum_steps, bool require_remote) {
    if (busy_ || running_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    QVariantMap selected;
    for (const auto& item : jobs_) if (item.toMap().value("id").toString() == job_id) selected = item.toMap();
    if (selected.value("status").toString() != "queued" || selected.value("cancelRequested").toBool()) return;
    if (require_remote && selected.value("providerConnectionId").toString().isEmpty()) return;
    const auto operation_world = world_id_;
    const auto session = ++run_generation_;
    const auto expected_provider = selected.value("providerConnectionId").toString();
    const auto path = database_path_;
    const auto runner = batch_runner_;
    const auto control = std::make_shared<RunControl>();
    run_control_ = control;
    active_job_id_ = job_id; active_world_id_ = operation_world; running_ = true;
    paused_jobs_.remove(job_id);
    error_text_.clear(); status_text_ = QStringLiteral("正在解析；每个片段独立保存检查点");
    overlayProgress(); emit changed();
    // 自有线程池在析构时等待；线程不会在视图模型销毁后继续访问成员或发布回调。
    worker_pool_.start([this, path, job_id, operation_world, session, control, runner, maximum_steps, require_remote, expected_provider] {
        using namespace xuyan::application;
        using Result = xuyan::domain::Result<OfflineBatchResult>;
        auto result = Result::failure({xuyan::domain::ErrorCode::storage_error,
            "后台解析未能执行；已保存的检查点仍保留", true, "检查任务后继续"});
        try {
            auto job = ExtractionJobService(path).load(job_id.toStdString());
            if (!job.ok()) result = Result::failure(*job.error);
            else {
                auto source = xuyan::storage::WorkspaceRepository(path).loadSource(job.value->source_id);
                if (!source.ok()) result = Result::failure(*source.error);
                else if (source.value->world_id != operation_world.toStdString()
                         || job.value->provider_connection_id != expected_provider.toStdString()
                         || (require_remote && job.value->provider_connection_id.empty()))
                    result = Result::failure({xuyan::domain::ErrorCode::validation_failed,
                        "任务归属或连接已改变，不会发送", false, "重新加载当前世界"});
                else {
                    ChapterProgress chapters(*job.value, source.value->chapters);
                    OfflineBatchOptions options;
                    options.maximum_steps = maximum_steps;
                    options.stop_token = control->stop.get_token();
                    const auto initial_requests = job.value->budget.consumed_requests;
                    options.on_progress = [this, control, session, operation_world, job_id, &chapters, initial_requests](const OfflineBatchProgress& progress) {
                        const auto completed_chapters = chapters.advance(progress.completed_steps);
                        const auto total_chapters = chapters.total();
                        QMetaObject::invokeMethod(this, [this, session, operation_world, job_id, progress,
                                                       completed_chapters, total_chapters, initial_requests] {
                            if (run_generation_ != session || !running_) return;
                            progress_.insert(job_id, {{"completed", progress.completed_steps}, {"total", progress.total_steps},
                                {"revision", progress.revision}, {"completedChapters", completed_chapters},
                                {"totalChapters", total_chapters}, {"consumedRequests", initial_requests + progress.processed_steps}});
                            if (world_id_ == operation_world) { overlayProgress(); emit changed(); }
                        }, Qt::QueuedConnection);
                        return control->action.load();
                    };
                    result = runner(path, job_id.toStdString(), options);
                    // 失败/未知结果也可取消剩余片段；核心处理器不会在这些状态自动继续。
                    if (control->action.load() == OfflineBatchAction::cancel) {
                        ExtractionJobService service(path);
                        auto state = service.loadState(job_id.toStdString());
                        if (state.ok() && state.value->status != "completed" && state.value->status != "cancelled") {
                            auto cancelled = service.cancel(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                job_id.toStdString(), state.value->revision);
                            if (!cancelled.ok()) result = Result::failure(*cancelled.error);
                            else result = Result::success({std::move(*cancelled.value), 0, OfflineBatchStopReason::cancelled});
                        }
                    }
                }
            }
        } catch (...) {
            // 不向界面传播原文、路径、模型返回值或凭据等异常细节。
            result = Result::failure({xuyan::domain::ErrorCode::storage_error,
                "后台解析发生内部错误；已提交片段保留", true, "刷新任务后从检查点继续"});
        }
        QMetaObject::invokeMethod(this, [this, result = std::move(result), session, job_id, operation_world]() mutable {
            finishBatch(std::move(result), session, job_id, operation_world);
        }, Qt::QueuedConnection);
    });
}

/** @brief 在界面线程最终结算批次；工作线程已退出但结束通知尚未到达时仍接受取消意图。 */
void ExtractionJobViewModel::finishBatch(
    xuyan::domain::Result<xuyan::application::OfflineBatchResult> result, std::uint64_t session,
    QString job_id, QString operation_world, bool cancellation_settled) {
    using namespace xuyan::application;
    if (session != run_generation_) return;
    const bool terminal = result.ok() && (result.value->job.status == "completed" || result.value->job.status == "cancelled");
    if (!cancellation_settled && !terminal && run_control_
        && run_control_->action.load() == OfflineBatchAction::cancel) {
        // 弥补最后进度通知与结束通知之间的取消窗口；此时仍保留运行态，不允许另起批次。
        const auto path = database_path_;
        worker_pool_.start([this, path, session, job_id, operation_world] {
            using Result = xuyan::domain::Result<OfflineBatchResult>;
            auto settled = Result::failure({xuyan::domain::ErrorCode::storage_error,
                "取消请求无法保存；检查点保留，请刷新后重试", true, "重试取消"});
            try {
                ExtractionJobService service(path);
                auto state = service.loadState(job_id.toStdString());
                if (!state.ok()) settled = Result::failure(*state.error);
                else {
                    auto job = state.value->status == "completed" || state.value->status == "cancelled"
                        ? service.load(job_id.toStdString())
                        : service.cancel(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                                         job_id.toStdString(), state.value->revision);
                    if (!job.ok()) settled = Result::failure(*job.error);
                    else {
                        const auto reason = job.value->status == "completed" ? OfflineBatchStopReason::completed : OfflineBatchStopReason::cancelled;
                        settled = Result::success({std::move(*job.value), 0, reason});
                    }
                }
            } catch (...) { /* 保留统一中文错误，不在结束回调中抛异常或泄露私人内容。 */ }
            QMetaObject::invokeMethod(this, [this, settled = std::move(settled), session, job_id, operation_world]() mutable {
                finishBatch(std::move(settled), session, job_id, operation_world, true);
            }, Qt::QueuedConnection);
        });
        return;
    }
    running_ = false; active_job_id_.clear(); active_world_id_.clear(); run_control_.reset();
    if (result.ok() && result.value->reason == OfflineBatchStopReason::paused) paused_jobs_.insert(job_id);
    if (world_id_ == operation_world) {
        if (!result.ok()) {
            error_text_ = QString::fromStdString(result.error->message);
            retain_error_on_refresh_ = true;
        } else {
            switch (result.value->reason) {
            case OfflineBatchStopReason::completed: status_text_ = QStringLiteral("解析完成；候选已进入人工校对，未自动写入世界"); break;
            case OfflineBatchStopReason::paused: status_text_ = QStringLiteral("已暂停；可以从检查点继续"); break;
            case OfflineBatchStopReason::cancelled: status_text_ = QStringLiteral("已取消剩余片段；已提交候选保留"); break;
            case OfflineBatchStopReason::step_limit: status_text_ = QStringLiteral("本批次结束；可继续处理剩余片段"); break;
            default: status_text_ = QStringLiteral("解析已停止，请核对问题片段或调用上限；不会自动重发"); break;
            }
        }
    }
    overlayProgress(); emit changed();
    refresh_after_world_change_ = true;
    refreshAfterWorldChange();
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
