#include "workspace_catalog_view_model.h"

#include "xuyan/application/workspace_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/storage/workspace_repository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>

WorkspaceCatalogViewModel::WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {
    loadRecent();
    theme_id_ = QSettings().value(QStringLiteral("appearance/themeId"), QStringLiteral("dark")).toString();
    if (theme_id_ != QStringLiteral("dark") && theme_id_ != QStringLiteral("light")) theme_id_ = QStringLiteral("dark");
    if (!QCoreApplication::arguments().contains(QStringLiteral("--screenshot")))
        registerRecent(QString::fromStdWString(database_path_.stem().wstring()), currentPath());
    refreshWorlds();
}

void WorkspaceCatalogViewModel::refreshWorlds() {
    QPointer<WorkspaceCatalogViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::storage::WorkspaceRepository repository(path);
        auto result = repository.listWorldTemplates();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            if (!result.ok()) { self->error_text_ = QString::fromStdString(result.error->message); emit self->changed(); return; }
            self->worlds_.clear();
            for (const auto& world : *result.value) self->worlds_.push_back(QVariantMap{
                {"id", QString::fromStdString(world.id)}, {"name", QString::fromStdString(world.name)},
                {"sourceId", QString::fromStdString(world.source_id)}});
            if (self->active_world_id_.isEmpty() && !self->worlds_.empty())
                self->active_world_id_ = self->worlds_.front().toMap().value(QStringLiteral("id")).toString();
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void WorkspaceCatalogViewModel::createWorld(QString name, const QUrl& novel_file) {
    name = name.trimmed();
    if (busy_) return;
    if (name.isEmpty() || name.size() > 120) {
        error_text_ = QStringLiteral("世界名称不能为空且不能超过 120 个字符"); emit changed(); return;
    }
    if (!novel_file.isEmpty() && !novel_file.isLocalFile()) {
        error_text_ = QStringLiteral("请选择本机 TXT 或 Markdown 小说"); emit changed(); return;
    }
    busy_ = true; error_text_.clear(); created_source_id_.clear(); created_chapter_count_ = 0;
    status_text_ = QStringLiteral("正在创建世界并解析章节…"); emit changed();
    const auto path = database_path_;
    const auto id = QStringLiteral("world-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto file = novel_file.isEmpty() ? QString{} : novel_file.toLocalFile();
    QPointer<WorkspaceCatalogViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, id, name, file] {
        QString error; QString source_id; int chapters = 0;
        try {
            xuyan::storage::WorkspaceRepository repository(path);
            auto created = repository.createWorldTemplate(id.toStdString(), name.toStdString());
            if (!created.ok()) error = QString::fromStdString(created.error->message);
            if (error.isEmpty() && !file.isEmpty()) {
                xuyan::application::SourceImportService importer(path);
                auto imported = importer.importTextFile(
                    QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
                    std::filesystem::path(file.toStdWString()), "1", id.toStdString());
                if (!imported.ok()) error = QString::fromStdString(imported.error->message);
                else {
                    auto attached = repository.attachWorldSource(id.toStdString(), imported.value->id);
                    if (!attached.ok()) error = QString::fromStdString(attached.error->message);
                    else { source_id = QString::fromStdString(imported.value->id);
                           chapters = static_cast<int>(imported.value->chapters.size()); }
                }
            }
        } catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, error, source_id, chapters, id] {
            if (!self) return;
            self->busy_ = false;
            self->error_text_ = error;
            self->created_source_id_ = source_id;
            self->created_chapter_count_ = chapters;
            self->active_world_id_ = id;
            self->status_text_ = error.isEmpty()
                ? (source_id.isEmpty() ? QStringLiteral("空白世界已创建，可稍后导入小说")
                                       : QStringLiteral("小说已导入，检测到 %1 个章节；请校对后创建提取任务").arg(chapters))
                : QStringLiteral("世界已创建，但小说导入未完成；请检查文件后重试");
            self->refreshWorlds(); emit self->changed();
            if (error.isEmpty()) emit self->worldCreated();
        }, Qt::QueuedConnection);
    });
}

void WorkspaceCatalogViewModel::selectWorld(int index) {
    if (index < 0 || index >= worlds_.size()) return;
    active_world_id_ = worlds_.at(index).toMap().value(QStringLiteral("id")).toString();
    emit changed();
}

void WorkspaceCatalogViewModel::setThemeId(QString theme_id) {
    if (theme_id != QStringLiteral("dark") && theme_id != QStringLiteral("light")) return;
    if (theme_id_ == theme_id) return;
    theme_id_ = std::move(theme_id);
    QSettings settings; settings.setValue(QStringLiteral("appearance/themeId"), theme_id_); settings.sync();
    emit changed();
}

QString WorkspaceCatalogViewModel::currentPath() const { return QDir::toNativeSeparators(QString::fromStdWString(database_path_.wstring())); }

