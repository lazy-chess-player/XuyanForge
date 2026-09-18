#pragma once

#include <QObject>
#include <QUrl>
#include <QStringList>

#include <filesystem>
#include <functional>

class PackageViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QStringList branchNames READ branchNames NOTIFY changed)
    Q_PROPERTY(QString comparisonText READ comparisonText NOTIFY changed)

public:
    explicit PackageViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_text_; }
    QString errorText() const { return error_text_; }
    QStringList branchNames() const { return branch_names_; }
    QString comparisonText() const { return comparison_text_; }

    Q_INVOKABLE void exportWorld(const QUrl& destination);
    Q_INVOKABLE void importWorld(const QUrl& source);
    Q_INVOKABLE void exportCharacter(QString blueprint_id, const QUrl& destination, bool include_private_notes);
    Q_INVOKABLE void importCharacter(const QUrl& source);
    Q_INVOKABLE void createBackup(const QUrl& parent_directory);
    Q_INVOKABLE void restoreBackup(const QUrl& backup_directory);
    Q_INVOKABLE void compareBranches(int left_index, int right_index);
    Q_INVOKABLE void exportBranch(int branch_index, const QUrl& destination, QString format, bool technical_log);
    Q_INVOKABLE void exportDiagnostics(const QUrl& destination);
    Q_INVOKABLE void adoptBranch(int branch_index, QString world_id, QString title);
    Q_INVOKABLE void reloadBranches() { refreshBranches(); }

signals:
    void changed();
    void worldImported();
    void characterImported();
    void backupRestored(QString databasePath);

private:
    using Work = std::function<void(const std::filesystem::path&, QString&, QString&)>;
    void run(Work work, bool world_import = false, bool character_import = false);
    void refreshBranches();

    std::filesystem::path database_path_;
    bool busy_{false};
    QString status_text_{QStringLiteral("世界包与人物包默认不包含 API Key")};
    QString error_text_;
    QStringList branch_names_;
    QStringList branch_ids_;
    QString comparison_text_{QStringLiteral("选择两个分支查看共同起点、状态、关系与调用成本差异")};
};
