#pragma once

#include <QObject>
#include <QVariantList>
#include <QUrl>

#include <filesystem>

/*
 * 职责：向界面提供工作区历史、真实世界目录、主题和语言设置，不预置世界资料。
 * 资源：拥有数据库路径及界面状态值；数据库连接在各次后台操作内独立创建。
 * 生命周期：由调用者或 parent 管理；公开方法在 GUI 线程调用，后台结果排队回到 GUI 线程。
 * 异步边界：对象销毁后放弃结果通知；已开始的磁盘操作不会因此自动回滚。
 */
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
    Q_PROPERTY(QString languageId READ languageId NOTIFY languageChanged)
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)

public:
    /* 功能：载入设置、登记当前工作区并异步读取世界目录。
     * 参数：database_path 为本地数据库路径（按值持有）；parent 为可空的 Qt 所有者。
     * 返回：构造后的视图模型。副作用：非截图启动会更新最近记录；不创建样例或发送网络请求。
     */
    explicit WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    /* 功能：读取最近工作区快照。参数：无。返回：最多20项的列表副本，含名称、路径及UTC打开时间；只读。 */
    QVariantList recentWorkspaces() const { return recent_; }
    /* 功能：提供当前数据库位置。参数：无。返回：使用本机路径分隔符的路径字符串；只读。 */
    QString currentPath() const;
    /* 功能：查询创建或切换操作是否在途。参数：无。返回：true表示禁止重复提交；只读。 */
    bool busy() const noexcept { return busy_; }
    /* 功能：读取当前操作提示。参数：无。返回：中文状态文本，可为空；只读。 */
    QString statusText() const { return status_text_; }
    /* 功能：读取最近一次错误。参数：无。返回：错误文本，空串表示无已记录错误；只读。 */
    QString errorText() const { return error_text_; }
    /* 功能：读取实际世界目录快照。参数：无。返回：含标识、名称、来源标识的列表副本，无世界时为空；只读。 */
    QVariantList worlds() const { return worlds_; }
    /* 功能：查询最近创建操作的小说来源。参数：无。返回：成功关联的来源标识，未导入或失败时为空；只读。 */
    QString createdSourceId() const { return created_source_id_; }
    /* 功能：查询最近导入检测到的章节数。参数：无。返回：章节个数，未成功导入时为0；只读。 */
    int createdChapterCount() const noexcept { return created_chapter_count_; }
    /* 功能：读取当前选中的世界。参数：无。返回：稳定世界标识，未选择时为空；只读。 */
    QString activeWorldId() const { return active_world_id_; }
    /* 功能：读取主题协议值。参数：无。返回：dark或light，显示名称由availableThemes提供；只读。 */
    QString themeId() const { return theme_id_; }
    /* 功能：读取语言协议值。参数：无。返回：当前仅为zh-CN，不直接作为显示标签；只读。 */
    QString languageId() const { return language_id_; }
    /* 功能：提供已验收语言选项。参数：无。返回：标识与中文名称组成的列表，目前仅简体中文；只读。 */
    QVariantList availableLanguages() const {
        return {QVariantMap{{"id", "zh-CN"}, {"name", QStringLiteral("简体中文")}}};
    }
    /* 功能：提供主题选项。参数：无。返回：深浅主题的稳定标识和中文名称列表；只读。 */
    QVariantList availableThemes() const {
        return {QVariantMap{{"id", "dark"}, {"name", QStringLiteral("深色")}},
                QVariantMap{{"id", "light"}, {"name", QStringLiteral("浅色")}}};
    }

    /* 功能：在应用数据目录分配独立工作区并异步打开。
     * 参数：name 为显示名称，去除首尾空白后须为1—120个UTF-16码元。
     * 返回：无。失败：无效名称写入errorText；打开失败保留当前进程。副作用：建目录、写库及最近记录，成功后重启切换。
     */
    Q_INVOKABLE void createWorkspace(QString name);
    /* 功能：异步打开用户指定数据库。参数：source 为本机文件URL，不接受远程地址。
     * 返回：无。失败：经errorText通知；副作用：可能迁移数据库，成功后保存最近记录并重启。
     */
    Q_INVOKABLE void openWorkspace(const QUrl& source);
    /* 功能：打开最近工作区。参数：index 为recentWorkspaces的零基索引，越界不操作。
     * 返回：无。副作用：按initializeAndSwitch执行磁盘打开及进程切换，不改变用户名称。
     */
    Q_INVOKABLE void switchToRecent(int index);
    /* 功能：移除最近列表条目而不删除数据库。参数：index 为零基索引，越界不操作。
     * 返回：无。副作用：持久化列表并发出changed。
     */
    Q_INVOKABLE void forgetRecent(int index);
    /* 功能：在线程池查询当前数据库的实际世界目录。参数：无。返回：无。
     * 失败：查询错误排队写入errorText。副作用：GUI线程更新worlds，无选择时选中第一个已有世界；不造数据。
     */
    Q_INVOKABLE void refreshWorlds();
    /* 功能：异步创建空世界，可接着导入本地小说并建立章节索引，不调用模型。
     * 参数：name 为去空白后的1—120个UTF-16码元名称；novel_file 为空表示不导入，否则必须为本机URL。
     * 返回：无。失败：busy时忽略；校验/导入错误写入errorText；世界创建与导入不是同一事务，导入失败可能保留空世界。
     * 副作用：后台写库及小说资产，GUI线程更新结果、刷新目录，成功时发出worldCreated。
     */
    Q_INVOKABLE void createWorld(QString name, const QUrl& novel_file);
    /* 功能：切换当前世界。参数：index 为worlds的零基索引，越界不操作。返回：无。副作用：更新标识并发出changed。 */
    Q_INVOKABLE void selectWorld(int index);
    /* 功能：设置主题。参数：theme_id 只接受dark或light；相同值或非法值不操作。
     * 返回：无。副作用：持久化设置并发出changed，不修改世界数据。
     */
    Q_INVOKABLE void setThemeId(QString theme_id);
    /* 功能：设置已开放的语言。参数：language_id 当前只接受zh-CN；相同值或非法值不操作。
     * 返回：无。副作用：有效变更写入设置并发出languageChanged及changed，以便后续语言扩展重译。
     */
    Q_INVOKABLE void setLanguageId(QString language_id);

