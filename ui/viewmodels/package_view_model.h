#pragma once

#include <QObject>
#include <QUrl>
#include <QStringList>

#include <filesystem>
#include <functional>

/*
 * 职责：提供世界/人物包、备份恢复及分支成果操作；凭据不进入包，用户资料只在显式操作时写出。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class PackageViewModel final : public QObject {
    Q_OBJECT
    /* 属性：查询包或分支操作是否在途；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：读取包处理结果说明；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /* 属性：读取包处理中文失败提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：读取分支显示名称；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QStringList branchNames READ branchNames NOTIFY changed)
    /* 属性：读取两分支比较说明；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString comparisonText READ comparisonText NOTIFY changed)

public:
    /* 功能：只读检查本地导出目标是否存在，为中文覆盖确认提供依据。
     * 参数：destination为调用期间借用的完整URL，仅接受本地文件地址；不保存引用。
     * 返回：本地路径已存在为true（目录也算存在）；非本地或不存在为false。
     * 失败：Qt路径分配异常传播；不证明文件可写或消除确认后的外部竞态，导出仍校验写入。
     * 副作用：只读文件元数据，不写文件、不打开数据库；仅GUI线程调用。 */
    Q_INVOKABLE bool destinationExists(const QUrl& destination) const;
    /*
     * 功能：绑定本地工作区并异步读取分支目录
     * 参数：database_path：输入，实例持有的本机数据库路径；parent：输入，可空 Qt 所有者，默认空。
     * 返回：完成成员初始化并排队分支查询。
     * 失败：路径分配异常传播，后台失败显示中文提示。
     * 副作用：只读取分支，不导出数据。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit PackageViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /*
     * 功能：查询包或分支操作是否在途
     * 参数：无。
     * 返回：true 禁止重复任务，默认false；分支目录刷新不占用此标志。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }
    /*
     * 功能：读取包处理结果说明
     * 参数：无。
     * 返回：中文文本，可为空，不包含密钥。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }
    /*
     * 功能：读取包处理中文失败提示
     * 参数：无。
     * 返回：空表示无当前失败，不包含底层异常。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }
    /*
     * 功能：读取分支显示名称
     * 参数：无。
     * 返回：列表副本，名称来自用户资料，与私有 branch_ids_ 按索引对齐。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QStringList branchNames() const { return branch_names_; }
    /*
     * 功能：读取两分支比较说明
     * 参数：无。
     * 返回：最近成功比较的中文计数和差异文本，未比较时为选择提示。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString comparisonText() const { return comparison_text_; }

    /*
     * 功能：导出当前工作区首个世界资料包，现有服务尚未接入当前世界选择参数。
     * 参数：destination：输入，必须为本机文件 URL；非本机地址忽略。
     * 返回：无。
     * 失败：busy 时 run 忽略；包验证或写出失败经 errorText 通知。
     * 副作用：后台显式写目标包；标题读取实际世界名称，未知作者留空；不包含密钥，GUI显示条目数量。
     * 线程与生命周期：GUI 捕获目标路径，线程池独立 PackageService 写出，GUI 回填；销毁不撤销文件写入。
     */
    Q_INVOKABLE void exportWorld(const QUrl& destination);
    /*
     * 功能：校验并导入本地世界包
     * 参数：source：输入，本机文件 URL；非本机地址忽略。
     * 返回：无。
     * 失败：busy 时忽略；包损坏、输入无效或存储失败显示中文提示。
     * 副作用：后台写世界资料及幂等命令；成功发 worldImported。
     * 线程与生命周期：GUI 发起，线程池执行，GUI 回填；对象销毁不回滚已提交导入。
     */
    Q_INVOKABLE void importWorld(const QUrl& source);
    /*
     * 功能：显式导出选定人物卡及可选私密笔记
     * 参数：blueprint_id：输入，非空卡 ID；destination：输入，本机目标文件 URL；include_private_notes：输入，true 才导出私密笔记，false 排除。
     * 返回：无。
     * 失败：非本机目标、空 ID 或 busy 忽略；加载/包写出失败显示提示。
     * 副作用：后台导出人物包，未知作者留空，不包含系统密钥；GUI显示版本数量。
     * 线程与生命周期：按值捕获路径、ID及隐私选项，线程池写出，GUI 回填；销毁不撤销导出。
     */
    Q_INVOKABLE void exportCharacter(QString blueprint_id, const QUrl& destination, bool include_private_notes);
    /*
     * 功能：校验并导入人物包
     * 参数：source：输入，本机包文件 URL；非本机忽略。
     * 返回：无。
     * 失败：busy 时忽略；格式/包/存储错误显示中文提示。
     * 副作用：后台写人物版本及命令；成功发 characterImported。
     * 线程与生命周期：线程池执行，GUI 回填；销毁不回滚已提交导入。
     */
    Q_INVOKABLE void importCharacter(const QUrl& source);
    /*
     * 功能：在选定目录下创建带 UTC 时间名的独立备份
     * 参数：parent_directory：输入，本机父目录 URL，不接受远程地址。
     * 返回：无。
     * 失败：busy 或非本机忽略；同名目录或磁盘失败由备份服务返回错误。
     * 副作用：写出真实数据库及资产副本，报告资产个数和字节总量，不写密钥。
     * 线程与生命周期：GUI 发起，全局线程池备份，GUI 回填；销毁不取消已开始备份。
     */
    Q_INVOKABLE void createBackup(const QUrl& parent_directory);
    /*
     * 功能：验证备份并恢复到应用数据下唯一新目录
     * 参数：backup_directory：输入，本机备份目录 URL。
     * 返回：无。
     * 失败：busy 或非本机忽略；摘要/文件/目录失败显示中文提示。
     * 副作用：创建恢复副本；成功发 backupRestored，是否切换由接收者处理。
     * 线程与生命周期：线程池恢复，GUI 发出路径信号；对象销毁不删除恢复副本。
     */
    Q_INVOKABLE void restoreBackup(const QUrl& backup_directory);
    /*
     * 功能：读取两个不同分支的共同提交及差异
     * 参数：left_index：输入，左分支零基索引；right_index：输入，右分支零基索引；均须有效且不同。
     * 返回：无。
     * 失败：busy、越界或同分支显示选择提示；服务失败经 errorText 通知。
     * 副作用：后台只读分支；GUI 更新比较文本、状态及 changed。
     * 线程与生命周期：GUI 捕获两个稳定 ID，线程池读取，GUI 应用结果，销毁后不通知。
     */
    Q_INVOKABLE void compareBranches(int left_index, int right_index);
    /*
     * 功能：导出明确选定的分支成果
     * 参数：branch_index：零基有效索引；destination：本机文件 URL；format：原始导出协议值，由服务校验；technical_log：是否包含技术日志；均为输入。
     * 返回：无。
     * 失败：索引无效、非本机或 busy 忽略；格式/文件失败显示中文提示。
     * 副作用：后台写用户指定成果文件，GUI 通知。
     * 线程与生命周期：线程池执行，GUI 回填；销毁不撤销已写文件。
     */
    Q_INVOKABLE void exportBranch(int branch_index, const QUrl& destination, QString format, bool technical_log);
    /*
     * 功能：显式导出脱敏诊断摘要
     * 参数：destination：输入，本机目标文件 URL。
     * 返回：无。
     * 失败：非本机或 busy 忽略；读写失败显示中文提示。
     * 副作用：后台写脱敏摘要，服务负责排除原文和秘密。
     * 线程与生命周期：GUI 捕获路径，线程池写出，GUI 回填；对象销毁不撤销已写文件。
     */
    Q_INVOKABLE void exportDiagnostics(const QUrl& destination);
    /*
     * 功能：将选中分支作为候选素材发布到指定世界版本
     * 参数：branch_index：有效零基索引；world_id：目标稳定世界 ID；title：用户提供的版本标题；均为输入，后两项由服务校验。
     * 返回：无。
     * 失败：无效索引或 busy 忽略；世界/分支/输入或存储失败显示提示。
     * 副作用：后台发布版本及幂等命令；成功发 worldImported 通知相关列表刷新。
     * 线程与生命周期：GUI 捕获 ID 与标题，线程池执行，GUI 通知；销毁不回滚发布。
     */
    Q_INVOKABLE void adoptBranch(int branch_index, QString world_id, QString title);
    /*
     * 功能：异步刷新分支目录
     * 参数：无。
     * 返回：无。
     * 失败：读库失败显示提示。
     * 副作用：调用 refreshBranches，更新目录并发 changed。
     * 线程与生命周期：GUI 发起，线程池查询，GUI 回填；不等待任务完成。
     */
    Q_INVOKABLE void reloadBranches() { refreshBranches(); }

