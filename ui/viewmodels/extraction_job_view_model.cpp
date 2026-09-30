#include "extraction_job_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/mock_extraction_processor.h"
#include "xuyan/application/remote_extraction_processor.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"
#include "xuyan/application/source_import_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <algorithm>
#include <atomic>

namespace {
/*
 * 功能：确认用户操作的任务仍出现在当前世界的可见任务列表中。
 * 参数：jobs 为本次调用借用的界面任务快照；job_id 为目标稳定任务 ID，可空但无法匹配。
 * 返回：找到相同 ID 返回 true，否则 false。
 * 失败：不校验数据库当前归属；QVariant 转换异常可能传播。
 * 副作用：只读界面快照，不访问存储或网络。
 */
bool visibleJob(const QVariantList& jobs, const QString& job_id) {
    for (const auto& job : jobs)
        if (job.toMap().value(QStringLiteral("id")).toString() == job_id) return true;
    return false;
}

/*
 * 功能：按持久化任务的连接 ID 选择离线或远程批次处理器，不信任界面提供商选择。
 * 参数：path 为调用期间借用的工作区路径；job_id 为稳定任务 ID；options 为步数、停止令牌及进度回调配置。
 * 返回：统一的批次结果与停止原因；失败时保留底层 Result 错误。
 * 失败：任务状态读取、凭据、网络或处理器失败返回 Result；构造/回调异常可传播到调用方。
 * 副作用：离线或远程处理器可能持久化步骤/候选；远程任务可能发送并计费，调用方必须先显式启动。
 * 线程与生命周期：由调用方工作线程同步执行；本地凭据和传输对象活到 processBatch 返回，回调只在此期间借用 options。
 */
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
    /*
     * 功能：把远程处理器进度转成统一批次动作，保留暂停和取消意图。
     * 参数：progress 为调用期间借用的远程进度；按引用捕获的 options 及 remote 只在同步 processBatch 期间有效。
     * 返回：对应的远程继续、暂停或取消动作。
     * 失败：上层进度回调异常向处理器传播，由批次调用方处理。
     * 副作用：调用 options.on_progress，可能排队发 GUI 进度通知；本回调不直接读写数据库。
     * 线程与生命周期：在处理器调用线程运行，回调不在 executeBatch 返回后保留。
     */
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

/*
 * 职责：在一次批次执行期间将持久化片段进度折算成已完成章节数量。
 * 数据来源与不变量：构造时读取完整任务步骤及章节半开码点区间；只把状态为 ready 的后续成功计入进度。
 * 资源与线程：只拥有计数和索引值，不持有数据库、界面或原文；创建、advance 和销毁均在同一批次工作线程。
 * 生命周期：批次结束即销毁，进度回调仅在其存活期间借用对象。
 */
class ChapterProgress final {
public:
    /*
     * 功能：从任务的已持久化步骤重建每章未完成片段数与可推进步骤索引。
     * 参数：job 为本次调用借用的任务快照；chapters 为按原文位置排序的章节快照，只借用至构造结束。
     * 返回：完成独立计数器初始化，包含已完成章节数量。
     * 失败：容器分配异常传播；不执行数据库或原文校验。
     * 副作用：只构建当前对象，不写任务或 GUI 状态。
     */
    ChapterProgress(const xuyan::domain::ExtractionJob& job,
                    const std::vector<xuyan::domain::SourceChapter>& chapters)
        : remaining_(chapters.size()), completed_steps_(job.completed_steps) {
        for (const auto& step : job.steps) {
            std::vector<std::size_t> indices;
            auto first = std::upper_bound(chapters.begin(), chapters.end(), step.start_codepoint,
                /*
                 * 功能：为 upper_bound 定位第一个结束位置超过片段起点的章节。
                 * 参数：start 为绝对 Unicode 码点起点；chapter 为本次比较借用的章节。
                 * 返回：片段起点早于章节半开终点时返回 true。
                 * 失败：无额外校验或异常。
                 * 副作用：只读章节边界；闭包不逃逸构造调用。
                 */
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
    /*
     * 功能：消费从上次通知以来新增的已提交片段，更新章节剩余计数。
     * 参数：completed_steps 为任务累计完成片段数，单位片段；应单调不减。
     * 返回：截至本次通知的已完成章节数，单位章。
     * 失败：若外部计数超过 ready 索引，仅处理已有索引，不访问越界元素。
     * 副作用：推进 next_、completed_steps_ 并原位更新剩余数，不读写数据库。
     */
    int advance(int completed_steps) {
        while (completed_steps_ < completed_steps && next_ < ready_.size()) {
            for (auto index : ready_[next_]) if (--remaining_[index] == 0) ++completed_chapters_;
            ++next_; ++completed_steps_;
        }
        return completed_chapters_;
    }
    /*
     * 功能：读取构造时捕获的来源章节数量。
     * 参数：无。
     * 返回：章节数，单位章；无章节时返回零。
     * 失败：不抛异常。
     * 副作用：只读本对象的章节计数。
     */
    int total() const { return static_cast<int>(remaining_.size()); }
private:
    /* 每章尚未完成的片段数；构造时按全部步骤建立，advance 在批次工作线程递减，寿命同本对象。 */
    std::vector<int> remaining_;
    /* 状态为 ready 的步骤按持久化顺序映射到所覆盖的章节下标，构造后只读，寿命同本对象。 */
    std::vector<std::vector<std::size_t>> ready_;
    /* 下一条尚未消费的 ready_ 零基下标，默认零，仅 advance 递增，单位片段。 */
    std::size_t next_{0};
    /* 已知完成步骤累计数，构造取任务检查点，advance 更新；单位片段。 */
    int completed_steps_{0};
    /* 剩余片段数已归零的章节累计数，构造计一次，advance 在章节首次归零时增加；单位章。 */
    int completed_chapters_{0};
};
}

/*
 * 职责：在 GUI 与专属批次工作线程之间共享可取消调度意图。
 * 资源与线程：只拥有原子动作及停止源，不持有 GUI、数据库连接、传输或凭据。
 * 生命周期：由启动批次的 shared_ptr 持有，批次结束且通知完成后释放。
 */
struct ExtractionJobViewModel::RunControl {
    /* 最近一次继续/暂停/取消意图，默认继续；GUI 原子写入，工作线程在片段检查点读取。 */
    std::atomic<xuyan::application::OfflineBatchAction> action{xuyan::application::OfflineBatchAction::proceed};
    /* 停止源，默认未请求停止；析构时 GUI 请求停止，工作线程持有配套 stop_token。 */
    std::stop_source stop;
};

/*
 * 功能：把任务值对象映射为 QML 可读的摘要，保留协议原值并增补中文状态标签。
 * 参数：jobs 为已由服务按世界筛选的任务列表，调用期间借用；步骤序号从一开始，预算单位按字段定义。
 * 返回：同序独立 QVariantList；无任务返回空列表。
 * 失败：容器或字符串分配异常可传播，不把异常当成空列表。
 * 副作用：只构造 GUI 快照，不改任务、不访问模型或数据库。
 */
QVariantList ExtractionJobViewModel::maps(const std::vector<xuyan::domain::ExtractionJob>& jobs) {
    QVariantList result;
    for (const auto& job : jobs) {
        QVariantList steps; int problem_ordinal = 0; int problem_attempt = 0;
        for (const auto& step : job.steps) {
            steps.push_back(QVariantMap{{"ordinal", step.ordinal}, {"status", QString::fromStdString(step.status)},
                {"statusLabel", view_model_text::stateLabel(step.status)},
                {"attempt", step.attempt}, {"start", static_cast<qlonglong>(step.start_codepoint)},
                {"end", static_cast<qlonglong>(step.end_codepoint)}});
            if (problem_ordinal == 0 && (step.status == "failed" || step.status == "unknown")) {
                problem_ordinal = step.ordinal; problem_attempt = step.attempt;
            }
        }
        result.push_back(QVariantMap{{"id", QString::fromStdString(job.id)}, {"sourceId", QString::fromStdString(job.source_id)},
            {"status", QString::fromStdString(job.status)}, {"statusLabel", view_model_text::stateLabel(job.status)},
            {"revision", job.revision}, {"total", job.total_steps},
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
    /*
     * 功能：启动时在工作线程恢复上次中断任务的持久化状态。
     * 参数：闭包按值捕获工作区路径与弱引用 self；无调用参数。
     * 返回：无；Result 经 GUI 排队回调交付。
     * 失败：恢复服务异常转换为中文错误；对象销毁时不更新界面。
     * 副作用：服务可能修改中断任务状态，但不会启动解析或发送请求。
     * 线程与生命周期：全局线程池执行，弱引用不延长对象寿命。
     */
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::domain::Result<int> result;
        try { result = xuyan::application::ExtractionJobService(path).recoverInterrupted(); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "中断任务恢复失败", true, "检查工作区后刷新任务"}); }
        if (!self) return;
        /*
         * 功能：将恢复结果应用到当前 GUI，并刷新当前选择世界的任务目录。
         * 参数：捕获弱引用和拥有的恢复 Result；无调用参数。
         * 返回：无；对象已销毁时直接放弃。
         * 失败：失败结果映射为中文错误，不自动重试中断任务。
         * 副作用：复位忙碌标志，发 changed，可能投递目录刷新。
         * 线程与生命周期：Qt 排队到 GUI 线程执行。
         */
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = view_model_text::errorText(*result.error);
            emit self->changed();
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

/*
 * 功能：结束自有批次线程，并尽力持久化已由用户明确请求的取消动作。
 * 参数：无。
 * 返回：无；析构完成后批次线程不再访问本对象。
 * 失败：取消落库异常被吞掉；下次打开仍需依据原检查点检查任务，不伪装为取消成功。
 * 副作用：发停止请求、等待专属线程池，必要时写入取消状态；不启动新模型请求。
 * 线程与生命周期：应在 GUI 所属线程析构；先等待使用 this 的批次线程，再释放成员。
 */
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

/*
 * 功能：判断当前批次是否已收到暂停或取消意图。
 * 参数：无。
 * 返回：有控制对象且动作不是继续时为 true，否则 false；不表示在途网络调用已经结束。
 * 失败：不抛异常。
 * 副作用：仅原子读取，不改持久化任务或界面。
 */
bool ExtractionJobViewModel::stopping() const noexcept {
    return run_control_ && run_control_->action.load() != xuyan::application::OfflineBatchAction::proceed;
}

/*
 * 功能：将本进程批次进度叠加到任务卡片，同时避免旧修订覆盖较新的持久化摘要。
 * 参数：无。
 * 返回：无；任务目录为空时不产生卡片。
 * 失败：QVariant 容器分配异常可能传播。
 * 副作用：原位更新 jobs_ 中的进度、章节计数与暂停标志，不写数据库。
 * 线程与生命周期：仅 GUI 线程调用；progress_ 的值来自排队回调。
 */
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

void ExtractionJobViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++world_generation_;
    jobs_.clear(); error_text_.clear(); status_text_ = tr("任务队列已就绪");
    if (busy_) refresh_after_world_change_ = true;
    else if (!world_id_.isEmpty()) refresh();
    emit changed();
}

/*
 * 功能：把当前世界查询结果映射成任务卡片并叠加尚未进入目录快照的本地进度。
 * 参数：jobs 为拥有的服务 Result，成功时包含已按世界筛选的任务；失败时包含错误。
 * 返回：无；失败时保留现有任务卡片并更新错误文字。
 * 失败：错误映射为中文提示；值分配异常可能传播。
 * 副作用：成功时替换 jobs_ 并发 changed，不修改数据库。
 * 线程与生命周期：GUI 线程同步调用，传入值在函数内消费。
 */
void ExtractionJobViewModel::applyListing(
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> jobs) {
    if (!jobs.ok()) error_text_ = view_model_text::errorText(*jobs.error);
    else { jobs_ = maps(*jobs.value); overlayProgress(); }
    emit changed();
}

/*
 * 功能：兑现一次后台操作期间挂起的世界刷新请求。
 * 参数：无。
 * 返回：无；没有挂起请求时直接返回。
 * 失败：后续目录查询错误由 refresh 报告。
 * 副作用：清除挂起标记，可能启动一次异步目录读取。
 * 线程与生命周期：仅 GUI 线程调用，不捕获已结束的操作对象。
 */
void ExtractionJobViewModel::refreshAfterWorldChange() {
    if (!refresh_after_world_change_) return;
    refresh_after_world_change_ = false;
    refresh();
}

void ExtractionJobViewModel::refresh() {
    if (world_id_.isEmpty()) { jobs_.clear(); emit changed(); return; }
    if (busy_) { refresh_after_world_change_ = true; return; }
    busy_ = true;
    if (!retain_error_on_refresh_) error_text_.clear();
    retain_error_on_refresh_ = false;
    emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto operation_world = world_id_; const auto world_generation = world_generation_;
    /*
     * 功能：在工作线程读取指定世界的持久化任务目录。
     * 参数：闭包按值捕获路径、操作世界、世界代次与弱引用 self。
     * 返回：无；Result 经 GUI 排队回调交付。
     * 失败：服务异常转中文存储错误；对象销毁则不投递界面结果。
     * 副作用：只读任务元数据，不执行任何步骤或网络请求。
     * 线程与生命周期：全局线程池运行，闭包不借用视图模型成员。
     */
    QThreadPool::globalInstance()->start([self, path, operation_world, world_generation] {
        xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> result;
        try { result = xuyan::application::ExtractionJobService(path).listForWorld(operation_world.toStdString()); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "任务目录读取失败", true, "检查工作区后刷新"}); }
        if (!self) return;
        /*
         * 功能：仅在当前世界和代次仍对应请求时应用任务目录。
         * 参数：捕获弱引用、拥有的目录 Result、操作世界及代次。
         * 返回：无；过期请求不覆盖新世界列表。
         * 失败：Result 失败交 applyListing 转中文错误。
         * 副作用：复位忙碌态并可能兑现延迟刷新，发 changed。
         * 线程与生命周期：Qt 排队到 GUI 线程，对象消失时放弃通知。
         */
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
    busy_ = true; error_text_.clear(); status_text_ = tr("正在按段落边界建立持久化步骤…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    /*
     * 功能：校验来源仍属于操作世界，再按切片与预算参数创建可恢复任务并读取目录。
     * 参数：闭包按值持有路径、来源 ID、切片码点大小与重叠、请求/输出上限、连接 ID、命令 ID、世界及代次；self 为弱引用。
     * 返回：无；创建和目录两个 Result 通过 GUI 排队回调传递。
     * 失败：来源读取、归属校验、任务参数或存储错误被保留；异常转中文错误。
     * 副作用：成功时写持久化任务与步骤，不实际执行步骤或发送模型请求。
     * 线程与生命周期：全局线程池运行，GUI 对象销毁不撤销已完成的创建。
     */
    QThreadPool::globalInstance()->start([self, path, source_id, chunk_size, overlap, max_requests,
                                          output_token_limit, provider_connection_id, command, operation_world, world_generation] {
        xuyan::domain::Result<xuyan::domain::ExtractionJob> created;
        auto listed = xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>>::failure(
            {xuyan::domain::ErrorCode::storage_error, "任务目录未能刷新", true, "刷新任务后检查实际状态"});
        try {
            xuyan::application::ExtractionJobService service(path);
            auto source = xuyan::application::SourceImportService(path).load(source_id.toStdString());
            if (!source.ok()) created = decltype(created)::failure(*source.error);
            else if (QString::fromStdString(source.value->world_id) != operation_world)
                created = decltype(created)::failure({xuyan::domain::ErrorCode::validation_failed,
                    "来源不属于当前世界", false, "重新选择当前世界的小说来源"});
            else created = service.create(command, source_id.toStdString(), static_cast<std::size_t>(chunk_size),
                                          static_cast<std::size_t>(overlap), max_requests, output_token_limit,
                                          provider_connection_id.toStdString());
        } catch (...) {
            created = decltype(created)::failure({xuyan::domain::ErrorCode::storage_error,
                "检查来源归属失败", true, "刷新工作区后重试"});
        }
        try { listed = xuyan::application::ExtractionJobService(path).listForWorld(operation_world.toStdString()); }
        catch (...) { listed = decltype(listed)::failure({xuyan::domain::ErrorCode::storage_error,
            "创建后的任务目录读取失败", true, "刷新任务"}); }
        if (!self) return;
        /*
         * 功能：仅为原操作世界显示任务创建结果，并应用同世界的最新目录。
         * 参数：捕获弱引用、拥有的创建/目录 Result、操作世界及代次。
         * 返回：无；世界切换后放弃旧提示并安排当前世界刷新。
         * 失败：创建失败显示分类中文错误，目录失败由 applyListing 报告。
         * 副作用：复位忙碌态，更新状态和列表，发 changed。
         * 线程与生命周期：Qt GUI 线程排队执行，对象已销毁则放弃通知。
         */
        QMetaObject::invokeMethod(self, [self, created = std::move(created), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!created.ok()) self->error_text_ = view_model_text::errorText(*created.error);
                else self->status_text_ = tr("已创建 %1 个可恢复步骤").arg(created.value->total_steps);
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::cancelJob(QString job_id, int revision) {
    if (running_ && job_id == active_job_id_ && world_id_ == active_world_id_) {
        run_control_->action.store(xuyan::application::OfflineBatchAction::cancel);
        status_text_ = tr("正在等待当前片段结束后取消；已提交结果保留");
        emit changed();
        return;
    }
    if (busy_ || world_id_.isEmpty() || !visibleJob(jobs_, job_id)) return;
    const auto operation_world = world_id_;
    const auto world_generation = world_generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    /*
     * 功能：按任务当前预期修订持久化取消，并读取原操作世界的任务目录。
     * 参数：闭包按值持有路径、任务 ID、预期修订、命令 ID、世界及代次；self 为弱引用。
     * 返回：无；取消与目录 Result 经排队回调交付。
     * 失败：修订冲突、存储错误或异常返回失败，不假装取消已落库。
     * 副作用：服务可能写取消状态；不发送模型请求。
     * 线程与生命周期：全局线程池运行，提交可能在界面销毁后完成。
     */
    QThreadPool::globalInstance()->start([self, path, job_id, revision, command, operation_world, world_generation] {
        xuyan::domain::Result<xuyan::domain::ExtractionJob> changed;
        auto listed = xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>>::failure(
            {xuyan::domain::ErrorCode::storage_error, "任务目录未能刷新", true, "刷新任务后检查实际状态"});
        try {
            xuyan::application::ExtractionJobService service(path);
            changed = service.cancel(command, job_id.toStdString(), revision);
            listed = service.listForWorld(operation_world.toStdString());
        } catch (...) { changed = decltype(changed)::failure({xuyan::domain::ErrorCode::storage_error,
            "取消任务或刷新发生内部错误", true, "刷新任务并检查取消状态"}); }
        if (!self) return;
        /*
         * 功能：仅在原世界仍被选中时显示取消落库结果与更新后目录。
         * 参数：捕获弱引用、拥有的取消/目录 Result、世界及代次。
         * 返回：无；旧世界结果不回填。
         * 失败：取消或刷新错误分别由错误状态及 applyListing 呈现。
         * 副作用：复位忙碌态、更新列表及状态并发 changed。
         * 线程与生命周期：Qt 排队到 GUI 线程，对象销毁则放弃通知。
         */
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!changed.ok()) self->error_text_ = view_model_text::errorText(*changed.error);
                else self->status_text_ = tr("取消请求已持久化");
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
    /*
     * 功能：仅重排作者指定的失败或未知步骤，不重跑已经完成的片段。
     * 参数：闭包按值持有路径、任务 ID、一基步骤序号、预期尝试次数、命令 ID、世界及代次；self 为弱引用。
     * 返回：无；重排与目录 Result 经 GUI 回调交付。
     * 失败：状态、尝试次数、修订或存储不符时返回失败；异常转中文错误。
     * 副作用：可能更新任务步骤和命令记录，但本回调不执行模型请求。
     * 线程与生命周期：全局线程池执行；对象销毁只丢弃通知，不撤销已提交状态。
     */
    QThreadPool::globalInstance()->start([self, path, job_id, ordinal, attempt, command, operation_world, world_generation] {
        xuyan::domain::Result<xuyan::domain::ExtractionJob> changed;
        auto listed = xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>>::failure(
            {xuyan::domain::ErrorCode::storage_error, "任务目录未能刷新", true, "刷新任务后检查实际状态"});
        try {
            xuyan::application::ExtractionJobService service(path);
            changed = service.retryStep(command, job_id.toStdString(), ordinal, attempt);
            listed = service.listForWorld(operation_world.toStdString());
        } catch (...) { changed = decltype(changed)::failure({xuyan::domain::ErrorCode::storage_error,
            "重排步骤或刷新发生内部错误", true, "刷新任务并检查步骤状态"}); }
        if (!self) return;
        /*
         * 功能：在原世界上下文中呈现重排结果并刷新任务摘要。
         * 参数：捕获弱引用、拥有的重排/目录 Result、操作世界和代次。
         * 返回：无；旧世界回调丢弃。
         * 失败：重排或目录错误以中文提示报告，不自动启动重排后的步骤。
         * 副作用：复位忙碌态、更新列表及提示，发 changed。
         * 线程与生命周期：Qt 排队到 GUI 线程，对象销毁后不访问其成员。
         */
        QMetaObject::invokeMethod(self, [self, changed = std::move(changed), listed = std::move(listed),
                                         operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!changed.ok()) self->error_text_ = view_model_text::errorText(*changed.error);
                else self->status_text_ = tr("失败/未知步骤已重新排队，不会重跑已完成步骤");
            }
            self->applyListing(std::move(listed));
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void ExtractionJobViewModel::runMock(QString job_id) {
    for (const auto& item : jobs_) {
        const auto job = item.toMap();
        if (job.value("id").toString() == job_id && job.value("providerConnectionId").toString().isEmpty())
            launchBatch(job_id, 100000, false);
    }
}

void ExtractionJobViewModel::runRemoteSample(QString job_id) {
    launchBatch(std::move(job_id), 1, true);
}

void ExtractionJobViewModel::startJob(QString job_id) {
    launchBatch(std::move(job_id), 100000, false);
}

void ExtractionJobViewModel::resumeJob(QString job_id) {
    startJob(std::move(job_id));
}

void ExtractionJobViewModel::pauseJob(QString job_id) {
    if (!running_ || job_id != active_job_id_ || world_id_ != active_world_id_) return;
    auto expected = xuyan::application::OfflineBatchAction::proceed;
    run_control_->action.compare_exchange_strong(expected, xuyan::application::OfflineBatchAction::pause);
    if (expected == xuyan::application::OfflineBatchAction::cancel) return;
    status_text_ = tr("正在等待当前片段结束后暂停；已提交结果保留");
    emit changed();
}

/*
 * 功能：仅由明确点击的入口启动任务批次，并以持久化任务状态再次核对世界及连接。
 * 参数：job_id 为稳定任务 ID；maximum_steps 为本次最多处理的片段数；require_remote 为是否必须绑定真实模型连接。
 * 返回：无；前置状态不满足时忽略，执行结果由 finishBatch 异步呈现。
 * 失败：来源/任务/连接不符、服务异常或批次失败转为安全中文结果，不自动补发未知片段。
 * 副作用：设置本进程运行态，在自有线程池运行处理器；显式远程任务可能发送并计费，逐片段保留检查点。
 * 线程与生命周期：GUI 发起；工作线程可借用 this，因为析构先发停止请求并 waitForDone；GUI 回调按会话代次过滤。
 */
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
    error_text_.clear(); status_text_ = tr("正在解析；每个片段独立保存检查点");
    overlayProgress(); emit changed();
    // 自有线程池在析构时等待；线程不会在视图模型销毁后继续访问成员或发布回调。
    /*
     * 功能：在专属线程读取冻结任务身份、执行批次并结算取消意图。
     * 参数：闭包按值持有路径、任务/世界/会话、共享控制对象、执行器、步数上限及预期连接；this 仅由专属线程池借用。
     * 返回：无；最终 Result 排队交给 finishBatch。
     * 失败：身份变化或服务/处理器异常形成安全错误；已提交的片段检查点不回滚。
     * 副作用：可能写步骤、候选、请求用量与取消状态；远程执行器可发送模型请求。
     * 线程与生命周期：worker_pool_ 单线程执行；析构先停止并等待池结束，不能在闭包结束前销毁 this。
     */
    worker_pool_.start([this, path, job_id, operation_world, session, control, runner, maximum_steps, require_remote, expected_provider] {
        using namespace xuyan::application;
        using Result = xuyan::domain::Result<OfflineBatchResult>;
        auto result = Result::failure({xuyan::domain::ErrorCode::storage_error,
            "后台解析未能执行；已保存的检查点仍保留", true, "检查任务后继续"});
        try {
            auto job = ExtractionJobService(path).load(job_id.toStdString());
            if (!job.ok()) result = Result::failure(*job.error);
            else {
                auto source = xuyan::application::SourceImportService(path).load(job.value->source_id);
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
                    /*
                     * 功能：把已提交步骤转换为章节进度，并返回最新暂停/取消意图。
                     * 参数：progress 为处理器调用期间借用的进度；chapters 是本批次栈上计数器，只在 runner 同步调用期间借用。
                     * 返回：原子控制对象中的继续、暂停或取消动作。
                     * 失败：GUI 投递失败不改变已持久化步骤；异常由处理器调用方捕获。
                     * 副作用：更新本批次章节计数，向 GUI 队列投递展示进度，不直接修改 GUI 成员。
                     * 线程与生命周期：运行于批次工作线程；回调不得在 runner 返回后继续持有 chapters 引用。
                     */
                    options.on_progress = [this, control, session, operation_world, job_id, &chapters, initial_requests](const OfflineBatchProgress& progress) {
                        const auto completed_chapters = chapters.advance(progress.completed_steps);
                        const auto total_chapters = chapters.total();
                        /*
                         * 功能：把本批次进度回填到任务卡片，只为当前世界发可见更新。
                         * 参数：捕获 this、会话、世界、任务、进度副本、章节数和初始请求数；无调用参数。
                         * 返回：无；旧会话或批次已结束时直接丢弃。
                         * 失败：值容器分配异常由 Qt 调用链处理，不影响持久化检查点。
                         * 副作用：更新 progress_，当前世界再叠加卡片并发 changed。
                         * 线程与生命周期：Qt 排队在 GUI 线程运行；视图模型析构时其事件队列随对象失效。
                         */
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
        /*
         * 功能：将批次终态交回 GUI 线程，统一处理状态、取消竞态和目录刷新。
         * 参数：捕获 this、拥有的批次 Result、会话、任务 ID 与操作世界。
         * 返回：无。
         * 失败：错误 Result 由 finishBatch 显示，不在回调中重发请求。
         * 副作用：调用 finishBatch 更新本进程运行态和界面状态。
         * 线程与生命周期：Qt 排队至 GUI 对象；析构先等待工作线程，不执行被销毁对象的事件回调。
         */
        QMetaObject::invokeMethod(this, [this, result = std::move(result), session, job_id, operation_world]() mutable {
            finishBatch(std::move(result), session, job_id, operation_world);
        }, Qt::QueuedConnection);
    });
}

/*
 * 功能：在 GUI 线程结算一次批次；若末尾收到取消，先补持久化再释放运行态。
 * 参数：result 为拥有的批次结果；session 为批次代次；job_id 为任务 ID；operation_world 为启动世界；
 *   cancellation_settled 为是否已完成末尾取消补偿，默认 false，避免重复发起补偿。
 * 返回：无；旧会话结果直接丢弃。
 * 失败：批次或补偿错误显示安全中文提示，保留已有检查点，不自动重试未知调用。
 * 副作用：可能启动一次取消补偿写入；最终清运行态、更新状态和当前世界目录。
 * 线程与生命周期：GUI 线程执行；补偿线程属于自有池，析构等待其结束。
 */
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
        /*
         * 功能：关闭批次终结回调与用户点击取消之间的窗口，持久化最后的取消意图。
         * 参数：闭包按值持有路径、会话、任务与操作世界；this 在自有线程池存活期内借用。
         * 返回：无；补偿结果排队再次交 finishBatch，且标记已结算。
         * 失败：服务或存储异常保留预设错误，不把错误当作取消成功。
         * 副作用：可能写任务取消状态和命令记录，不发模型请求。
         * 线程与生命周期：worker_pool_ 执行，析构等待结束；不从工作线程直接更新 GUI。
         */
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
            /*
             * 功能：将末尾取消补偿结果交回 GUI，完成唯一一次最终结算。
             * 参数：捕获 this、拥有的补偿 Result、会话、任务及世界；无调用参数。
             * 返回：无；会话失效由 finishBatch 丢弃。
             * 失败：补偿失败在最终结算时显示错误，不自动重新取消。
             * 副作用：更新运行态、提示和目录，必要时发 changed。
             * 线程与生命周期：Qt 排队到 GUI 线程；对象销毁时未执行事件不会运行。
             */
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
            error_text_ = view_model_text::errorText(*result.error);
            retain_error_on_refresh_ = true;
        } else {
            switch (result.value->reason) {
            case OfflineBatchStopReason::completed: status_text_ = tr("解析完成；候选已进入人工校对，未自动写入世界"); break;
            case OfflineBatchStopReason::paused: status_text_ = tr("已暂停；可以从检查点继续"); break;
            case OfflineBatchStopReason::cancelled: status_text_ = tr("已取消剩余片段；已提交候选保留"); break;
            case OfflineBatchStopReason::step_limit: status_text_ = tr("本批次结束；可继续处理剩余片段"); break;
            default: status_text_ = tr("解析已停止，请核对问题片段或调用上限；不会自动重发"); break;
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
    busy_ = true; error_text_.clear(); status_text_ = tr("正在本地复核候选证据样本…"); emit changed();
    QPointer<ExtractionJobViewModel> self(this); const auto path = database_path_;
    /*
     * 功能：在工作线程读取指定任务的本地证据质量抽样报告。
     * 参数：闭包按值持有路径、任务 ID、操作世界和代次；self 是弱引用。
     * 返回：无；拥有的报告 Result 排队传回 GUI。
     * 失败：报告服务异常转安全中文错误，对象销毁时丢弃通知。
     * 副作用：只读候选和证据，不发送模型请求、不修改候选审核状态。
     * 线程与生命周期：全局线程池执行，闭包不借用 GUI 对象成员。
     */
    QThreadPool::globalInstance()->start([self, path, job_id, operation_world, world_generation] {
        xuyan::domain::Result<xuyan::domain::ExtractionQualityReport> report;
        try { report = xuyan::application::ExtractionJobService(path).qualityReport(job_id.toStdString(), 100); }
        catch (...) { report = decltype(report)::failure({xuyan::domain::ErrorCode::storage_error,
            "候选证据复核失败", true, "检查工作区后重新复核"}); }
        if (!self) return;
        /*
         * 功能：仅在世界身份未改变时显示证据抽样数量，不把抽样结果写成模型精确率。
         * 参数：捕获弱引用、拥有的报告 Result、操作世界及代次。
         * 返回：无；过期世界结果不回填。
         * 失败：服务错误映射为中文提示，报告空结果仍按实际零计数呈现。
         * 副作用：复位忙碌态、更新状态或错误、发 changed 并兑现延迟刷新。
         * 线程与生命周期：Qt 排队到 GUI 线程，对象消失后不访问成员。
         */
        QMetaObject::invokeMethod(self, [self, report = std::move(report), operation_world, world_generation]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (self->world_id_ != operation_world || self->world_generation_ != world_generation) {
                self->refreshAfterWorldChange();
                return;
            }
            if (self->world_id_ == operation_world) {
                if (!report.ok()) self->error_text_ = view_model_text::errorText(*report.error);
                else self->status_text_ = tr("抽样 %1 项：证据可定位 %2，已接受 %3，已拒绝 %4，待校对 %5；模型精确率/召回率尚未实测")
                    .arg(report.value->sampled_candidates).arg(report.value->evidence_valid).arg(report.value->accepted)
                    .arg(report.value->rejected).arg(report.value->unresolved);
            }
            emit self->changed();
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}
