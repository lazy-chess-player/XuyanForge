#include "candidate_review_view_model.h"

#include "xuyan/application/candidate_service.h"
#include "xuyan/storage/workspace_repository.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <utility>

namespace {
/** @brief 将领域层 UTF-8 字符串转换为界面文本。 */
QString text(const std::string& value) { return QString::fromStdString(value); }
}

QVariantList CandidateReviewViewModel::maps(const std::vector<xuyan::domain::ExtractionCandidate>& candidates) {
    QVariantList result;
    for (const auto& value : candidates) {
        result.push_back(QVariantMap{{"id", text(value.id)}, {"type", text(value.candidate_type)},
            {"name", text(value.name)}, {"source", text(value.source_id)}, {"quote", text(value.quote)},
            {"provenance", text(value.provenance_type)}, {"reviewStatus", text(value.review_status)},
            {"revision", value.revision}, {"start", static_cast<qlonglong>(value.start_codepoint)},
            {"end", static_cast<qlonglong>(value.end_codepoint)}});
    }
    return result;
}

CandidateReviewViewModel::CandidateReviewViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {}

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

void CandidateReviewViewModel::load(QString keep_id) {
    if (busy_ || world_id_.isEmpty()) return;
    const auto world_id = world_id_.toStdString();
    const auto generation = ++request_generation_;
    const auto page_index = page_index_;
    QPointer<CandidateReviewViewModel> self(this);
    busy_ = true; error_text_.clear(); emit changed();
    const auto path = database_path_;
    const auto status = filter_.toStdString();
    QThreadPool::globalInstance()->start([self, path, status, world_id, generation, page_index, keep_id] {
        QString error;
        xuyan::domain::ExtractionCandidatePage page;
        try {
            xuyan::application::CandidateService service(path);
            auto result = service.listPage(world_id, {}, status, page_size_, page_index * page_size_);
            if (!result.ok()) error = text(result.error->message);
            else page = std::move(*result.value);
        } catch (const std::exception&) {
            error = QStringLiteral("读取候选失败，请检查工作区后重试");
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, error, page = std::move(page), keep_id]() mutable {
            // 世界、筛选或页码变化都会使旧请求失效，包括 A→B→A 的情况。
            if (!self || generation != self->request_generation_) return;
            self->busy_ = false;
            if (!error.isEmpty()) {
                self->error_text_ = error;
                self->status_text_ = QStringLiteral("当前世界候选读取失败");
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
                self->status_text_ = QStringLiteral("当前世界共 %1 项候选；本页显示 %2 项")
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
    status_text_ = world_id_.isEmpty() ? QStringLiteral("请选择世界以查看待校对候选")
                                        : QStringLiteral("正在读取当前世界的候选…");
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
    status_text_ = QStringLiteral("正在原子提交审核结果…"); emit changed();
    const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto review_status = status.toStdString(); const auto edited_name = name.toStdString();
    const auto edited_fields = fields_json.toStdString(); const auto provenance = provenance_type.toStdString();
    QThreadPool::globalInstance()->start([self, path, command, candidate_id, expected_revision,
                                         world_id, generation, review_status, edited_name, edited_fields, provenance] {
        xuyan::domain::Result<xuyan::domain::ExtractionCandidate> result;
        try {
            xuyan::storage::WorkspaceRepository repository(path);
            auto current = repository.loadExtractionCandidate(candidate_id);
            if (!current.ok()) result = decltype(result)::failure(*current.error);
            else {
                auto source = repository.loadSource(current.value->source_id);
                if (!source.ok()) result = decltype(result)::failure(*source.error);
                else if (source.value->world_id != world_id) result = decltype(result)::failure(
                    {xuyan::domain::ErrorCode::revision_conflict, "候选来源不属于当前世界", false, "刷新候选后重试"});
                else {
                    xuyan::application::CandidateService service(path);
                    result = service.review(command, candidate_id, expected_revision, review_status,
                                            edited_name, edited_fields, provenance);
                }
            }
        } catch (const std::exception&) {
            result = decltype(result)::failure(
                {xuyan::domain::ErrorCode::storage_error, "审核候选失败，请检查工作区后重试", true, "刷新候选后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), review_status, generation]() mutable {
            if (!self || generation != self->request_generation_) return;
            self->busy_ = false; self->reviewing_ = false;
            if (!result.ok()) { self->error_text_ = text(result.error->message); emit self->changed(); return; }
            self->status_text_ = review_status == "accepted" ? QStringLiteral("候选、世界条目与证据已在同一事务中提交")
                : review_status == "rejected" ? QStringLiteral("候选已拒绝并保留审核修订")
                : review_status == "conflicted" ? QStringLiteral("候选已转入冲突队列") : QStringLiteral("候选编辑已保存");
            if (review_status == "accepted") emit self->candidateAccepted();
            self->load();
        }, Qt::QueuedConnection);
    });
}
