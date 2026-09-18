#include "simulation_view_model.h"

#include "xuyan/application/simulation_service.h"
#include "xuyan/storage/workspace_repository.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>

namespace {

QString knowledgeText(const xuyan::domain::CharacterState* character) {
    if (character == nullptr) return QStringLiteral("状态不可用");
    QStringList facts;
    if (character->knows_gate_closure) facts << QStringLiteral("北门今夜封闭");
    if (character->knows_seal_forgery) facts << QStringLiteral("印章有伪造迹象");
    if (facts.isEmpty()) facts << QStringLiteral("仅掌握公开谈判信息");
    return facts.join(QStringLiteral(" · "));
}

} // namespace

SimulationViewModel::SimulationViewModel(std::filesystem::path databasePath, QObject* parent)
    : QObject(parent), database_path_(std::move(databasePath)) {
    reload();
}

QString SimulationViewModel::narration() const { return QString::fromStdString(state_.state.narration); }
QString SimulationViewModel::stateHash() const { return QString::fromStdString(state_.state_hash); }
QString SimulationViewModel::branchName() const {
    return active_branch_index_ >= 0 && active_branch_index_ < branch_names_.size()
        ? branch_names_.at(active_branch_index_) : QStringLiteral("原始路线");
}
QString SimulationViewModel::branchId() const { return QString::fromStdString(state_.branch_id); }
QString SimulationViewModel::sealStatus() const {
    const auto* holder = xuyan::domain::findCharacter(state_.state, state_.state.seal_holder_id);
    return QStringLiteral("唯一物品 · %1持有 · %2")
        .arg(holder == nullptr ? QStringLiteral("未知") : QString::fromStdString(holder->name),
             state_.state.seal_inspected ? QStringLiteral("已检查") : QStringLiteral("未检查"));
}
QString SimulationViewModel::xuKnowledge() const {
    return knowledgeText(xuyan::domain::findCharacter(state_.state, "actor-xucheng"));
}
QString SimulationViewModel::shenKnowledge() const {
    return knowledgeText(xuyan::domain::findCharacter(state_.state, "actor-shentang"));
}

QStringList SimulationViewModel::productionLog() const {
    QStringList lines;
    for (const auto& turn : production_session_.turns) {
        const auto actor = turn.actor_id == "actor-xucheng" ? QStringLiteral("许澄")
                         : turn.actor_id == "actor-shentang" ? QStringLiteral("沈棠")
                         : QString::fromStdString(turn.actor_id);
        const auto text = !turn.final_narration.empty() ? turn.final_narration
                        : !turn.draft_narration.empty() ? QStringLiteral("【草稿】").toStdString() + turn.draft_narration
                        : QStringLiteral("【%1】").arg(QString::fromStdString(turn.status)).toStdString();
        lines << QStringLiteral("%1 · %2  %3").arg(turn.ordinal).arg(actor, QString::fromStdString(text));
    }
    return lines;
}

QString SimulationViewModel::commandId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void SimulationViewModel::run(Work work, QString action) {
    if (busy_) return;
    busy_ = true;
    error_text_.clear();
    status_text_ = action;
    emit changed();

    const auto path = database_path_;
    QPointer<SimulationViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, work = std::move(work), action = std::move(action)]() mutable {
        CommitResult result;
        try {
            result = work(path);
        } catch (const std::exception& exception) {
            result = CommitResult::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                            "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), action]() mutable {
            if (self) self->applyResult(std::move(result), action);
        }, Qt::QueuedConnection);
    });
}

void SimulationViewModel::applyResult(CommitResult result, const QString& action) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = QString::fromStdString(result.error->message);
        status_text_ = QStringLiteral("操作未提交");
        emit changed();
        return;
    }
    state_ = std::move(*result.value);
    status_text_ = action == QStringLiteral("打开工作区") ? QStringLiteral("工作区已恢复")
                 : action == QStringLiteral("运行下一回合") ? QStringLiteral("回合已原子提交")
                 : action == QStringLiteral("创建分支") ? QStringLiteral("新分支已创建并切换")
                 : QStringLiteral("状态已保存");
    refreshBranches();
    refreshProductionSession();
}

void SimulationViewModel::runSession(SessionWork work, QString action) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); status_text_ = action; emit changed();
    const auto path = database_path_;
    QPointer<SimulationViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, work = std::move(work), action = std::move(action)]() mutable {
        SessionResult result;
        try { result = work(path); }
        catch (const std::exception& exception) {
            result = SessionResult::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                             "检查工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), action]() mutable {
            if (self) self->applySessionResult(std::move(result), action);
        }, Qt::QueuedConnection);
    });
}

void SimulationViewModel::applySessionResult(SessionResult result, const QString& action) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = QString::fromStdString(result.error->message);
        status_text_ = QStringLiteral("操作未提交"); emit changed(); return;
    }
    production_session_ = std::move(*result.value);
    status_text_ = action + QStringLiteral(" · 已保存");
    emit changed();
    refreshHead();
}

