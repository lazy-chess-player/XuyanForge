#include "workspace_view_model.h"

#include "xuyan/application/workspace_service.h"
#include "xuyan/domain/hash.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>
#include <QSettings>

#include <algorithm>
#include <exception>

namespace {
QString draftRoot(const std::filesystem::path& database_path) {
    const auto database = QString::fromStdWString(database_path.wstring()).toStdString();
    return QStringLiteral("workspaceDrafts/%1").arg(
        QString::fromStdString(xuyan::domain::sha256(database).substr(0, 20)));
}
}

WorkspaceViewModel::WorkspaceViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {
    refresh();
}

QString WorkspaceViewModel::selectedId() const {
    return selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].id) : QString{};
}
QString WorkspaceViewModel::selectedName() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("name").toString();
    return selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].name) : QString{};
}
QString WorkspaceViewModel::selectedKind() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("kind").toString();
    return selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].kind) : QStringLiteral("other");
}
QString WorkspaceViewModel::selectedDescription() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("description").toString();
    return selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].description) : QString{};
}
QString WorkspaceViewModel::selectedAliases() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("aliases").toString();
    if (selected_index_ < 0) return {};
    QStringList values;
    for (const auto& value : entities_[selected_index_].aliases) values << QString::fromStdString(value);
    return values.join(QStringLiteral("，"));
}
QString WorkspaceViewModel::selectedTags() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("tags").toString();
    if (selected_index_ < 0) return {};
    QStringList values;
    for (const auto& value : entities_[selected_index_].tags) values << QString::fromStdString(value);
    return values.join(QStringLiteral("，"));
}
QString WorkspaceViewModel::selectedAttributes() const {
    if (!active_draft_.isEmpty()) return active_draft_.value("attributes").toString();
    return selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].attributes_json) : QStringLiteral("{}");
}
int WorkspaceViewModel::selectedRevision() const noexcept {
    return selected_index_ >= 0 ? entities_[selected_index_].revision : 0;
}

QString WorkspaceViewModel::pageText() const {
    if (total_ == 0) return QStringLiteral("0 / 0");
    return QStringLiteral("%1–%2 / %3").arg(offset_ + 1).arg(offset_ + static_cast<int>(entities_.size())).arg(total_);
}

QString WorkspaceViewModel::commandId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

std::vector<std::string> WorkspaceViewModel::parseList(QString value) {
    value.replace(QChar(0xff0c), QChar(','));
    const auto parts = value.split(',', Qt::SkipEmptyParts);
    std::vector<std::string> result;
    result.reserve(parts.size());
    for (const auto& part : parts) result.push_back(part.trimmed().toStdString());
    return result;
}

QVariantMap WorkspaceViewModel::toMap(const xuyan::domain::WorldEntity& entity) {
    return {{QStringLiteral("id"), QString::fromStdString(entity.id)},
            {QStringLiteral("name"), QString::fromStdString(entity.name)},
            {QStringLiteral("kind"), QString::fromStdString(entity.kind)},
            {QStringLiteral("description"), QString::fromStdString(entity.description)},
            {QStringLiteral("revision"), entity.revision}};
}

xuyan::domain::WorldEntity WorkspaceViewModel::fromForm(QString name, QString kind, QString description,
                                                         QString aliases, QString tags, QString attributes) {
    xuyan::domain::WorldEntity entity;
    entity.kind = kind.toStdString();
    entity.name = name.trimmed().toStdString();
    entity.description = description.toStdString();
    entity.aliases = parseList(std::move(aliases));
    entity.tags = parseList(std::move(tags));
    entity.attributes_json = attributes.trimmed().isEmpty() ? "{}" : attributes.trimmed().toStdString();
    return entity;
}

void WorkspaceViewModel::refresh(QString query, QString kind) {
    last_query_ = std::move(query);
    last_kind_ = std::move(kind);
    offset_ = 0;
    loadPage(last_query_, last_kind_, selectedId());
}

void WorkspaceViewModel::loadPage(QString query, QString kind, QString keep_id) {
    if (busy_) return;
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    QPointer<WorkspaceViewModel> self(this);
    const auto offset = offset_; const auto limit = page_size_;
    QThreadPool::globalInstance()->start([self, path, query = std::move(query), kind = std::move(kind), keep_id, offset, limit] {
        PageResult result;
        try {
            xuyan::application::WorkspaceService service(path);
            result = query.isEmpty() && kind.isEmpty() && offset == 0
                ? service.openAndList(limit)
                : service.search(query.toStdString(), kind.toStdString(), offset, limit);
        } catch (const std::exception& exception) {
            result = PageResult::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                          "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), keep_id]() mutable {
            if (self) self->applyPage(std::move(result), keep_id);
        }, Qt::QueuedConnection);
    });
}

