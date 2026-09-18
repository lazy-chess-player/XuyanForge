#include "source_view_model.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/application/evidence_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <algorithm>

SourceViewModel::SourceViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {
    refresh();
}

QString SourceViewModel::selectedHash() const {
    return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].sha256) : QString{};
}

void SourceViewModel::refresh() {
    if (busy_) return;
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.list();
        } catch (const std::exception& exception) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (self) self->applyDocuments(std::move(result));
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::applyDocuments(
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result, QString keep_id) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = QString::fromStdString(result.error->message);
        emit changed();
        return;
    }
    documents_ = std::move(*result.value);
    source_items_.clear();
    selected_index_ = -1;
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        const auto& document = documents_[index];
        source_items_.push_back(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(document.name)},
            {QStringLiteral("id"), QString::fromStdString(document.id)},
            {QStringLiteral("chapters"), static_cast<int>(document.chapters.size())},
            {QStringLiteral("encoding"), QString::fromStdString(document.detected_encoding)},
        });
        if (QString::fromStdString(document.id) == keep_id) selected_index_ = static_cast<int>(index);
    }
    if (selected_index_ < 0 && !documents_.empty()) selected_index_ = 0;
    if (selected_index_ >= 0) selectSource(selected_index_);
    else emit changed();
}

void SourceViewModel::importFile(const QUrl& file_url) {
    if (busy_ || !file_url.isLocalFile()) return;
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    const auto file = file_url.toLocalFile().toStdWString();
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, file, command] {
        xuyan::domain::Result<xuyan::domain::SourceDocument> imported;
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> documents;
        try {
            xuyan::application::SourceImportService service(path);
            imported = service.importTextFile(command, std::filesystem::path(file));
            if (imported.ok()) documents = service.list();
        } catch (const std::exception& exception) {
            imported = decltype(imported)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                    "检查文件与工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, imported = std::move(imported), documents = std::move(documents)]() mutable {
            if (!self) return;
            if (!imported.ok()) {
                self->busy_ = false;
                self->error_text_ = QString::fromStdString(imported.error->message);
                emit self->changed();
                return;
            }
            const auto keep = QString::fromStdString(imported.value->id);
            self->applyDocuments(std::move(documents), keep);
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectSource(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) return;
    selected_index_ = index;
    chapter_items_.clear();
    evidence_.clear(); evidence_items_.clear(); selected_chapter_index_ = -1;
    for (const auto& chapter : documents_[index].chapters) {
        chapter_items_.push_back(QVariantMap{
            {QStringLiteral("title"), QString::fromStdString(chapter.title)},
            {QStringLiteral("ordinal"), chapter.ordinal},
            {QStringLiteral("start"), static_cast<qlonglong>(chapter.start_codepoint)},
            {QStringLiteral("end"), static_cast<qlonglong>(chapter.end_codepoint)},
        });
    }
    preview_text_ = QStringLiteral("正在读取标准化文本…");
    emit changed();
    loadPreview(documents_[index].id);
}

void SourceViewModel::loadPreview(const std::string& source_id) {
    const auto path = database_path_;
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, source_id] {
        xuyan::application::SourceImportService service(path);
        auto result = service.loadNormalizedText(source_id);
        xuyan::application::EvidenceService evidence_service(path);
        auto evidence = evidence_service.listForSource(source_id);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, result = std::move(result), evidence = std::move(evidence)]() mutable {
            if (!self || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id) return;
            if (!result.ok()) self->error_text_ = QString::fromStdString(result.error->message);
            else {
                constexpr std::size_t preview_limit = 200000;
                const auto preview = result.value->substr(0, preview_limit);
                self->preview_text_ = QString::fromUtf8(preview.data(), static_cast<qsizetype>(preview.size()));
                if (result.value->size() > preview_limit) self->preview_text_ += QStringLiteral("\n\n……预览已截断……");
            }
            self->evidence_.clear(); self->evidence_items_.clear();
            if (evidence.ok()) {
                self->evidence_ = std::move(*evidence.value);
                for (const auto& item : self->evidence_) self->evidence_items_.push_back(QVariantMap{
                    {"entityId", QString::fromStdString(item.entity_id)}, {"field", QString::fromStdString(item.field_path)},
                    {"start", static_cast<qlonglong>(item.start_codepoint)}, {"end", static_cast<qlonglong>(item.end_codepoint)},
                    {"quote", QString::fromStdString(item.quote)}, {"provenance", QString::fromStdString(item.provenance_type)}});
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::createEvidence(QString entity_id, QString field_path, int selection_start,
                                     int selection_end, QString provenance_type) {
    if (busy_ || selected_index_ < 0) return;
    if (selection_start < 0 || selection_end <= selection_start || selection_end > preview_text_.size()) {
        error_text_ = QStringLiteral("请先在原文预览中选择一段非空文本"); emit changed(); return;
    }
    const auto start_cp = static_cast<std::size_t>(preview_text_.left(selection_start).toUcs4().size());
    const auto end_cp = static_cast<std::size_t>(preview_text_.left(selection_end).toUcs4().size());
    const auto source_id = documents_[selected_index_].id;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在校验证据区间…"); emit changed();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, source_id, entity_id, field_path, start_cp, end_cp, provenance_type] {
        xuyan::application::EvidenceService service(path);
        auto created = service.create(command, entity_id.toStdString(), field_path.toStdString(), source_id,
                                      start_cp, end_cp, provenance_type.toStdString());
        auto listed = service.listForSource(source_id);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, created = std::move(created), listed = std::move(listed)]() mutable {
            if (!self || self->selected_index_ < 0 || self->documents_[self->selected_index_].id != source_id) return;
            self->busy_ = false;
            if (!created.ok()) self->error_text_ = QString::fromStdString(created.error->message);
            else self->status_text_ = QStringLiteral("证据已关联到 %1.%2").arg(
                QString::fromStdString(created.value->entity_id), QString::fromStdString(created.value->field_path));
            self->evidence_.clear(); self->evidence_items_.clear();
            if (listed.ok()) for (const auto& item : *listed.value) {
                self->evidence_.push_back(item);
                self->evidence_items_.push_back(QVariantMap{{"entityId", QString::fromStdString(item.entity_id)},
                    {"field", QString::fromStdString(item.field_path)}, {"start", static_cast<qlonglong>(item.start_codepoint)},
                    {"end", static_cast<qlonglong>(item.end_codepoint)}, {"quote", QString::fromStdString(item.quote)},
                    {"provenance", QString::fromStdString(item.provenance_type)}});
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectEvidence(int index) {
    if (index < 0 || index >= static_cast<int>(evidence_.size())) return;
    const auto& item = evidence_[static_cast<std::size_t>(index)];
    const auto utf16ForCodepoint = [this](std::size_t wanted) {
        qsizetype offset = 0; std::size_t count = 0;
        while (offset < preview_text_.size() && count < wanted) {
            if (preview_text_.at(offset).isHighSurrogate() && offset + 1 < preview_text_.size()
                && preview_text_.at(offset + 1).isLowSurrogate()) offset += 2;
            else ++offset;
            ++count;
        }
        return static_cast<int>(offset);
    };
    highlight_start_ = utf16ForCodepoint(item.start_codepoint);
    highlight_end_ = utf16ForCodepoint(item.end_codepoint);
    emit changed();
}

void SourceViewModel::selectChapter(int index) {
    if (selected_index_ < 0 || index < 0 || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    selected_chapter_index_ = index;
    const auto& chapter = documents_[selected_index_].chapters[static_cast<std::size_t>(index)];
    const auto utf16ForCodepoint = [this](std::size_t wanted) {
        qsizetype offset = 0; std::size_t count = 0;
        while (offset < preview_text_.size() && count < wanted) {
            offset += preview_text_.at(offset).isHighSurrogate() && offset + 1 < preview_text_.size()
                && preview_text_.at(offset + 1).isLowSurrogate() ? 2 : 1;
            ++count;
        }
        return static_cast<int>(offset);
    };
    highlight_start_ = utf16ForCodepoint(chapter.start_codepoint);
    highlight_end_ = utf16ForCodepoint(chapter.end_codepoint);
    emit changed();
}

void SourceViewModel::saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint) {
    if (busy_ || selected_index_ < 0 || index < 0
        || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    auto chapters = documents_[selected_index_].chapters;
    auto& edited = chapters[static_cast<std::size_t>(index)];
    edited.title = title.trimmed().toStdString();
    edited.start_codepoint = start_codepoint < 0 ? 0 : static_cast<std::size_t>(start_codepoint);
    edited.end_codepoint = end_codepoint < 0 ? 0 : static_cast<std::size_t>(end_codepoint);
    const auto source_id = documents_[selected_index_].id; const auto expected = documents_[selected_index_].chapter_revision;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在校验章节布局…"); emit changed();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, source_id, expected, chapters = std::move(chapters)]() mutable {
        xuyan::application::SourceImportService service(path);
        auto result = service.saveChapters(command, source_id, expected, std::move(chapters));
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, result = std::move(result)]() mutable {
            if (!self || self->selected_index_ < 0 || self->documents_[self->selected_index_].id != source_id) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = QString::fromStdString(result.error->message); emit self->changed(); return; }
            self->documents_[self->selected_index_] = std::move(*result.value);
            self->status_text_ = QStringLiteral("章节布局已保存为修订 %1").arg(self->documents_[self->selected_index_].chapter_revision);
            self->selectSource(self->selected_index_);
        }, Qt::QueuedConnection);
    });
}
