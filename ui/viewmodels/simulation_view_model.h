#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <QObject>
#include <QStringList>

#include <filesystem>
#include <functional>

class SimulationViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool completed READ completed NOTIFY changed)
    Q_PROPERTY(int turn READ turn NOTIFY changed)
    Q_PROPERTY(QString narration READ narration NOTIFY changed)
    Q_PROPERTY(QString stateHash READ stateHash NOTIFY changed)
    Q_PROPERTY(QString branchName READ branchName NOTIFY changed)
    Q_PROPERTY(QString branchId READ branchId NOTIFY changed)
    Q_PROPERTY(QString sealStatus READ sealStatus NOTIFY changed)
    Q_PROPERTY(QString xuKnowledge READ xuKnowledge NOTIFY changed)
    Q_PROPERTY(QString shenKnowledge READ shenKnowledge NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QStringList branchNames READ branchNames NOTIFY changed)
    Q_PROPERTY(int activeBranchIndex READ activeBranchIndex NOTIFY changed)
    Q_PROPERTY(bool hasProductionSession READ hasProductionSession NOTIFY changed)
    Q_PROPERTY(QString productionSessionId READ productionSessionId NOTIFY changed)
    Q_PROPERTY(QString productionStatus READ productionStatus NOTIFY changed)
    Q_PROPERTY(int productionTurnCount READ productionTurnCount NOTIFY changed)
    Q_PROPERTY(int productionMaxTurns READ productionMaxTurns NOTIFY changed)
    Q_PROPERTY(int productionUsedCalls READ productionUsedCalls NOTIFY changed)
    Q_PROPERTY(int productionMaxCalls READ productionMaxCalls NOTIFY changed)
    Q_PROPERTY(int productionUnknownCalls READ productionUnknownCalls NOTIFY changed)
    Q_PROPERTY(bool productionContinuous READ productionContinuous NOTIFY changed)
    Q_PROPERTY(QStringList productionLog READ productionLog NOTIFY changed)

public:
    explicit SimulationViewModel(std::filesystem::path databasePath, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    bool paused() const noexcept { return state_.state.paused; }
    bool completed() const noexcept { return state_.state.completed; }
    int turn() const noexcept { return state_.state.turn; }
    QString narration() const;
    QString stateHash() const;
    QString branchName() const;
    QString branchId() const;
    QString sealStatus() const;
    QString xuKnowledge() const;
    QString shenKnowledge() const;
    QString errorText() const { return error_text_; }
    QString statusText() const { return status_text_; }
    QStringList branchNames() const { return branch_names_; }
    int activeBranchIndex() const noexcept { return active_branch_index_; }
    bool hasProductionSession() const noexcept { return !production_session_.id.empty(); }
    QString productionSessionId() const { return QString::fromStdString(production_session_.id); }
    QString productionStatus() const { return QString::fromStdString(production_session_.status); }
    int productionTurnCount() const noexcept { return static_cast<int>(production_session_.turns.size()); }
    int productionMaxTurns() const noexcept { return production_session_.max_turns; }
    int productionUsedCalls() const noexcept { return production_session_.used_calls; }
    int productionMaxCalls() const noexcept { return production_session_.max_calls; }
    int productionUnknownCalls() const noexcept { return production_session_.unknown_calls; }
    bool productionContinuous() const noexcept { return production_session_.continuous; }
    QStringList productionLog() const;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void step();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void createBranch();
    Q_INVOKABLE void selectBranch(int index);
    Q_INVOKABLE void createProductionSession(bool continuous);
    Q_INVOKABLE void stepProductionSession();
    Q_INVOKABLE void runProductionSession();
    Q_INVOKABLE void toggleProductionPause();
    Q_INVOKABLE void cancelProductionSession();
    Q_INVOKABLE void directorInspectSeal();

signals:
    void changed();

private:
    using CommitResult = xuyan::domain::Result<xuyan::domain::CommitView>;
    using Work = std::function<CommitResult(const std::filesystem::path&)>;
    using SessionResult = xuyan::domain::Result<xuyan::domain::SimulationSession>;
    using SessionWork = std::function<SessionResult(const std::filesystem::path&)>;

    void run(Work work, QString action);
    void applyResult(CommitResult result, const QString& action);
    void refreshBranches();
    void runSession(SessionWork work, QString action);
    void applySessionResult(SessionResult result, const QString& action);
    void refreshProductionSession();
    void refreshHead();
    static QString commandId();

    std::filesystem::path database_path_;
    xuyan::domain::CommitView state_;
    bool busy_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("正在打开工作区…")};
    QStringList branch_names_;
    QStringList branch_ids_;
    int active_branch_index_{-1};
    xuyan::domain::SimulationSession production_session_;
};