void SimulationViewModel::refreshProductionSession() {
    const auto path = database_path_;
    QPointer<SimulationViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::application::SimulationService service(path);
        service.recoverInterruptedSessions();
        auto result = service.sessions();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self || !result.ok()) return;
            if (!result.value->empty()) self->production_session_ = result.value->front();
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SimulationViewModel::refreshHead() {
    if (production_session_.branch_id.empty()) return;
    const auto path = database_path_;
    const auto branch = production_session_.branch_id;
    QPointer<SimulationViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, branch] {
        xuyan::storage::WorkspaceRepository repository(path);
        auto result = repository.loadHead(branch);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self || !result.ok()) return;
            self->state_ = std::move(*result.value); emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SimulationViewModel::refreshBranches() {
    const auto path = database_path_;
    QPointer<SimulationViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        xuyan::application::SimulationService service(path);
        auto result = service.branches();
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self || !result.ok()) {
                if (self) emit self->changed();
                return;
            }
            self->branch_names_.clear();
            self->branch_ids_.clear();
            self->active_branch_index_ = -1;
            for (const auto& branch : *result.value) {
                self->branch_names_ << QString::fromStdString(branch.name);
                self->branch_ids_ << QString::fromStdString(branch.id);
                if (branch.id == self->state_.branch_id) self->active_branch_index_ = self->branch_ids_.size() - 1;
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SimulationViewModel::reload() {
    run([](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.open();
    }, QStringLiteral("打开工作区"));
}

void SimulationViewModel::step() {
    const auto id = commandId().toStdString();
    run([id](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.step(id);
    }, QStringLiteral("运行下一回合"));
}

void SimulationViewModel::togglePause() {
    const auto id = commandId().toStdString();
    const bool next = !paused();
    run([id, next](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.setPaused(id, next);
    }, next ? QStringLiteral("暂停推演") : QStringLiteral("恢复推演"));
}

void SimulationViewModel::createBranch() {
    const auto id = commandId().toStdString();
    const auto name = QStringLiteral("分支 %1 · 回合 %2").arg(branch_names_.size()).arg(turn()).toStdString();
    run([id, name](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.forkCurrent(id, name);
    }, QStringLiteral("创建分支"));
}

void SimulationViewModel::selectBranch(int index) {
    if (index < 0 || index >= branch_ids_.size() || index == active_branch_index_) return;
    const auto id = branch_ids_.at(index).toStdString();
    run([id](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.switchBranch(id);
    }, QStringLiteral("切换分支"));
}

void SimulationViewModel::createProductionSession(bool continuous) {
    const auto id = commandId().toStdString();
    const auto branch = state_.branch_id;
    runSession([id, branch, continuous](const auto& path) {
        xuyan::application::SimulationService service(path);
        return service.createSession(id, branch, 20, continuous, 120);
    }, continuous ? QStringLiteral("创建连续推演会话") : QStringLiteral("创建单步推演会话"));
}

void SimulationViewModel::stepProductionSession() {
    if (!hasProductionSession()) return;
    const auto id = commandId().toStdString(); const auto session_id = production_session_.id;
    runSession([id, session_id](const auto& path) {
        return xuyan::application::SimulationService(path).stepSessionMock(id, session_id);
    }, QStringLiteral("生产会话单步"));
}

void SimulationViewModel::runProductionSession() {
    if (!hasProductionSession()) return;
    const auto id = commandId().toStdString(); const auto session_id = production_session_.id;
    runSession([id, session_id](const auto& path) {
        return xuyan::application::SimulationService(path).runSessionMock(id, session_id);
    }, QStringLiteral("连续运行"));
}

void SimulationViewModel::toggleProductionPause() {
    if (!hasProductionSession()) return;
    const auto id = commandId().toStdString(); const auto session_id = production_session_.id;
    const auto revision = production_session_.revision;
    const auto action = production_session_.status == "paused" ? std::string{"resume"} : std::string{"pause"};
    runSession([id, session_id, revision, action](const auto& path) {
        return xuyan::application::SimulationService(path).controlSession(id, session_id, revision, action);
    }, action == "resume" ? QStringLiteral("恢复生产会话") : QStringLiteral("暂停生产会话"));
}

void SimulationViewModel::cancelProductionSession() {
    if (!hasProductionSession()) return;
    const auto id = commandId().toStdString(); const auto session_id = production_session_.id;
    const auto revision = production_session_.revision;
    runSession([id, session_id, revision](const auto& path) {
        return xuyan::application::SimulationService(path).controlSession(id, session_id, revision, "cancel");
    }, QStringLiteral("停止生产会话"));
}

void SimulationViewModel::directorInspectSeal() {
    if (!hasProductionSession()) return;
    const auto id = commandId().toStdString(); const auto session_id = production_session_.id;
    runSession([id, session_id](const auto& path) {
        return xuyan::application::SimulationService(path).directorIntervene(
            id, session_id, "actor-shentang", "导演接管沈棠复核印章。", "inspect_seal");
    }, QStringLiteral("导演介入"));
}
