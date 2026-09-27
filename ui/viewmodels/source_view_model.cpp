#include "source_view_model.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/application/evidence_service.h"
#include "xuyan/storage/workspace_repository.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <algorithm>
#include <iterator>

SourceViewModel::SourceViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {}

/** @brief 切换世界时先清空旧世界内容；进行中的后台操作完成后再补刷新。 */
void SourceViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++world_generation_;
    ++preview_generation_;
    desired_source_id_.clear();
    documents_.clear(); source_items_.clear(); chapter_items_.clear();
    evidence_.clear(); evidence_items_.clear();
    selected_index_ = -1; selected_chapter_index_ = -1;
    preview_text_.clear(); preview_loading_ = false;
    pending_evidence_range_.reset();
    preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
    highlight_start_ = 0; highlight_end_ = 0;
    status_text_.clear(); error_text_.clear();
    if (busy_) refresh_after_world_change_ = true;
    else if (!world_id_.isEmpty()) refresh();
    emit changed();
}

QString SourceViewModel::selectedHash() const {
    return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].sha256) : QString{};
}

bool SourceViewModel::canPreviousWindow() const noexcept {
    if (selected_index_ < 0 || selected_chapter_index_ < 0) return false;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    return static_cast<std::size_t>(selected_chapter_index_) < chapters.size()
        && preview_start_codepoint_ > chapters[static_cast<std::size_t>(selected_chapter_index_)].start_codepoint;
}

bool SourceViewModel::canNextWindow() const noexcept {
    if (selected_index_ < 0 || selected_chapter_index_ < 0) return false;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    return static_cast<std::size_t>(selected_chapter_index_) < chapters.size()
        && preview_end_codepoint_ < chapters[static_cast<std::size_t>(selected_chapter_index_)].end_codepoint;
}

void SourceViewModel::refresh() {
    if (world_id_.isEmpty()) {
        ++preview_generation_;
        documents_.clear(); source_items_.clear(); chapter_items_.clear();
        selected_index_ = -1; selected_chapter_index_ = -1;
        preview_text_.clear(); evidence_.clear(); evidence_items_.clear();
        preview_loading_ = false; pending_evidence_range_.reset();
        preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
        emit changed();
        return;
    }
    if (busy_) { refresh_after_world_change_ = true; return; }
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    const auto requested_world = world_id_;
    const auto world_generation = world_generation_;
    const auto keep_id = desired_source_id_.isEmpty() ? selectedSourceId() : desired_source_id_;
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, keep_id, requested_world, world_generation] {
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.listForWorld(requested_world.toStdString());
        } catch (const std::exception& exception) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), keep_id, requested_world, world_generation]() mutable {
            if (!self) return;
            if (self->world_id_ != requested_world || self->world_generation_ != world_generation) {
                // 切换世界后的旧查询不能覆盖当前列表，转而读取新世界。
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            const auto selected_now = self->selectedSourceId();
            self->applyDocuments(std::move(result), self->desired_source_id_.isEmpty()
                ? (selected_now.isEmpty() ? keep_id : selected_now) : self->desired_source_id_);
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::applyDocuments(
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result, QString keep_id) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = QString::fromStdString(result.error->message);
        emit changed();
        refreshAfterWorldChange();
        return;
    }
    const auto previous_source_id = selectedSourceId();
    std::string previous_chapter_id;
    std::optional<std::size_t> previous_window_start;
    if (selected_index_ >= 0 && selected_chapter_index_ >= 0) {
        const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
        if (static_cast<std::size_t>(selected_chapter_index_) < chapters.size()) {
            previous_chapter_id = chapters[static_cast<std::size_t>(selected_chapter_index_)].id;
            previous_window_start = preview_start_codepoint_;
        }
    }
    documents_ = std::move(*result.value);
    // 存储层可列出整个工作区，这里只把当前世界的来源交给界面。
    std::erase_if(documents_, [this](const auto& document) {
        return world_id_.isEmpty() || QString::fromStdString(document.world_id) != world_id_;
    });
    desired_source_id_.clear();
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
    if (selected_index_ >= 0) {
        // 导入新来源不继承旧章节；刷新同一来源才恢复稳定章节 ID 与窗口。
        if (previous_source_id == keep_id)
            selectSourceAt(selected_index_, previous_chapter_id, previous_window_start);
        else selectSourceAt(selected_index_);
    }
    else emit changed();
    refreshAfterWorldChange();
}

