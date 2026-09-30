#include "package_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/package_service.h"
#include "xuyan/application/backup_service.h"
#include "xuyan/application/branch_outcome_service.h"
#include "xuyan/application/simulation_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>

#include <exception>

PackageViewModel::PackageViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) { refreshBranches(); }

void PackageViewModel::refreshBranches() {
    const auto database = database_path_; QPointer<PackageViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database] {
        xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> result;
        try { result = xuyan::application::SimulationService(database).branches(); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "分支目录读取失败", true, "检查工作区后刷新"}); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            if (!result.ok()) { self->error_text_ = view_model_text::errorText(*result.error); emit self->changed(); return; }
            self->branch_names_.clear(); self->branch_ids_.clear();
            for (const auto& branch : *result.value) {
                self->branch_names_ << QString::fromStdString(branch.name);
                self->branch_ids_ << QString::fromStdString(branch.id);
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void PackageViewModel::run(Work work, bool world_import, bool character_import) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); status_text_ = tr("正在校验和处理包…"); emit changed();
    const auto database = database_path_;
    QPointer<PackageViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database, work = std::move(work), world_import, character_import] {
        QString status;
        QString error;
        try { work(database, status, error); }
        catch (...) { error = tr("工作区操作发生内部错误，请检查资料后重试"); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, status, error, world_import, character_import] {
            if (!self) return;
            self->busy_ = false; self->status_text_ = status; self->error_text_ = error; emit self->changed();
            if (error.isEmpty() && world_import) emit self->worldImported();
            if (error.isEmpty() && character_import) emit self->characterImported();
        }, Qt::QueuedConnection);
    });
}

void PackageViewModel::exportWorld(const QUrl& destination) {
    if (!destination.isLocalFile()) return;
    const auto target = destination.toLocalFile().toStdWString();
    run([target](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        auto result = service.exportWorld(std::filesystem::path(target), "我的世界", "本地作者");
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("世界包已导出：%1 个条目").arg(result.value->entity_count);
    });
}

void PackageViewModel::importWorld(const QUrl& source) {
    if (!source.isLocalFile()) return;
    const auto path = source.toLocalFile().toStdWString();
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    run([path, command](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        auto result = service.importWorld(command, std::filesystem::path(path));
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("世界包已导入：%1 个条目").arg(result.value->entity_count);
    }, true, false);
}

void PackageViewModel::exportCharacter(QString blueprint_id, const QUrl& destination, bool include_private_notes) {
    if (!destination.isLocalFile() || blueprint_id.isEmpty()) return;
    const auto target = destination.toLocalFile().toStdWString();
    const auto id = blueprint_id.toStdString();
    run([target, id, include_private_notes](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        auto result = service.exportCharacter(id, std::filesystem::path(target), "本地作者", include_private_notes);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("人物包已导出：%1 个版本").arg(result.value->entity_count);
    });
}

void PackageViewModel::importCharacter(const QUrl& source) {
    if (!source.isLocalFile()) return;
    const auto path = source.toLocalFile().toStdWString();
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    run([path, command](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        auto result = service.importCharacter(command, std::filesystem::path(path));
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("人物包已导入：%1 个版本").arg(result.value->entity_count);
    }, false, true);
}

void PackageViewModel::createBackup(const QUrl& parent_directory) {
    if (!parent_directory.isLocalFile()) return;
    const auto name = QStringLiteral("XuyanForge-backup-%1").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const auto target = std::filesystem::path((parent_directory.toLocalFile() + QLatin1Char('/') + name).toStdWString());
    run([target](const auto& database, QString& status, QString& error) {
        xuyan::application::BackupService service(database);
        auto result = service.create(target);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("完整备份已创建：%1 个资产，%2 字节").arg(result.value->asset_count).arg(result.value->asset_bytes);
    });
}

