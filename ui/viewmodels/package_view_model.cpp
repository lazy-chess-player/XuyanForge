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
#include <QFileInfo>

#include <exception>

PackageViewModel::PackageViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) { refreshBranches(); }

void PackageViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++world_generation_;
    error_text_.clear();
    status_text_ = busy_ ? tr("文件操作正在后台执行，切换世界不会改变已开始的导出")
                        : world_id_.isEmpty() ? tr("请选择世界后导出资料包") : tr("已选择世界，可导出当前资料包");
    emit changed();
}

bool PackageViewModel::destinationExists(const QUrl& destination) const {
    return destination.isLocalFile() && QFileInfo::exists(destination.toLocalFile());
}

void PackageViewModel::refreshBranches() {
    const auto database = database_path_; QPointer<PackageViewModel> self(this);
    /* 功能：在线程池读取当前工作区实际分支目录，既不创建分支也不自动导出。
     * 参数：无；database按值持有，self只观察模型。返回：无。
     * 失败：服务失败保留Result错误，异常转为中文存储错误；模型销毁不排队回填。
     * 副作用：局部服务可能打开/初始化数据库，查询结果回GUI；不占用文件操作busy、不发送网络。 */
    QThreadPool::globalInstance()->start([self, database] {
        xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> result;
        try { result = xuyan::application::SimulationService(database).branches(); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "分支目录读取失败", true, "检查工作区后刷新"}); }
        if (!self) return;
        /* 功能：在GUI应用分支目录或错误，并保持名称和稳定标识索引一致。
         * 参数：无；result为拥有型移动捕获，self观察模型。返回：无。
         * 失败：模型销毁无动作；查询失败保留旧目录并显示安全中文提示。
         * 副作用：更新目录或错误并发changed；工作区级目录不使用世界代次。 */
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

void PackageViewModel::run(Work work, bool world_import, bool character_import,
                           std::optional<quint64> world_generation) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); status_text_ = tr("正在校验和处理包…"); emit changed();
    const auto database = database_path_;
    QPointer<PackageViewModel> self(this);
    /* 功能：在独立工作线程执行已冻结参数的文件用例，并将值结果排队回GUI。
     * 参数：无；database/work及通知标志按值持有，self只观察模型，world_generation为可空选择快照。
     * 返回：无。失败：回调异常转为安全中文错误，模型销毁则放弃通知。
     * 副作用：具体文件读写由work决定；不直接修改GUI成员，不在销毁后重新创建模型。 */
    QThreadPool::globalInstance()->start([self, database, work = std::move(work), world_import, character_import, world_generation] {
        QString status;
        QString error;
        try { work(database, status, error); }
        catch (...) { error = tr("工作区操作发生内部错误，请检查资料后重试"); }
        if (!self) return;
        /* 功能：在GUI结算文件操作，世界代次不符时只解除单操作忙碌锁。
         * 参数：无；status/error/通知标志/可空代次为值捕获，self不拥有模型。
         * 返回：无。失败：模型已销毁则无动作；不能撤销后台已完成的文件写入。
         * 副作用：有效结果更新中文说明并发信号；旧世界结果不覆盖新选择或发送业务完成通知。 */
        QMetaObject::invokeMethod(self, [self, status, error, world_import, character_import, world_generation] {
            if (!self) return;
            if (world_generation && *world_generation != self->world_generation_) {
                self->busy_ = false;
                self->status_text_ = self->world_id_.isEmpty() ? tr("请选择世界后导出资料包")
                                                            : tr("已选择世界，可导出当前资料包");
                emit self->changed();
                return;
            }
            self->busy_ = false; self->status_text_ = status; self->error_text_ = error; emit self->changed();
            if (error.isEmpty() && world_import) emit self->worldImported();
            if (error.isEmpty() && character_import) emit self->characterImported();
        }, Qt::QueuedConnection);
    });
}

void PackageViewModel::exportWorld(const QUrl& destination) {
    if (busy_) return;
    if (world_id_.isEmpty() || !destination.isLocalFile() || destination.toLocalFile().isEmpty()) {
        error_text_ = world_id_.isEmpty() ? tr("请先在项目列表选择要导出的世界") : tr("请选择有效的本地导出路径");
        emit changed();
        return;
    }
    const auto target = destination.toLocalFile().toStdWString();
    const auto world = world_id_.toStdString();
    const auto generation = world_generation_;
    /* 功能：后台读取实际世界并导出不含凭据的包，未填元数据不伪造标题/作者。
     * 参数：database为run提供的数据库路径借用；status/error为输出引用，写成功说明或失败提示。
     * 返回：无。失败：服务Result错误映射中文；构造/标准及其他异常由外层run捕获。
     * 副作用：读取数据库并写target包，成功更新status、失败更新error；不发送网络。
     * 线程与生命周期：线程池同步执行，target/world按值持有；参数仅本次回调有效，GUI回填由run负责。 */
    run([target, world](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        // 标题由服务读取实际世界名称；未填写作者保持未知，不预置任何名称或署名。
        auto result = service.exportWorld(world, std::filesystem::path(target), {}, {});
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("世界包已导出：%1 个条目").arg(result.value->entity_count);
    }, false, false, generation);
}