/** @brief 在异步操作落地后刷新世界切换期间可能变化的来源清单。 */
void SourceViewModel::refreshAfterWorldChange() {
    if (!refresh_after_world_change_) return;
    refresh_after_world_change_ = false;
    refresh();
}

void SourceViewModel::importFile(const QUrl& file_url, QString world_id) {
    if (busy_ || !file_url.isLocalFile()) return;
    if (world_id.isEmpty()) { error_text_ = QStringLiteral("请先在首页创建世界"); emit changed(); return; }
    if (world_id != world_id_) { error_text_ = QStringLiteral("当前世界已切换，请重新选择导入目标"); emit changed(); return; }
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    const auto file = file_url.toLocalFile().toStdWString();
    const auto world_generation = world_generation_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, file, command, world_id, world_generation] {
        xuyan::domain::Result<xuyan::domain::SourceDocument> imported;
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> documents;
        try {
            xuyan::application::SourceImportService service(path);
            imported = service.importTextFile(command, std::filesystem::path(file), "1", world_id.toStdString());
            if (imported.ok()) {
                auto attached = xuyan::storage::WorkspaceRepository(path).attachWorldSource(
                    world_id.toStdString(), imported.value->id);
                if (!attached.ok()) imported = decltype(imported)::failure(*attached.error);
                else documents = service.listForWorld(world_id.toStdString());
            }
        } catch (const std::exception& exception) {
            imported = decltype(imported)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                    "检查文件与工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, imported = std::move(imported), documents = std::move(documents), world_id, world_generation]() mutable {
            if (!self) return;
            if (self->world_id_ != world_id || self->world_generation_ != world_generation) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            if (!imported.ok()) {
                self->busy_ = false;
                self->error_text_ = QString::fromStdString(imported.error->message);
                emit self->changed();
                self->refreshAfterWorldChange();
                return;
            }
            const auto keep = QString::fromStdString(imported.value->id);
            self->applyDocuments(std::move(documents), keep);
            emit self->sourceImported();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectSource(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) return;
    // 用户明确选中来源后，旧的按 ID 延迟跳转意图不应在下次刷新时重新生效。
    desired_source_id_.clear();
    selectSourceAt(index);
}

void SourceViewModel::selectSourceAt(int index, const std::string& chapter_id,
                                     std::optional<std::size_t> preferred_window_start) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) return;
    ++preview_generation_;
    pending_evidence_range_.reset();
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
    const auto& chapters = documents_[static_cast<std::size_t>(index)].chapters;
    if (!chapters.empty()) {
        int chapter_index = 0;
        bool chapter_matched = false;
        if (!chapter_id.empty()) {
            const auto found = std::find_if(chapters.begin(), chapters.end(), [&chapter_id](const auto& chapter) {
                return chapter.id == chapter_id;
            });
            if (found != chapters.end()) {
                chapter_index = static_cast<int>(std::distance(chapters.begin(), found));
                chapter_matched = true;
            }
        }
        if (!chapter_matched && preferred_window_start) {
            // 外部重划章节可能更换 ID，退化为按原文绝对坐标寻找仍覆盖阅读位置的章节。
            const auto found = std::find_if(chapters.begin(), chapters.end(), [preferred_window_start](const auto& chapter) {
                return chapter.start_codepoint <= *preferred_window_start
                    && *preferred_window_start < chapter.end_codepoint;
            });
            if (found != chapters.end()) {
                chapter_index = static_cast<int>(std::distance(chapters.begin(), found));
                chapter_matched = true;
            }
        }
        if (!chapter_matched) preferred_window_start.reset();
        selected_chapter_index_ = chapter_index;
        const auto& chapter = chapters[static_cast<std::size_t>(chapter_index)];
        auto start = chapter.start_codepoint;
        if (preferred_window_start && *preferred_window_start >= chapter.start_codepoint) {
            // 边界校正后若旧窗口起点超出本章，退到仍可读的最后一个窗口。
            start = *preferred_window_start < chapter.end_codepoint ? *preferred_window_start
                : std::max(chapter.start_codepoint, chapter.end_codepoint -
                    std::min(preview_window_size_, chapter.end_codepoint - chapter.start_codepoint));
        }
        showWindow(start, std::min(chapter.end_codepoint, start + preview_window_size_));
    } else {
        preview_text_.clear(); preview_loading_ = false;
        preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
        highlight_start_ = 0; highlight_end_ = 0;
        emit changed();
    }
}