void PackageViewModel::restoreBackup(const QUrl& backup_directory) {
    if (busy_ || !backup_directory.isLocalFile()) return;
    busy_ = true; error_text_.clear(); status_text_ = tr("正在验证数据库与全部资产摘要…"); emit changed();
    const auto source = std::filesystem::path(backup_directory.toLocalFile().toStdWString());
    const auto target_text = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/workspaces/restored-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto target = std::filesystem::path(target_text.toStdWString());
    QPointer<PackageViewModel> self(this);
    QThreadPool::globalInstance()->start([self, source, target] {
        QString status; QString error; QString database;
        xuyan::domain::Result<xuyan::application::BackupReport> result;
        try { result = xuyan::application::BackupService::restore(source, target); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "备份恢复发生内部错误", true, "检查备份与目标目录后重试"}); }
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else {
            database = QDir::toNativeSeparators(QString::fromStdWString(result.value->workspace_database.wstring()));
            status = tr("备份验证通过，正在打开恢复副本…");
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, status, error, database] {
            if (!self) return;
            self->busy_ = false; self->status_text_ = status; self->error_text_ = error; emit self->changed();
            if (error.isEmpty()) emit self->backupRestored(database);
        }, Qt::QueuedConnection);
    });
}

void PackageViewModel::compareBranches(int left_index, int right_index) {
    if (busy_ || left_index < 0 || right_index < 0 || left_index >= branch_ids_.size()
        || right_index >= branch_ids_.size() || left_index == right_index) {
        error_text_ = tr("请选择两个不同分支"); emit changed(); return;
    }
    busy_ = true; error_text_.clear(); status_text_ = tr("正在重建共同提交链…"); emit changed();
    const auto database = database_path_; const auto left = branch_ids_[left_index].toStdString();
    const auto right = branch_ids_[right_index].toStdString(); QPointer<PackageViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database, left, right] {
        xuyan::domain::Result<xuyan::domain::BranchComparison> result;
        try { result = xuyan::application::BranchOutcomeService(database).compare(left, right); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "分支比较读取失败", true, "刷新分支后重试"}); }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) self->error_text_ = view_model_text::errorText(*result.error);
            else {
                QStringList differences;
                for (const auto& item : result.value->differences)
                    differences << QStringLiteral("%1：%2 → %3").arg(QString::fromStdString(item.field), QString::fromStdString(item.left_value), QString::fromStdString(item.right_value));
                self->comparison_text_ = tr("共同提交 %1\n左：%2 次调用 / %3+%4 词元；右：%5 次调用 / %6+%7 词元\n%8")
                    .arg(QString::fromStdString(result.value->common_commit_id).left(16))
                    .arg(result.value->left_calls).arg(result.value->left_input_tokens).arg(result.value->left_output_tokens)
                    .arg(result.value->right_calls).arg(result.value->right_input_tokens).arg(result.value->right_output_tokens)
                    .arg(differences.isEmpty() ? tr("状态一致") : differences.join(QStringLiteral(" · ")));
                self->status_text_ = tr("分支比较已完成");
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void PackageViewModel::exportBranch(int branch_index, const QUrl& destination, QString format, bool technical_log) {
    if (!destination.isLocalFile() || branch_index < 0 || branch_index >= branch_ids_.size()) return;
    const auto branch = branch_ids_[branch_index].toStdString(); const auto target = destination.toLocalFile().toStdWString();
    const auto selected_format = format.toStdString();
    run([branch, target, selected_format, technical_log](const auto& database, QString& status, QString& error) {
        auto result = xuyan::application::BranchOutcomeService(database).exportBranch(branch, target, selected_format, technical_log);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("分支成果已导出");
    });
}

void PackageViewModel::exportDiagnostics(const QUrl& destination) {
    if (!destination.isLocalFile()) return;
    const auto target = destination.toLocalFile().toStdWString();
    run([target](const auto& database, QString& status, QString& error) {
        auto result = xuyan::application::BranchOutcomeService(database).exportDiagnostics(target);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("脱敏诊断摘要已导出");
    });
}

void PackageViewModel::adoptBranch(int branch_index, QString world_id, QString title) {
    if (branch_index < 0 || branch_index >= branch_ids_.size()) return;
    const auto branch = branch_ids_[branch_index].toStdString(); const auto world = world_id.toStdString();
    const auto name = title.toStdString(); const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    run([branch, world, name, command](const auto& database, QString& status, QString& error) {
        auto result = xuyan::application::BranchOutcomeService(database).adoptAsWorldVersion(command, branch, world, name);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("已作为候选素材发布世界版本 %1").arg(QString::fromStdString(result.value->id));
    }, true, false);
}
