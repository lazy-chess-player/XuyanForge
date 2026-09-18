#include "workspace_catalog_view_model.h"

#include "xuyan/application/workspace_service.h"

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
    registerRecent(QString::fromStdWString(database_path_.stem().wstring()), currentPath());
}

QString WorkspaceCatalogViewModel::currentPath() const { return QDir::toNativeSeparators(QString::fromStdWString(database_path_.wstring())); }

void WorkspaceCatalogViewModel::loadRecent() {
    QSettings settings;
    const auto size = settings.beginReadArray(QStringLiteral("recentWorkspaces"));
    for (int index = 0; index < size; ++index) {
        settings.setArrayIndex(index);
        const auto path = settings.value(QStringLiteral("path")).toString();
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
