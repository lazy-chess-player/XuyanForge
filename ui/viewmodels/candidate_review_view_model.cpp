#include "candidate_review_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/application/source_import_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <utility>

namespace {
/*
 * 功能：将领域对象中的 UTF-8 字符串转换为 Qt 界面字符串。
 * 参数：value 为本次调用期间借用的 UTF-8 文本；允许空串。
 * 返回：拥有自身存储的 QString，空输入返回空串。
 * 失败：内存分配异常可传播；不校验输入是否为有效 UTF-8。
 * 副作用：只分配返回值，不读写数据库或界面状态。
 */
QString text(const std::string& value) { return QString::fromStdString(value); }
}

/*
 * 功能：把当前页候选转为 QML 所需的轻量字段，保留原始协议值并另附中文审核状态。
 * 参数：candidates 为调用期间借用的当前页候选；列表可空，位置单位为原文 Unicode 码点。
 * 返回：与输入同序的独立列表；空输入返回空列表。
 * 失败：字符串或容器分配异常可传播，不吞掉无效候选。
 * 副作用：只构造界面值，不修改候选或数据库。
 */
QVariantList CandidateReviewViewModel::maps(const std::vector<xuyan::domain::ExtractionCandidate>& candidates) {
    QVariantList result;
    for (const auto& value : candidates) {
        result.push_back(QVariantMap{{"id", text(value.id)}, {"type", text(value.candidate_type)},
            {"name", text(value.name)}, {"source", text(value.source_id)}, {"quote", text(value.quote)},
            {"provenance", text(value.provenance_type)}, {"reviewStatus", text(value.review_status)},
            {"reviewStatusLabel", view_model_text::stateLabel(value.review_status)},
            {"revision", value.revision}, {"start", static_cast<qlonglong>(value.start_codepoint)},
            {"end", static_cast<qlonglong>(value.end_codepoint)}});
    }
    return result;
}

CandidateReviewViewModel::CandidateReviewViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {}

/*
 * 功能：在当前页中定位已选候选，供各只读属性及审核入口共享。
 * 参数：无。
 * 返回：有效选择时返回借用指针，仅在候选列表下一次变更前有效；无选择或越界返回空指针。
 * 失败：不抛异常，不访问数据库。
 * 副作用：只读当前 GUI 线程的候选快照。
 */
const xuyan::domain::ExtractionCandidate* CandidateReviewViewModel::selected() const noexcept {
    return selected_index_ >= 0 && selected_index_ < static_cast<int>(candidates_.size())
        ? &candidates_[static_cast<std::size_t>(selected_index_)] : nullptr;
}

QString CandidateReviewViewModel::selectedId() const { const auto* v = selected(); return v ? text(v->id) : QString{}; }
QString CandidateReviewViewModel::selectedType() const { const auto* v = selected(); return v ? text(v->candidate_type) : QString{}; }
QString CandidateReviewViewModel::selectedName() const { const auto* v = selected(); return v ? text(v->name) : QString{}; }
QString CandidateReviewViewModel::selectedFields() const { const auto* v = selected(); return v ? text(v->fields_json) : QString{}; }
QString CandidateReviewViewModel::selectedQuote() const { const auto* v = selected(); return v ? text(v->quote) : QString{}; }
QString CandidateReviewViewModel::selectedProvenance() const { const auto* v = selected(); return v ? text(v->provenance_type) : QString{}; }
QString CandidateReviewViewModel::selectedSource() const { const auto* v = selected(); return v ? text(v->source_id) : QString{}; }
QString CandidateReviewViewModel::selectedRange() const {
    const auto* v = selected();
    return v ? QStringLiteral("%1–%2").arg(v->start_codepoint).arg(v->end_codepoint) : QString{};
}
int CandidateReviewViewModel::selectedRevision() const noexcept { const auto* v = selected(); return v ? v->revision : 0; }

/*
 * 功能：异步读取当前世界及筛选条件对应的一页候选，并尽量恢复指定候选的选择。
 * 参数：keep_id 为按值持有的稳定候选 ID；空串表示只选择本页首项，若有项的话。
 * 返回：无；读取结果通过 changed、errorText 和当前页属性通知。
 * 失败：忙碌或未选世界时忽略；服务错误和异常转为中文提示，不伪造成功空页。
 * 副作用：设置忙碌态，在线程池读取元数据；GUI 回调只应用匹配请求代次的结果。
 * 线程与生命周期：后台仅捕获路径和值及 QPointer；Qt 排队回调在 GUI 线程执行，对象销毁后放弃通知。
 */