void WorkspaceViewModel::applyPage(PageResult result, const QString& keep_id) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = QString::fromStdString(result.error->message);
        emit changed();
        return;
    }
    entities_ = std::move(result.value->items);
    total_ = result.value->total;
    items_.clear();
    selected_index_ = -1;
    for (std::size_t index = 0; index < entities_.size(); ++index) {
        items_.push_back(toMap(entities_[index]));
        if (QString::fromStdString(entities_[index].id) == keep_id) selected_index_ = static_cast<int>(index);
    }
    bool restore_new_draft = false;
    if (keep_id.isEmpty()) {
        QSettings settings; const auto last = settings.value(draftRoot(database_path_) + QStringLiteral("/lastKey")).toString();
        if (last == QStringLiteral("__new__")) { selected_index_ = -1; restore_new_draft = true; }
        else if (!last.isEmpty()) for (std::size_t index = 0; index < entities_.size(); ++index)
            if (QString::fromStdString(entities_[index].id) == last) selected_index_ = static_cast<int>(index);
    }
    if (selected_index_ < 0 && !entities_.empty() && !restore_new_draft) selected_index_ = 0;
    loadActiveDraft();
    emit changed();
}

void WorkspaceViewModel::selectEntity(int index) {
    if (index < 0 || index >= static_cast<int>(entities_.size())) return;
    selected_index_ = index;
    loadActiveDraft();
    error_text_.clear();
    emit changed();
}

void WorkspaceViewModel::clearSelection() {
    selected_index_ = -1;
    loadActiveDraft();
    error_text_.clear();
    emit changed();
}

void WorkspaceViewModel::runEntity(std::function<EntityResult(const std::filesystem::path&)> work,
                                   QString success_message) {
    if (busy_) return;
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    QPointer<WorkspaceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, work = std::move(work), success_message = std::move(success_message)] {
        EntityResult result;
        try {
            result = work(path);
        } catch (const std::exception& exception) {
            result = EntityResult::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                            "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), success_message]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) {
                self->error_text_ = QString::fromStdString(result.error->message);
                emit self->changed();
                return;
            }
            self->status_text_ = success_message;
            self->clearActiveDraft();
            const auto keep = QString::fromStdString(result.value->id);
            self->loadPage(self->last_query_, self->last_kind_, keep);
        }, Qt::QueuedConnection);
    });
}

QString WorkspaceViewModel::draftKey() const {
    return draftRoot(database_path_) + QLatin1Char('/')
        + (selectedId().isEmpty() ? QStringLiteral("__new__") : selectedId());
}

void WorkspaceViewModel::loadActiveDraft() {
    QSettings settings; settings.beginGroup(draftKey());
    active_draft_.clear();
    const auto revision = settings.value(QStringLiteral("revision"), -1).toInt();
    if (revision == selectedRevision()) {
        for (const auto* key : {"name", "kind", "description", "aliases", "tags", "attributes"})
            if (settings.contains(QString::fromLatin1(key))) active_draft_.insert(QString::fromLatin1(key), settings.value(QString::fromLatin1(key)));
    }
    settings.endGroup();
}

void WorkspaceViewModel::clearActiveDraft() {
    QSettings settings; const auto current = selectedId().isEmpty() ? QStringLiteral("__new__") : selectedId();
    settings.remove(draftKey());
    if (settings.value(draftRoot(database_path_) + QStringLiteral("/lastKey")).toString() == current)
        settings.remove(draftRoot(database_path_) + QStringLiteral("/lastKey"));
    settings.sync(); active_draft_.clear();
}

void WorkspaceViewModel::saveDraft(QString name, QString kind, QString description,
                                   QString aliases, QString tags, QString attributes) {
    const QVariantMap incoming{{"name", name}, {"kind", kind}, {"description", description},
        {"aliases", aliases}, {"tags", tags}, {"attributes", attributes}};
    if (incoming == active_draft_) return;
    QString original_aliases; QString original_tags;
    if (selected_index_ >= 0) {
        QStringList values;
        for (const auto& value : entities_[selected_index_].aliases) values << QString::fromStdString(value);
        original_aliases = values.join(QStringLiteral("，")); values.clear();
        for (const auto& value : entities_[selected_index_].tags) values << QString::fromStdString(value);
        original_tags = values.join(QStringLiteral("，"));
    }
    const bool unchanged = name == (selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].name) : QString{})
        && kind == (selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].kind) : QStringLiteral("other"))
        && description == (selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].description) : QString{})
        && aliases == original_aliases
        && tags == original_tags
        && attributes == (selected_index_ >= 0 ? QString::fromStdString(entities_[selected_index_].attributes_json) : QStringLiteral("{}"));
    if (unchanged) {
        const bool had_draft = !active_draft_.isEmpty(); clearActiveDraft();
        if (had_draft) emit changed();
        return;
    }
    active_draft_ = incoming;
    QSettings settings; settings.beginGroup(draftKey()); settings.setValue(QStringLiteral("revision"), selectedRevision());
    for (auto iterator = active_draft_.cbegin(); iterator != active_draft_.cend(); ++iterator) settings.setValue(iterator.key(), iterator.value());
    settings.endGroup(); settings.setValue(draftRoot(database_path_) + QStringLiteral("/lastKey"),
        selectedId().isEmpty() ? QStringLiteral("__new__") : selectedId());
    settings.sync(); emit changed();
}