signals:
    /* 功能：通知界面重新读取可变状态。参数：无。返回：无；接收方在所属线程处理。 */
    void changed();
    /* 功能：通知世界创建及可选导入均已成功。参数：无。返回：无；接收方可刷新小说列表。 */
    void worldCreated();
    /* 功能：通知语言设置发生有效改变。参数：无。返回：无；接收方负责加载翻译并重译界面。 */
    void languageChanged();

private:
    /* 功能：读取有效最近路径并迁移旧自动标签，不读取数据库正文。参数：无。返回：无。
     * 副作用：填充recent_；忽略失效路径和旧开发产物记录，不删除文件。
     */
    void loadRecent();
    /* 功能：按路径去重并将工作区置于最近列表首位。
     * 参数：name 为可选显示名称，空时保留已有名称或使用中文默认名；path 为本机数据库路径。
     * 返回：无。副作用：保留最多20项、记录UTC时间、写入设置并发出changed。
     */
    void registerRecent(QString name, const QString& path);
    /* 功能：将recent_完整替换到当前应用设置。参数：无。返回：无。
     * 副作用：写入并同步QSettings；当前实现不向界面传播QSettings同步错误。
     */
    void saveRecent();
    /* 功能：在线程池初始化数据库，成功后切换进程。参数：name 为可选显示名称；path 为按值捕获的本机数据库路径。
     * 返回：无。失败：busy时忽略，打开失败在GUI线程设置错误且不重启；对象销毁后不通知。
     * 副作用：可能创建/迁移数据库，成功后保存最近记录并调用restartAt；网络不参与。
     */
    void initializeAndSwitch(QString name, std::filesystem::path path);
    /* 功能：启动同一程序打开指定数据库。参数：path 为本机数据库路径，作为单独参数传递。
     * 返回：无。副作用：启动独立进程并退出当前事件循环；当前实现未反馈启动失败，属于待补错误路径。
     */
    static void restartAt(const QString& path);

    // 当前实例绑定的数据库路径；构造时确定，后台任务复制使用，实例内不切换。
    std::filesystem::path database_path_;
    // 最近工作区记录列表，最多20项；载入/登记/遗忘操作更新，供侧栏读取。
    QVariantList recent_;
    // 创建或切换是否在途，初始false；GUI线程置位及处理完成回调时复位。
    bool busy_{false};
    // 当前操作的中文进度说明；初始为已打开，供状态栏读取。
    QString status_text_{QStringLiteral("当前工作区已打开")};
    // 最近失败文本，初始为空；操作入口清理，校验及后台完成回调写入。
    QString error_text_;
    // 当前数据库真实世界的轻量列表；初始为空，由refreshWorlds的GUI回调替换。
    QVariantList worlds_;
    // 最近创建并成功关联的小说来源标识；空表示无成功导入，供界面跳转使用。
    QString created_source_id_;
    // 最近成功导入的章节个数，非字数；初始0，每次创建开始重置。
    int created_chapter_count_{0};
    // 当前选中的世界稳定标识；初始空，由创建、选择或首次目录加载更新。
    QString active_world_id_;
    // 主题内部标识，默认dark；从设置恢复并校验，界面通过中文选项选择。
    QString theme_id_{QStringLiteral("dark")};
    // 语言内部标识，默认且当前仅允许zh-CN；预留未来完成翻译后的扩展。
    QString language_id_{QStringLiteral("zh-CN")};
};