signals:
    /*
     * 功能：通知界面重读包状态和分支目录
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收者结果。
     * 副作用：调用 Qt 已连接槽。
     * 线程与生命周期：GUI 发出。
     */
    void changed();
    /*
     * 功能：通知世界包导入或分支采纳已成功
     * 参数：无。
     * 返回：无。
     * 失败：失败时不发送。
     * 副作用：接收者可刷新世界列表；不附加写入。
     * 线程与生命周期：GUI 在操作成功后发出。
     */
    void worldImported();
    /*
     * 功能：通知人物包导入已成功
     * 参数：无。
     * 返回：无。
     * 失败：失败时不发送。
     * 副作用：接收者可刷新人物卡。
     * 线程与生命周期：GUI 在操作成功后发出。
     */
    void characterImported();
    /*
     * 功能：通知恢复副本已创建并可尝试打开
     * 参数：databasePath：输出信号参数，本机恢复数据库路径，值对象，不保证接收者切换成功。
     * 返回：无。
     * 失败：恢复失败不发送；不处理接收方打开错误。
     * 副作用：接收者可发起工作区切换，原库未在此处覆盖。
     * 线程与生命周期：GUI 发出，排队接收者拥有参数副本。
     */
    void backupRestored(QString databasePath);

private:
    /*
     * 回调契约：在线程池同步读取给定数据库并输出中文状态/错误；引用只在该次调用有效。
     * 返回无；失败允许抛异常由 run 隔离；具体磁盘副作用由各操作 lambda 明示，不访问 GUI。
     */
    using Work = std::function<void(const std::filesystem::path&, QString&, QString&)>;
    /*
     * 功能：串行入口投递一次包或分支工作
     * 参数：work：输入，按值拥有的同步工作回调；接收数据库路径及状态/错误输出引用，须只在调用期间使用；world_import：成功是否发世界通知，默认false；character_import：成功是否发人物通知，默认false。
     * 返回：无。
     * 失败：busy 时忽略；回调异常转换安全中文错误。
     * 副作用：设置 busy，线程池执行 work；GUI 应用输出和条件通知。
     * 线程与生命周期：回调在线程池同步执行，不借用 GUI 对象；QPointer 失效不回填，后台写入不自动取消。
     */
    void run(Work work, bool world_import = false, bool character_import = false);
    /*
     * 功能：后台读取分支显示目录
     * 参数：无。
     * 返回：无。
     * 失败：查询失败保留旧目录并写中文提示。
     * 副作用：GUI 替换对齐名称/ID列表并发 changed，不设 busy。
     * 线程与生命周期：全局线程池读库，GUI 回填；销毁后放弃通知。
     */
    void refreshBranches();

    /* 本机数据库路径，构造确定，实例持有；后台按值复制。 */
    std::filesystem::path database_path_;
    /* 包/比较/恢复操作在途标志，默认false；GUI 设置与完成回调复位。 */
    bool busy_{false};
    /* 包处理中文状态；初始说明包不含模型密钥，GUI 更新。 */
    QString status_text_{QStringLiteral("世界包与人物包默认不包含模型密钥")};
    /* 包处理中文错误，默认空；GUI 写入，不含异常正文。 */
    QString error_text_;
    /* 分支显示名列表，默认空；GUI 成功读目录替换，无额外数量上限。 */
    QStringList branch_names_;
    /* 与 branch_names_ 对齐的稳定 ID 列表，默认空；GUI 捕获操作 ID，不暴露给名称标签。 */
    QStringList branch_ids_;
    /* 最近成功比较文本；初始为选择说明，GUI 比较回调更新。 */
    QString comparison_text_{QStringLiteral("选择两个分支查看共同起点、状态、关系与调用成本差异")};
};
