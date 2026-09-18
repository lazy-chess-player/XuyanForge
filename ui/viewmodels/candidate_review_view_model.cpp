#include "candidate_review_view_model.h"

#include "xuyan/application/candidate_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

namespace {
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
    : QObject(parent), database_path_(std::move(database_path)) { load(); }

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
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<CandidateReviewViewModel> self(this); const auto path = database_path_; const auto status = filter_.toStdString();
    QThreadPool::globalInstance()->start([self, path, status, keep_id] {
        xuyan::application::CandidateService service(path); auto result = service.list(status);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), keep_id]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = text(result.error->message);
            else {
                self->candidates_ = std::move(*result.value); self->items_ = maps(self->candidates_); self->selected_index_ = -1;
                for (int index = 0; index < static_cast<int>(self->candidates_.size()); ++index)
                    if (text(self->candidates_[static_cast<std::size_t>(index)].id) == keep_id) { self->selected_index_ = index; break; }
                if (self->selected_index_ < 0 && !self->candidates_.empty()) self->selected_index_ = 0;
                self->status_text_ = QStringLiteral("当前队列 %1 项；引文均已按 Unicode 码点复核").arg(self->candidates_.size());
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void CandidateReviewViewModel::refresh() { load(selectedId()); }

void CandidateReviewViewModel::setFilter(QString status) {
    if (busy_ || status == filter_) return;
    filter_ = std::move(status); selected_index_ = -1; emit changed(); load();
}

void CandidateReviewViewModel::selectCandidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates_.size()) || index == selected_index_) return;
    selected_index_ = index; emit changed();
}

void CandidateReviewViewModel::reviewSelected(QString status, QString name, QString fields_json, QString provenance_type) {
    const auto* value = selected();
    if (busy_ || value == nullptr) return;
    const auto candidate_id = value->id; const auto expected_revision = value->revision;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在原子提交审核结果…"); emit changed();
    QPointer<CandidateReviewViewModel> self(this); const auto path = database_path_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto review_status = status.toStdString(); const auto edited_name = name.toStdString();
    const auto edited_fields = fields_json.toStdString(); const auto provenance = provenance_type.toStdString();
    QThreadPool::globalInstance()->start([self, path, command, candidate_id, expected_revision, review_status,
                                         edited_name, edited_fields, provenance] {
        xuyan::application::CandidateService service(path);
        auto result = service.review(command, candidate_id, expected_revision, review_status,
                                     edited_name, edited_fields, provenance);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), review_status]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = text(result.error->message); emit self->changed(); return; }
            self->status_text_ = review_status == "accepted" ? QStringLiteral("候选、世界条目与证据已在同一事务中提交")
                : review_status == "rejected" ? QStringLiteral("候选已拒绝并保留审核修订")
                : review_status == "conflicted" ? QStringLiteral("候选已转入冲突队列") : QStringLiteral("候选编辑已保存");
            if (review_status == "accepted") emit self->candidateAccepted();
            self->load();
        }, Qt::QueuedConnection);
    });
}
