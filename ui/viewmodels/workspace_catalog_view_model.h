#pragma once

#include <QObject>
#include <QVariantList>
#include <QUrl>

#include <filesystem>

class WorkspaceCatalogViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recentWorkspaces READ recentWorkspaces NOTIFY changed)
    Q_PROPERTY(QString currentPath READ currentPath CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY changed)
    Q_PROPERTY(QString createdSourceId READ createdSourceId NOTIFY changed)
    Q_PROPERTY(int createdChapterCount READ createdChapterCount NOTIFY changed)
    Q_PROPERTY(QString activeWorldId READ activeWorldId NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QVariantList availableThemes READ availableThemes CONSTANT)

public:
    explicit WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList recentWorkspaces() const { return recent_; }
    QString currentPath() const;
    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_text_; }
    QString errorText() const { return error_text_; }
    QVariantList worlds() const { return worlds_; }
    QString createdSourceId() const { return created_source_id_; }
    int createdChapterCount() const noexcept { return created_chapter_count_; }
    QString activeWorldId() const { return active_world_id_; }
    QString themeId() const { return theme_id_; }
    QVariantList availableThemes() const {
        return {QVariantMap{{"id", "dark"}, {"name", QStringLiteral("深色")}},
                QVariantMap{{"id", "light"}, {"name", QStringLiteral("浅色")}}};
    }

    Q_INVOKABLE void createWorkspace(QString name);
    Q_INVOKABLE void openWorkspace(const QUrl& source);
    Q_INVOKABLE void switchToRecent(int index);
    Q_INVOKABLE void forgetRecent(int index);
    Q_INVOKABLE void refreshWorlds();
    Q_INVOKABLE void createWorld(QString name, const QUrl& novel_file);
    Q_INVOKABLE void selectWorld(int index);
    Q_INVOKABLE void setThemeId(QString theme_id);

signals:
    void changed();
    void worldCreated();

private:
    void loadRecent();
    void registerRecent(QString name, const QString& path);
    void saveRecent();
    void initializeAndSwitch(QString name, std::filesystem::path path);
    static void restartAt(const QString& path);

    std::filesystem::path database_path_;
    QVariantList recent_;
    bool busy_{false};
    QString status_text_{QStringLiteral("当前工作区已打开")};
    QString error_text_;
    QVariantList worlds_;
    QString created_source_id_;
    int created_chapter_count_{0};
    QString active_world_id_;
    QString theme_id_{QStringLiteral("dark")};
};