void WorkspaceViewModel::discardDraft() { clearActiveDraft(); emit changed(); }

void WorkspaceViewModel::previousPage() {
    if (busy_ || offset_ <= 0) return;
    offset_ = std::max(0, offset_ - page_size_); loadPage(last_query_, last_kind_);
}

void WorkspaceViewModel::nextPage() {
    if (busy_ || !hasNextPage()) return;
    offset_ += page_size_; loadPage(last_query_, last_kind_);
}

void WorkspaceViewModel::mergeSelectedInto(QString target_id) {
    target_id = target_id.trimmed();
    if (busy_ || selected_index_ < 0 || target_id.isEmpty()) return;
    if (draftAvailable()) { error_text_ = QStringLiteral("合并前请先保存或丢弃当前草稿"); emit changed(); return; }
    const auto source_id = entities_[selected_index_].id; const auto source_revision = entities_[selected_index_].revision;
    if (target_id.toStdString() == source_id) { error_text_ = QStringLiteral("合并目标不能是当前条目"); emit changed(); return; }
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在原子合并条目与引用…"); emit changed();
    const auto path = database_path_; const auto command = commandId().toStdString(); QPointer<WorkspaceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, source_id, source_revision, target_id] {
        xuyan::domain::Result<xuyan::domain::EntityMergeResult> result;
        try {
            xuyan::application::WorkspaceService service(path);
            auto target = service.load(target_id.toStdString());
            if (!target.ok()) result = decltype(result)::failure(*target.error);
            else result = service.merge(command, source_id, source_revision, target.value->id, target.value->revision);
        } catch (const std::exception& exception) { result = decltype(result)::failure(
            {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = QString::fromStdString(result.error->message); emit self->changed(); return; }
            self->status_text_ = QStringLiteral("合并完成，可用记录 %1 拆分恢复").arg(QString::fromStdString(result.value->merge_id));
            self->offset_ = 0; self->loadPage(self->last_query_, self->last_kind_, QString::fromStdString(result.value->target.id));
        }, Qt::QueuedConnection);
    });
}

void WorkspaceViewModel::splitMerge(QString merge_id) {
    merge_id = merge_id.trimmed();
    if (busy_ || merge_id.isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在校验并拆分合并记录…"); emit changed();
    const auto path = database_path_; const auto command = commandId().toStdString(); QPointer<WorkspaceViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, command, merge_id] {
        xuyan::domain::Result<xuyan::domain::EntityMergeResult> result;
        try {
            xuyan::application::WorkspaceService service(path);
            result = service.splitMerge(command, merge_id.toStdString());
        } catch (const std::exception& exception) { result = decltype(result)::failure(
            {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = QString::fromStdString(result.error->message); emit self->changed(); return; }
            self->status_text_ = QStringLiteral("拆分完成；双方稳定 ID 与证据引用已恢复");
            self->offset_ = 0; self->loadPage(self->last_query_, self->last_kind_, QString::fromStdString(result.value->source.id));
        }, Qt::QueuedConnection);
    });
}

void WorkspaceViewModel::createEntity(QString name, QString kind, QString description,
                                      QString aliases, QString tags, QString attributes) {
    if (world_id_.isEmpty()) { error_text_ = QStringLiteral("请先在首页创建或选择世界"); emit changed(); return; }
    auto entity = fromForm(std::move(name), std::move(kind), std::move(description),
                           std::move(aliases), std::move(tags), std::move(attributes));
    entity.world_id = world_id_.toStdString();
    const auto command = commandId().toStdString();
    runEntity([command, entity = std::move(entity)](const auto& path) mutable {
        xuyan::application::WorkspaceService service(path);
        return service.create(command, std::move(entity));
    }, QStringLiteral("条目已创建"));
}

void WorkspaceViewModel::saveSelected(QString name, QString kind, QString description,
                                      QString aliases, QString tags, QString attributes) {
    if (selected_index_ < 0) {
        createEntity(std::move(name), std::move(kind), std::move(description),
                     std::move(aliases), std::move(tags), std::move(attributes));
        return;
    }
    auto entity = fromForm(std::move(name), std::move(kind), std::move(description),
                           std::move(aliases), std::move(tags), std::move(attributes));
    entity.id = entities_[selected_index_].id;
    entity.world_id = entities_[selected_index_].world_id;
    entity.review_status = entities_[selected_index_].review_status;
    const auto expected = entities_[selected_index_].revision;
    const auto command = commandId().toStdString();
    runEntity([command, entity = std::move(entity), expected](const auto& path) mutable {
        xuyan::application::WorkspaceService service(path);
        return service.save(command, std::move(entity), expected);
    }, QStringLiteral("条目修订已保存"));
}

void WorkspaceViewModel::deleteSelected() {
    if (selected_index_ < 0) return;
    const auto id = entities_[selected_index_].id;
    const auto expected = entities_[selected_index_].revision;
    const auto command = commandId().toStdString();
    runEntity([command, id, expected](const auto& path) {
        xuyan::application::WorkspaceService service(path);
        return service.remove(command, id, expected);
    }, QStringLiteral("条目已移入修订历史"));
}