void WorkspaceCatalogViewModel::loadRecent() {
    QSettings settings;
    const auto size = settings.beginReadArray(QStringLiteral("recentWorkspaces"));
    for (int index = 0; index < size; ++index) {
        settings.setArrayIndex(index);
        const auto path = settings.value(QStringLiteral("path")).toString();
        const auto portable_path = QDir::fromNativeSeparators(path);
        const auto data_root = QDir::fromNativeSeparators(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        if (portable_path.contains(QStringLiteral("/build/"), Qt::CaseInsensitive)
            || (QDir::fromNativeSeparators(QFileInfo(path).absolutePath()) == QDir::cleanPath(data_root)
                && QFileInfo(path).fileName() != QStringLiteral("workspace.sqlite"))) continue;
        if (!path.isEmpty() && QFileInfo::exists(path)) recent_.push_back(QVariantMap{
            {"name", settings.value(QStringLiteral("name")).toString()}, {"path", path},
            {"lastOpened", settings.value(QStringLiteral("lastOpened")).toString()}});
    }
    settings.endArray();
}

void WorkspaceCatalogViewModel::saveRecent() {
    QSettings settings; settings.remove(QStringLiteral("recentWorkspaces"));
    settings.beginWriteArray(QStringLiteral("recentWorkspaces"));
    for (int index = 0; index < recent_.size(); ++index) {
        settings.setArrayIndex(index); const auto item = recent_.at(index).toMap();
        settings.setValue(QStringLiteral("name"), item.value(QStringLiteral("name")));
        settings.setValue(QStringLiteral("path"), item.value(QStringLiteral("path")));
        settings.setValue(QStringLiteral("lastOpened"), item.value(QStringLiteral("lastOpened")));
    }
    settings.endArray(); settings.sync();
}

void WorkspaceCatalogViewModel::registerRecent(QString name, const QString& path) {
    const auto canonical = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    for (int index = recent_.size() - 1; index >= 0; --index)
        if (QFileInfo(recent_.at(index).toMap().value(QStringLiteral("path")).toString()).absoluteFilePath()
            == QFileInfo(canonical).absoluteFilePath()) recent_.removeAt(index);
    if (name.trimmed().isEmpty()) name = QFileInfo(canonical).completeBaseName();
    recent_.push_front(QVariantMap{{"name", name.trimmed()}, {"path", canonical},
        {"lastOpened", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    while (recent_.size() > 20) recent_.removeLast();
    saveRecent(); emit changed();
}

void WorkspaceCatalogViewModel::restartAt(const QString& path) {
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--workspace"), path});
    QCoreApplication::quit();
}

void WorkspaceCatalogViewModel::initializeAndSwitch(QString name, std::filesystem::path path) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在打开工作区…"); emit changed();
    QPointer<WorkspaceCatalogViewModel> self(this);
    QThreadPool::globalInstance()->start([self, name, path = std::move(path)] {
        QString error;
        try {
            xuyan::application::WorkspaceService service(path);
            auto opened = service.openAndList(1);
            if (!opened.ok()) error = QString::fromStdString(opened.error->message);
        } catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
        if (!self) return;
        const auto path_text = QDir::toNativeSeparators(QString::fromStdWString(path.wstring()));
        QMetaObject::invokeMethod(self, [self, name, path_text, error] {
            if (!self) return;
            self->busy_ = false;
            if (!error.isEmpty()) { self->error_text_ = error; self->status_text_.clear(); emit self->changed(); return; }
            self->registerRecent(name, path_text); self->status_text_ = QStringLiteral("正在切换工作区…"); emit self->changed();
            restartAt(path_text);
        }, Qt::QueuedConnection);
    });
}

void WorkspaceCatalogViewModel::createWorkspace(QString name) {
    name = name.trimmed();
    if (name.isEmpty() || name.size() > 120) { error_text_ = QStringLiteral("工作区名称不能为空且不能超过 120 个字符"); emit changed(); return; }
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/workspaces/")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(root);
    initializeAndSwitch(name, std::filesystem::path((root + QStringLiteral("/workspace.sqlite")).toStdWString()));
}

void WorkspaceCatalogViewModel::openWorkspace(const QUrl& source) {
    if (!source.isLocalFile()) { error_text_ = QStringLiteral("请选择本机 SQLite 工作区文件"); emit changed(); return; }
    const auto path = source.toLocalFile();
    initializeAndSwitch(QFileInfo(path).completeBaseName(), std::filesystem::path(path.toStdWString()));
}

void WorkspaceCatalogViewModel::switchToRecent(int index) {
    if (index < 0 || index >= recent_.size()) return;
    const auto item = recent_.at(index).toMap();
    initializeAndSwitch(item.value(QStringLiteral("name")).toString(),
                        std::filesystem::path(item.value(QStringLiteral("path")).toString().toStdWString()));
}

void WorkspaceCatalogViewModel::forgetRecent(int index) {
    if (index < 0 || index >= recent_.size()) return;
    recent_.removeAt(index); saveRecent(); emit changed();
}