void PackageViewModel::importWorld(const QUrl& source) {
    if (!source.isLocalFile()) return;
    const auto path = source.toLocalFile().toStdWString();
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    /* 功能：后台校验世界条目包并按幂等命令原子导入。
     * 参数：database为借用的数据库路径，status/error为本次输出引用；path/command按值冻结。
     * 返回：无。失败：服务失败映射中文，异常由run隔离；不返回部分成功。
     * 副作用：读取指定包并提交条目与命令，不自动选择世界、不发送网络；仅线程池调用。 */
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
    /* 功能：后台导出明确人物卡版本，保留隐私选项且未知作者留空。
     * 参数：database为run提供的路径借用；status/error为本次输出引用，成功写版本数或失败写中文提示。
     * 返回：无。失败：服务Result映射中文，异常由外层run捕获，不把部分失败报告成成功。
     * 副作用：读取卡片并写target包，是否含私人备注由include_private_notes控制，不读取系统密钥。
     * 线程与生命周期：线程池同步执行；target/id/隐私标志按值持有，参数仅回调有效，GUI回填由run负责。 */
    run([target, id, include_private_notes](const auto& database, QString& status, QString& error) {
        xuyan::application::PackageService service(database);
        auto result = service.exportCharacter(id, std::filesystem::path(target), {}, include_private_notes);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("人物包已导出：%1 个版本").arg(result.value->entity_count);
    });
}

void PackageViewModel::importCharacter(const QUrl& source) {
    if (!source.isLocalFile()) return;
    const auto path = source.toLocalFile().toStdWString();
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    /* 功能：后台验证人物包并一次导入版本集合。
     * 参数：database为run借用路径，status/error为调用期间输出引用；path/command按值持有。
     * 返回：无。失败：服务错误映射中文，异常交run处理。
     * 副作用：只读指定包，提交人物版本及幂等命令，成功通知由run回GUI；不读系统凭据、不联网。 */
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
    /* 功能：后台创建完整数据库及资产备份，条目包不能替代本用例。
     * 参数：database为借用源路径，status/error为本次输出引用，target按值持有新目录路径。
     * 返回：无。失败：服务Result失败映射中文，异常由run处理；不把失败统计当作备份完成。
     * 副作用：创建暂存/备份目录及快照文件，成功写资产计数/字节，不备份系统凭据；线程池同步执行。 */
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
    /* 功能：在线程池验证备份并恢复到新的唯一目录，原工作区保持不变。
     * 参数：无；source/target为拥有型路径，self为弱观察指针。返回：无。
     * 失败：服务错误映射中文，异常转为安全错误，销毁模型则不通知。
     * 副作用：读备份并写恢复副本，生成数据库路径供GUI决定是否打开；不读取系统凭据。 */
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
        /* 功能：在GUI结算恢复状态，成功通知目录模型尝试打开副本。
         * 参数：无；status/error/database为值捕获，self只观察。返回：无。
         * 失败：模型销毁无动作，恢复失败不发backupRestored；接收者打开失败不在这里处理。
         * 副作用：释放busy、更新说明并发信号，不覆盖或删除原库；只在GUI线程执行。 */
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
    /* 功能：后台读取两个已明确选择分支的共同提交和现有差异协议。
     * 参数：无；database/left/right按值捕获，self观察模型。返回：无。
     * 失败：服务Result保留错误，异常转中文存储错误；模型销毁不回填。
     * 副作用：只读分支/提交/会话，可触发结构初始化，不发送网络或改写世界事实；线程池执行。 */
    QThreadPool::globalInstance()->start([self, database, left, right] {
        xuyan::domain::Result<xuyan::domain::BranchComparison> result;
        try { result = xuyan::application::BranchOutcomeService(database).compare(left, right); }
        catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "分支比较读取失败", true, "刷新分支后重试"}); }
        if (!self) return;
        /* 功能：在GUI更新现有比较说明和调用统计；不把差异文本当作新事实。
         * 参数：无；result为移动拥有值，self为弱观察。返回：无。
         * 失败：查询失败写安全错误并保留原比较文本，销毁模型无动作。
         * 副作用：解除busy、更新比较/状态并发changed；原始差异协议的通用化属于XF-25，未在此完成。 */
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
    /* 功能：后台按明确分支和格式导出已提交成果。
     * 参数：database为run借用路径，status/error为本次输出引用；branch/target/格式/日志选择按值捕获。
     * 返回：无。失败：服务Result错误映射中文，异常由run处理。
     * 副作用：查询分支并写target文件，technical_log决定是否含技术日志；不自动采纳世界事实，线程池执行。 */
    run([branch, target, selected_format, technical_log](const auto& database, QString& status, QString& error) {
        auto result = xuyan::application::BranchOutcomeService(database).exportBranch(branch, target, selected_format, technical_log);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("分支成果已导出");
    });
}

void PackageViewModel::exportDiagnostics(const QUrl& destination) {
    if (!destination.isLocalFile()) return;
    const auto target = destination.toLocalFile().toStdWString();
    /* 功能：后台生成现有脱敏诊断摘要，而非复制小说或模型详细输出。
     * 参数：database为借用工作区路径，status/error为本次输出引用，target按值冻结。
     * 返回：无。失败：服务错误映射中文，异常由run隔离。
     * 副作用：查询聚合摘要并写目标文件，不读取系统凭据；仅工作线程执行，GUI结算由run负责。 */
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
    /* 功能：后台将明确分支的现有可采纳素材交世界版本服务处理。
     * 参数：database为调用期间借用路径，status/error为输出引用；branch/world/name/command按值捕获。
     * 返回：无。失败：服务校验/幂等/存储错误映射中文，异常由run隔离。
     * 副作用：可能创建候选素材并发布版本，成功由run通知刷新；不能由本回调保证通用推演和全字段采纳完成。 */
    run([branch, world, name, command](const auto& database, QString& status, QString& error) {
        auto result = xuyan::application::BranchOutcomeService(database).adoptAsWorldVersion(command, branch, world, name);
        if (!result.ok()) error = view_model_text::errorText(*result.error);
        else status = tr("已作为候选素材发布世界版本 %1").arg(QString::fromStdString(result.value->id));
    }, true, false);
}