void CandidateReviewViewModel::load(QString keep_id) {
    if (busy_ || world_id_.isEmpty()) return;
    const auto world_id = world_id_.toStdString();
    const auto generation = ++request_generation_;
    const auto page_index = page_index_;
    QPointer<CandidateReviewViewModel> self(this);
    busy_ = true; error_text_.clear(); emit changed();
    const auto path = database_path_;
    const auto status = filter_.toStdString();
    /*
     * 功能：在工作线程按固定页号读取候选，避免在 GUI 线程查询大世界。
     * 参数：闭包按值持有路径、筛选、世界、代次、页号及保留选择 ID；self 是非拥有式弱引用。
     * 返回：无；通过排队回调传回页结果或安全错误文本。
     * 失败：服务失败及异常转中文错误；对象已销毁则丢弃通知。
     * 副作用：只读工作区，向 GUI 事件队列投递回调；不发送模型请求。
     * 线程与生命周期：全局线程池执行，闭包不借用视图模型成员。
     */
    QThreadPool::globalInstance()->start([self, path, status, world_id, generation, page_index, keep_id] {
        QString error;
        xuyan::domain::ExtractionCandidatePage page;
        try {
            xuyan::application::CandidateService service(path);
            auto result = service.listPage(world_id, {}, status, page_size_, page_index * page_size_);
            if (!result.ok()) error = view_model_text::errorText(*result.error);
            else page = std::move(*result.value);
        } catch (...) {
            error = tr("读取候选失败，请检查工作区后重试");
        }
        if (!self) return;
        /*
         * 功能：在 GUI 线程应用当前代次的候选页，并在末页变空时重新定位页号。
         * 参数：按值捕获弱引用、代次、错误、页值及保留选择 ID；页值由闭包独占。
         * 返回：无；过期或对象已销毁时直接放弃。
         * 失败：错误写入界面状态；内存分配异常由 Qt 调用链处理。
         * 副作用：更新页数、候选与选择，发 changed；末页回退会再发一次异步读取。
         * 线程与生命周期：QueuedConnection 保证在 GUI 事件循环中运行。
         */
        QMetaObject::invokeMethod(self, [self, generation, error, page = std::move(page), keep_id]() mutable {
            // 世界、筛选或页码变化都会使旧请求失效，包括 A→B→A 的情况。
            if (!self || generation != self->request_generation_) return;
            self->busy_ = false;
            if (!error.isEmpty()) {
                self->error_text_ = error;
                self->status_text_ = tr("当前世界候选读取失败");
            }
            else {
                self->total_count_ = static_cast<qlonglong>(page.total);
                if (page.items.empty() && self->page_index_ > 0 && page.total > 0) {
                    // 最后一页被审核或删除后，退回仍有内容的末页。
                    self->page_index_ = (page.total - 1) / page_size_;
                    self->load();
                    return;
                }
                if (page.total == 0) self->page_index_ = 0;
                self->candidates_ = std::move(page.items); self->items_ = maps(self->candidates_); self->selected_index_ = -1;
                for (int index = 0; index < static_cast<int>(self->candidates_.size()); ++index)
                    if (text(self->candidates_[static_cast<std::size_t>(index)].id) == keep_id) { self->selected_index_ = index; break; }
                if (self->selected_index_ < 0 && !self->candidates_.empty()) self->selected_index_ = 0;
                self->status_text_ = tr("当前世界共 %1 项候选；本页显示 %2 项")
                    .arg(self->total_count_).arg(self->candidates_.size());
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void CandidateReviewViewModel::refresh() { load(selectedId()); }

void CandidateReviewViewModel::setWorldId(QString world_id) {
    if (world_id == world_id_) return;
    world_id_ = std::move(world_id);
    ++request_generation_;
    candidates_.clear(); items_.clear(); selected_index_ = -1;
    page_index_ = 0; total_count_ = 0;
    busy_ = false; reviewing_ = false; error_text_.clear();
    status_text_ = world_id_.isEmpty() ? tr("请选择世界以查看待校对候选")
                                        : tr("正在读取当前世界的候选…");
    emit changed();
    load();
}

void CandidateReviewViewModel::setFilter(QString status) {
    if (reviewing_ || status == filter_) return;
    ++request_generation_;
    filter_ = std::move(status);
    candidates_.clear(); items_.clear(); selected_index_ = -1;
    page_index_ = 0; total_count_ = 0; busy_ = false;
    emit changed(); load();
}

void CandidateReviewViewModel::selectCandidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates_.size()) || index == selected_index_) return;
    selected_index_ = index; emit changed();
}

void CandidateReviewViewModel::previousPage() {
    if (!canPreviousPage()) return;
    --page_index_;
    candidates_.clear(); items_.clear(); selected_index_ = -1;
    emit changed(); load();
}

void CandidateReviewViewModel::nextPage() {
    if (!canNextPage()) return;
    ++page_index_;
    candidates_.clear(); items_.clear(); selected_index_ = -1;
    emit changed(); load();
}

void CandidateReviewViewModel::reviewSelected(QString status, QString name, QString fields_json, QString provenance_type) {
    const auto* value = selected();
    if (busy_ || value == nullptr || world_id_.isEmpty()) return;
    const auto candidate_id = value->id;
    const auto expected_revision = value->revision;
    const auto world_id = world_id_.toStdString();
    const auto generation = ++request_generation_;
    QPointer<CandidateReviewViewModel> self(this);
    busy_ = true; reviewing_ = true; error_text_.clear();
    status_text_ = tr("正在原子提交审核结果…"); emit changed();
    const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto review_status = status.toStdString(); const auto edited_name = name.toStdString();
    const auto edited_fields = fields_json.toStdString(); const auto provenance = provenance_type.toStdString();
    /* 审核目标、预期修订及世界身份必须一同捕获；切世界只使回调失效，不撤销已提交事务。 */
    /*
     * 功能：在工作线程复读候选及其来源，确认来源仍属于操作世界后提交审核。
     * 参数：闭包按值持有命令、候选、修订、世界及表单字段；self 为不拥有对象的弱引用。
     * 返回：无；审核结果以排队回调送回 GUI。
     * 失败：候选/来源读取、世界归属或修订不符时返回失败；异常转安全中文错误。
     * 副作用：成功时由候选服务提交审核事务，可能写入世界投影；不调用模型。
     * 线程与生命周期：全局线程池执行；对象销毁后写事务仍可能完成，但不访问已销毁界面。
     */
    QThreadPool::globalInstance()->start([self, path, command, candidate_id, expected_revision,
                                         world_id, generation, review_status, edited_name, edited_fields, provenance] {
        xuyan::domain::Result<xuyan::domain::ExtractionCandidate> result;
        try {
            xuyan::application::CandidateService service(path);
            auto current = service.load(candidate_id);
            if (!current.ok()) result = decltype(result)::failure(*current.error);
            else {
                auto source = xuyan::application::SourceImportService(path).load(current.value->source_id);
                if (!source.ok()) result = decltype(result)::failure(*source.error);
                else if (source.value->world_id != world_id) result = decltype(result)::failure(
                    {xuyan::domain::ErrorCode::revision_conflict, "候选来源不属于当前世界", false, "刷新候选后重试"});
                else {
                    result = service.review(command, candidate_id, expected_revision, review_status,
                                            edited_name, edited_fields, provenance);
                }
            }
        } catch (...) {
            result = decltype(result)::failure(
                {xuyan::domain::ErrorCode::storage_error, "审核候选失败，请检查工作区后重试", true, "刷新候选后重试"});
        }
        if (!self) return;
        /*
         * 功能：只在原审核页面仍对应相同请求代次时显示结果并刷新候选页。
         * 参数：捕获弱引用、审核结果、内部审核状态和请求代次；结果由闭包独占。
         * 返回：无；对象消失或页面代次变化时不修改界面。
         * 失败：服务错误映射为中文提示，成功时按审核状态提示；不重试提交。
         * 副作用：复位忙碌/审核状态，发 changed；接受时另发 candidateAccepted，成功后重新读页。
         * 线程与生命周期：通过 Qt 排队到 GUI 线程；旧世界的已落库结果不会回填到新世界。
         */
        QMetaObject::invokeMethod(self, [self, result = std::move(result), review_status, generation]() mutable {
            if (!self || generation != self->request_generation_) return;
            self->busy_ = false; self->reviewing_ = false;
            if (!result.ok()) { self->error_text_ = view_model_text::errorText(*result.error); emit self->changed(); return; }
            self->status_text_ = review_status == "accepted" ? tr("候选、世界条目与证据已在同一事务中提交")
                : review_status == "rejected" ? tr("候选已拒绝并保留审核修订")
                : review_status == "conflicted" ? tr("候选已转入冲突队列") : tr("候选编辑已保存");
            if (review_status == "accepted") emit self->candidateAccepted();
            self->load();
        }, Qt::QueuedConnection);
    });
}