void SourceViewModel::selectSourceId(QString source_id) {
    if (source_id.isEmpty() || world_id_.isEmpty()) return;
    desired_source_id_ = source_id;
    for (int index = 0; index < static_cast<int>(documents_.size()); ++index) {
        if (QString::fromStdString(documents_[index].id) == source_id) {
            selectSource(index); return;
        }
    }
    if (!busy_) {
        status_text_ = QStringLiteral("正在刷新来源，请稍后再试");
        refresh();
    }
}

void SourceViewModel::loadPreview(const std::string& source_id, std::size_t start_codepoint,
                                  std::size_t end_codepoint) {
    const auto path = database_path_;
    const auto world_generation = world_generation_;
    const auto preview_generation = preview_generation_;
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, source_id, start_codepoint, end_codepoint,
                                          world_generation, preview_generation] {
        xuyan::domain::Result<std::string> result;
        xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> evidence;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.evidenceText(source_id, start_codepoint, end_codepoint);
            xuyan::application::EvidenceService evidence_service(path);
            evidence = evidence_service.listForSource(source_id);
        } catch (const std::exception& exception) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                "检查来源与工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, start_codepoint, end_codepoint,
                                         world_generation, preview_generation,
                                         result = std::move(result), evidence = std::move(evidence)]() mutable {
            if (!self || self->world_generation_ != world_generation
                || self->preview_generation_ != preview_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id
                || self->preview_start_codepoint_ != start_codepoint
                || self->preview_end_codepoint_ != end_codepoint) return;
            self->preview_loading_ = false;
            if (!result.ok()) {
                self->error_text_ = QString::fromStdString(result.error->message);
                self->pending_evidence_range_.reset();
            }
            else self->preview_text_ = QString::fromStdString(*result.value);
            self->evidence_.clear(); self->evidence_items_.clear();
            if (evidence.ok()) {
                self->evidence_ = std::move(*evidence.value);
                for (const auto& item : self->evidence_) self->evidence_items_.push_back(QVariantMap{
                    {"entityId", QString::fromStdString(item.entity_id)}, {"field", QString::fromStdString(item.field_path)},
                    {"start", static_cast<qlonglong>(item.start_codepoint)}, {"end", static_cast<qlonglong>(item.end_codepoint)},
                    {"quote", QString::fromStdString(item.quote)}, {"provenance", QString::fromStdString(item.provenance_type)}});
            }
            if (result.ok() && self->pending_evidence_range_) {
                const auto [start, end] = *self->pending_evidence_range_;
                self->highlightEvidenceRange(start, end);
                self->pending_evidence_range_.reset();
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::createEvidence(QString entity_id, QString field_path, int selection_start,
                                     int selection_end, QString provenance_type) {
    if (busy_ || preview_loading_ || selected_index_ < 0) return;
    if (selection_start < 0 || selection_end <= selection_start || selection_end > preview_text_.size()) {
        error_text_ = QStringLiteral("请先在原文预览中选择一段非空文本"); emit changed(); return;
    }
    const auto start_cp = preview_start_codepoint_ + static_cast<std::size_t>(preview_text_.left(selection_start).toUcs4().size());
    const auto end_cp = preview_start_codepoint_ + static_cast<std::size_t>(preview_text_.left(selection_end).toUcs4().size());
    const auto source_id = documents_[selected_index_].id;
    const auto world_generation = world_generation_;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在校验证据区间…"); emit changed();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, source_id, entity_id, field_path,
                                          start_cp, end_cp, provenance_type, world_generation] {
        xuyan::application::EvidenceService service(path);
        auto created = service.create(command, entity_id.toStdString(), field_path.toStdString(), source_id,
                                      start_cp, end_cp, provenance_type.toStdString());
        auto listed = service.listForSource(source_id);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, world_generation,
                                         created = std::move(created), listed = std::move(listed)]() mutable {
            if (!self) return;
            if (self->world_generation_ != world_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
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
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectEvidence(int index) {
    if (index < 0 || index >= static_cast<int>(evidence_.size())) return;
    const auto& item = evidence_[static_cast<std::size_t>(index)];
    if (item.start_codepoint >= preview_start_codepoint_ && item.end_codepoint <= preview_end_codepoint_) {
        if (preview_loading_) pending_evidence_range_ = {{item.start_codepoint, item.end_codepoint}};
        else highlightEvidenceRange(item.start_codepoint, item.end_codepoint);
        emit changed();
        return;
    }
    if (selected_index_ < 0) return;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    for (std::size_t chapter_index = 0; chapter_index < chapters.size(); ++chapter_index) {
        const auto& chapter = chapters[chapter_index];
        if (item.start_codepoint < chapter.start_codepoint || item.start_codepoint >= chapter.end_codepoint) continue;
        // 跨章证据仍归属起点章节；临时窗口可越过章节末尾以完整显示引文。
        const auto candidate_start = item.end_codepoint > preview_window_size_
            ? item.end_codepoint - preview_window_size_ : 0;
        const auto start = std::max(chapter.start_codepoint, candidate_start);
        if (start > item.start_codepoint) break;
        selected_chapter_index_ = static_cast<int>(chapter_index);
        pending_evidence_range_ = {{item.start_codepoint, item.end_codepoint}};
        showWindow(start, std::min(std::max(chapter.end_codepoint, item.end_codepoint),
                                   start + preview_window_size_));
        return;
    }
    status_text_ = QStringLiteral("该证据位于其他章节，请先选择对应章节");
    emit changed();
}

void SourceViewModel::selectChapter(int index) {
    if (selected_index_ < 0 || index < 0 || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    selected_chapter_index_ = index;
    pending_evidence_range_.reset();
    const auto& chapter = documents_[selected_index_].chapters[static_cast<std::size_t>(index)];
    showWindow(chapter.start_codepoint, std::min(chapter.end_codepoint,
                                                chapter.start_codepoint + preview_window_size_));
}

void SourceViewModel::previousWindow() {
    if (!canPreviousWindow()) return;
    const auto& chapter = documents_[static_cast<std::size_t>(selected_index_)].chapters[
        static_cast<std::size_t>(selected_chapter_index_)];
    const auto end = preview_start_codepoint_;
    const auto start = end - chapter.start_codepoint > preview_window_size_
        ? end - preview_window_size_ : chapter.start_codepoint;
    pending_evidence_range_.reset();
    showWindow(start, end);
}

void SourceViewModel::nextWindow() {
    if (!canNextWindow()) return;
    const auto& chapter = documents_[static_cast<std::size_t>(selected_index_)].chapters[
        static_cast<std::size_t>(selected_chapter_index_)];
    const auto start = preview_end_codepoint_;
    pending_evidence_range_.reset();
    showWindow(start, std::min(chapter.end_codepoint, start + preview_window_size_));
}

void SourceViewModel::showWindow(std::size_t start_codepoint, std::size_t end_codepoint) {
    if (selected_index_ < 0 || end_codepoint < start_codepoint) return;
    // 每次切窗递增请求代次；旧窗口即使与新窗口边界相同也不能回填。
    ++preview_generation_;
    preview_start_codepoint_ = start_codepoint;
    preview_end_codepoint_ = end_codepoint;
    preview_text_ = QStringLiteral("正在读取本章原文…");
    preview_loading_ = true;
    highlight_start_ = 0; highlight_end_ = 0;
    emit changed();
    loadPreview(documents_[selected_index_].id, preview_start_codepoint_, preview_end_codepoint_);
}

void SourceViewModel::highlightEvidenceRange(std::size_t start_codepoint, std::size_t end_codepoint) {
    highlight_start_ = 0; highlight_end_ = 0;
    if (preview_loading_ || start_codepoint < preview_start_codepoint_ || end_codepoint > preview_end_codepoint_
        || end_codepoint < start_codepoint) return;
    const auto utf16ForCodepoint = [this](std::size_t wanted) {
        qsizetype offset = 0; std::size_t count = 0;
        while (offset < preview_text_.size() && count < wanted) {
            // Qt 字符串按 UTF-16 存储；补充平面字符跨两个码元但只占一个码点。
            if (preview_text_.at(offset).isHighSurrogate() && offset + 1 < preview_text_.size()
                && preview_text_.at(offset + 1).isLowSurrogate()) offset += 2;
            else ++offset;
            ++count;
        }
        return static_cast<int>(offset);
    };
    highlight_start_ = utf16ForCodepoint(start_codepoint - preview_start_codepoint_);
    highlight_end_ = utf16ForCodepoint(end_codepoint - preview_start_codepoint_);
}

void SourceViewModel::saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint) {
    if (busy_ || selected_index_ < 0 || index < 0
        || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    auto chapters = documents_[selected_index_].chapters;
    auto& edited = chapters[static_cast<std::size_t>(index)];
    edited.title = title.trimmed().toStdString();
    edited.start_codepoint = start_codepoint < 0 ? 0 : static_cast<std::size_t>(start_codepoint);
    edited.end_codepoint = end_codepoint < 0 ? 0 : static_cast<std::size_t>(end_codepoint);
    // 表单只编辑一章；相邻章节共享的切片边界随之移动，避免留下未解析的原文空档。
    if (index > 0) chapters[static_cast<std::size_t>(index - 1)].end_codepoint = edited.start_codepoint;
    if (index + 1 < static_cast<int>(chapters.size()))
        chapters[static_cast<std::size_t>(index + 1)].start_codepoint = edited.end_codepoint;
    const auto source_id = documents_[selected_index_].id; const auto expected = documents_[selected_index_].chapter_revision;
    const auto world_generation = world_generation_;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在校验章节布局…"); emit changed();
    QPointer<SourceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, source_id, expected,
                                          chapters = std::move(chapters), world_generation]() mutable {
        xuyan::domain::Result<xuyan::domain::SourceDocument> result;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.saveChapters(command, source_id, expected, std::move(chapters));
        } catch (const std::exception&) {
            // 双层防护：服务外的意外异常也必须让界面结束忙碌态并允许重试。
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
                                                "章节布局保存失败", true, "检查工作区文件与权限后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, source_id, world_generation, result = std::move(result)]() mutable {
            if (!self) return;
            if (self->world_generation_ != world_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            self->busy_ = false;
            if (!result.ok()) {
                self->error_text_ = QString::fromStdString(result.error->message);
                emit self->changed();
                self->refreshAfterWorldChange();
                return;
            }
            std::string selected_chapter_id;
            std::optional<std::size_t> selected_window_start;
            if (self->selected_chapter_index_ >= 0) {
                const auto& old_chapters = self->documents_[static_cast<std::size_t>(self->selected_index_)].chapters;
                if (static_cast<std::size_t>(self->selected_chapter_index_) < old_chapters.size()) {
                    selected_chapter_id = old_chapters[static_cast<std::size_t>(self->selected_chapter_index_)].id;
                    selected_window_start = self->preview_start_codepoint_;
                }
            }
            self->documents_[self->selected_index_] = std::move(*result.value);
            self->status_text_ = QStringLiteral("章节布局已保存为修订 %1").arg(self->documents_[self->selected_index_].chapter_revision);
            self->selectSourceAt(self->selected_index_, selected_chapter_id, selected_window_start);
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}
