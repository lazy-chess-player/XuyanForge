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
    Q_PROPERTY(QVariantList demoStages READ demoStages NOTIFY changed)
    Q_PROPERTY(int demoCompleted READ demoCompleted NOTIFY changed)
    Q_PROPERTY(int demoTotal READ demoTotal NOTIFY changed)
    Q_PROPERTY(bool demoReady READ demoReady NOTIFY changed)
    Q_PROPERTY(bool demoBusy READ demoBusy NOTIFY changed)
    Q_PROPERTY(bool onboardingVisible READ onboardingVisible NOTIFY changed)
    Q_PROPERTY(QString demoVersionId READ demoVersionId NOTIFY changed)

public:
    explicit WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList recentWorkspaces() const { return recent_; }
    QString currentPath() const;
    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_text_; }
    QString errorText() const { return error_text_; }
    QVariantList demoStages() const { return demo_stages_; }
    int demoCompleted() const noexcept { return demo_completed_; }
    int demoTotal() const noexcept { return demo_stages_.size(); }
    bool demoReady() const noexcept { return demo_ready_; }
    bool demoBusy() const noexcept { return demo_busy_; }
    bool onboardingVisible() const noexcept { return !onboarding_dismissed_ && !demo_ready_; }
    QString demoVersionId() const { return demo_version_id_; }

    Q_INVOKABLE void createWorkspace(QString name);
    Q_INVOKABLE void openWorkspace(const QUrl& source);
    Q_INVOKABLE void switchToRecent(int index);
    Q_INVOKABLE void forgetRecent(int index);
    Q_INVOKABLE void refreshDemo();
    Q_INVOKABLE void installDemoWorld();
    Q_INVOKABLE void dismissOnboarding();

signals:
    void changed();
    void demoInstalled();

private:
    void loadRecent();
    void registerRecent(QString name, const QString& path);
    void saveRecent();
    void initializeAndSwitch(QString name, std::filesystem::path path);
    static void restartAt(const QString& path);
    QString onboardingSettingsKey() const;

    std::filesystem::path database_path_;
    QVariantList recent_;
    bool busy_{false};
    QString status_text_{QStringLiteral("当前工作区已打开")};
    QString error_text_;
    QVariantList demo_stages_;
    int demo_completed_{0};
    bool demo_ready_{false};
    bool demo_busy_{false};
    bool onboarding_dismissed_{false};
    QString demo_version_id_;
};
