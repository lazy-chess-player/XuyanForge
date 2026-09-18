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

public:
    explicit WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList recentWorkspaces() const { return recent_; }
    QString currentPath() const;
    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_text_; }
    QString errorText() const { return error_text_; }

    Q_INVOKABLE void createWorkspace(QString name);
    Q_INVOKABLE void openWorkspace(const QUrl& source);
    Q_INVOKABLE void switchToRecent(int index);
    Q_INVOKABLE void forgetRecent(int index);

signals:
    void changed();

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
};
