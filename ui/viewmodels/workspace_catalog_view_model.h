#pragma once

#include <QObject>
#include <QVariantList>
#include <QUrl>

#include <filesystem>
#include <cstdint>

/*
 * 职责：提供真实世界目录、工作区历史、中文语言及主题设置；应用服务隔离存储，不预置内容。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class WorkspaceCatalogViewModel final : public QObject {
    Q_OBJECT
    /* 属性：读取有效最近工作区记录；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList recentWorkspaces READ recentWorkspaces NOTIFY changed)
    /* 属性：读取当前实例绑定数据库路径；实例内恒定，不发送变更通知。 */
    Q_PROPERTY(QString currentPath READ currentPath CONSTANT)
    /* 属性：查询世界创建或工作区打开是否在途；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：读取当前项目操作提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /* 属性：读取当前项目操作错误；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：读取真实世界轻量目录；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY changed)
    /* 属性：读取最近创建操作成功关联的小说来源；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString createdSourceId READ createdSourceId NOTIFY changed)
    /* 属性：读取最近成功关联小说的章节数；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int createdChapterCount READ createdChapterCount NOTIFY changed)
    /* 属性：读取当前选中世界 ID；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString activeWorldId READ activeWorldId NOTIFY changed)
    /* 属性：读取当前主题协议值；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    /* 属性：提供主题选项；实例内恒定，不发送变更通知。 */
    Q_PROPERTY(QVariantList availableThemes READ availableThemes CONSTANT)
    /* 属性：读取当前语言协议值；languageChanged 通知重读；changed 同时刷新页面。 */
    Q_PROPERTY(QString languageId READ languageId NOTIFY languageChanged)
    /* 属性：提供当前已开放语言选项；实例内恒定，不发送变更通知。 */
    Q_PROPERTY(QVariantList availableLanguages READ availableLanguages CONSTANT)

public:

    /*
     * 功能：加载外观设置、登记当前工作区并异步读世界目录
     * 参数：database_path：输入，实例持有的本机数据库路径；parent：输入，可空 Qt 所有者，默认空。
     * 返回：完成目录和设置初始化并发起读取。
     * 失败：路径/QObject 分配异常传播；目录服务异常转中文错误。
     * 副作用：非截图启动写最近记录；只读实际世界目录，不造样例或联网。
     * 线程与生命周期：GUI 构造，线程池查目录，GUI 按代次回填。
     */
    explicit WorkspaceCatalogViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /*
     * 功能：读取有效最近工作区记录
     * 参数：无。
     * 返回：列表副本，含用户名称、路径及 UTC 打开时间；新登记保留最多20项。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList recentWorkspaces() const { return recent_; }

    /*
     * 功能：读取当前实例绑定数据库路径
     * 参数：无。
     * 返回：本机分隔符的绝对/传入路径文本；实例内不切换数据库。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString currentPath() const;

    /*
     * 功能：查询世界创建或工作区打开是否在途
     * 参数：无。
     * 返回：true 防止重复提交；目录刷新不占用此标志。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }

    /*
     * 功能：读取当前项目操作提示
     * 参数：无。
     * 返回：中文文本，初始为工作区已打开。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }

    /*
     * 功能：读取当前项目操作错误
     * 参数：无。
     * 返回：中文安全文本，空表示无当前失败。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：读取真实世界轻量目录
     * 参数：无。
     * 返回：ID、用户名称、来源 ID 的列表副本；未加载或空库为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList worlds() const { return worlds_; }

    /*
     * 功能：读取最近创建操作成功关联的小说来源
     * 参数：无。
     * 返回：稳定来源 ID；未导入、导入/关联失败或新操作开始时为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString createdSourceId() const { return created_source_id_; }

    /*
     * 功能：读取最近成功关联小说的章节数
     * 参数：无。
     * 返回：章节个数，初始/无成功导入为0，不是字数。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int createdChapterCount() const noexcept { return created_chapter_count_; }

    /*
     * 功能：读取当前选中世界 ID
     * 参数：无。
     * 返回：未选择为空；创建失败保留旧选择。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString activeWorldId() const { return active_world_id_; }

    /*
     * 功能：读取当前主题协议值
     * 参数：无。
     * 返回：dark 或 light，初始 dark；显示标签单独提供。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString themeId() const { return theme_id_; }

    /*
     * 功能：读取当前语言协议值
     * 参数：无。
     * 返回：当前只允许 zh-CN，不作为显示标签。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString languageId() const { return language_id_; }

    /*
     * 功能：提供当前已开放语言选项
     * 参数：无。
     * 返回：标识与中文名称的列表副本，目前仅简体中文。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList availableLanguages() const {
        return {QVariantMap{{"id", "zh-CN"}, {"name", tr("简体中文")}}};
    }

    /*
     * 功能：提供主题选项
     * 参数：无。
     * 返回：dark/light 与可翻译的深色/浅色名称列表。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList availableThemes() const {
        return {QVariantMap{{"id", "dark"}, {"name", tr("深色")}},
                QVariantMap{{"id", "light"}, {"name", tr("浅色")}}};
    }


    /*
     * 功能：创建独立工作区目录并尝试打开切换
     * 参数：name：输入，去首尾空白后须1—120个 UTF-16 码元，用户名称保持原文。
     * 返回：无。
     * 失败：busy 时忽略；名称无效、建目录、初始化或进程启动失败显示中文错误，保留原进程。
     * 副作用：在应用数据目录创建唯一目录/空库；成功登记最近记录并尝试启动新进程。
     * 线程与生命周期：GUI 校验建目录，线程池初始化，GUI 启动独立进程；销毁后初始化可完成但不切换。
     */
    Q_INVOKABLE void createWorkspace(QString name);

    /*
     * 功能：初始化并打开用户选择的工作区
     * 参数：source：输入，本机数据库文件 URL；不接受远程地址。
     * 返回：无。
     * 失败：非本机显示错误，busy 时忽略；初始化或启动失败保留当前进程。
     * 副作用：服务可创建/迁移数据库，登记最近记录，启动成功才退出。
     * 线程与生命周期：GUI 发起，线程池独立服务 initialize，GUI 切进程。
     */
    Q_INVOKABLE void openWorkspace(const QUrl& source);

    /*
     * 功能：打开最近列表中的工作区
     * 参数：index：输入，recentWorkspaces 的零基有效下标。
     * 返回：无。
     * 失败：越界或 busy 忽略；初始化或启动失败显示中文错误。
     * 副作用：按用户名称登记历史并尝试进程切换。
     * 线程与生命周期：GUI 复制路径和名称，线程池初始化，GUI 启动进程。
     */
    Q_INVOKABLE void switchToRecent(int index);

    /*
     * 功能：移除一个历史记录而不删除库
     * 参数：index：输入，recentWorkspaces 的零基下标。
     * 返回：无。
     * 失败：越界忽略；当前未向界面传播 QSettings 同步错误。
     * 副作用：写设置、发 changed，不删除文件。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void forgetRecent(int index);

    /*
     * 功能：后台读取当前工作区实际世界目录
     * 参数：无。
     * 返回：无。
     * 失败：服务失败或异常写中文错误；不把失败当空成功。
     * 副作用：递增目录请求代次，GUI 更新 worlds，尚未选择时选首项，发 changed。
     * 线程与生命周期：GUI 捕获路径与代次，线程池 WorldCatalogService 独立连接；仅最新代次回填，销毁不通知。
     */
    Q_INVOKABLE void refreshWorlds();

    /*
     * 功能：创建空世界，可接着导入并关联本地小说
     * 参数：name：输入，去空白后1—120个 UTF-16 码元；novel_file：输入，空表示不导入，否则须本机文件 URL。
     * 返回：无。
     * 失败：busy 时忽略；名称/URL无效显示错误；仅世界创建成功才选新 ID，导入/关联失败可保留已建空世界和资产。
     * 副作用：后台应用服务写世界及可选小说来源/资产；GUI 更新结果并刷新目录，全成功才发 worldCreated。
     * 线程与生命周期：GUI 捕获值，线程池独立服务操作；QPointer 失效不回填，已写世界/资产不自动回滚。
     */
    Q_INVOKABLE void createWorld(QString name, const QUrl& novel_file);

    /*
     * 功能：按世界目录下标选择世界
     * 参数：index：输入，当前 worlds 零基有效下标。
     * 返回：无。
     * 失败：越界忽略。
     * 副作用：更新 activeWorldId，发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void selectWorld(int index);

    /*
     * 功能：保存一个已开放的主题
     * 参数：theme_id：输入，仅 dark 或 light。
     * 返回：无。
     * 失败：非法或同值忽略；QSettings 同步错误当前不向界面报告。
     * 副作用：写应用设置并发 changed，不改世界库。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void setThemeId(QString theme_id);

    /*
     * 功能：保存已开放语言并通知翻译刷新
     * 参数：language_id：输入，当前仅 zh-CN。
     * 返回：无。
     * 失败：非法或同值忽略；设置同步失败当前未向界面报告。
     * 副作用：持久化语言，发 languageChanged 与 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void setLanguageId(QString language_id);

signals:

    /*
     * 功能：通知项目目录及可变属性需要重读
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收方结果。
     * 副作用：调用 Qt 已连接槽。
     * 线程与生命周期：GUI 发出。
     */
    void changed();

    /*
     * 功能：通知世界创建及可选导入均成功
     * 参数：无。
     * 返回：无。
     * 失败：创建或导入/关联失败不发出。
     * 副作用：接收方可刷新来源或跳转；不触发模型。
     * 线程与生命周期：GUI 发出，createdSourceId/章节数已更新。
     */
    void worldCreated();

    /*
     * 功能：通知语言设置发生有效改变
     * 参数：无。
     * 返回：无。
     * 失败：非法或同值不发送，不保证接收者成功加载翻译。
     * 副作用：接收方可加载翻译及重译页面。
     * 线程与生命周期：GUI 发出。
     */
    void languageChanged();

private:

    /*
     * 功能：从设置载入有效最近记录并迁移旧自动标签
     * 参数：无。
     * 返回：无。
     * 失败：无效或已消失路径跳过；设置错误当前未向界面传播。
     * 副作用：填充 recent_，不读正文、不删除路径，不修改用户自定义名称。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void loadRecent();

    /*
     * 功能：按路径去重并登记最近打开时间
     * 参数：name：输入，可空用户名称，空时保留已有名称或中文默认名；path：输入，同步借用本机数据库路径。
     * 返回：无。
     * 失败：设置同步错误当前未向界面传播。
     * 副作用：保留最多20项并写 UTC 时间，保存设置、发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void registerRecent(QString name, const QString& path);

    /*
     * 功能：将当前最近列表完整写入应用设置
     * 参数：无。
     * 返回：无。
     * 失败：QSettings 同步错误当前未向界面传播。
     * 副作用：替换最近记录数组并同步，不改数据库。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void saveRecent();

    /*
     * 功能：通过应用服务初始化数据库后尝试进程切换
     * 参数：name：输入，可空工作区显示名称；path：输入，按值持有的本机数据库路径。
     * 返回：无。
     * 失败：busy 时忽略；初始化异常/失败或新进程启动失败显示中文错误，原进程继续运行。
     * 副作用：可创建/迁移数据库；初始化成功后写最近记录，再尝试重启。
     * 线程与生命周期：线程池持值 initialize，GUI 回填和启动；对象销毁时不通知/不重启，磁盘操作可能已完成。
     */
    void initializeAndSwitch(QString name, std::filesystem::path path);

    /*
     * 功能：启动同一程序打开指定数据库，成功才退出原进程
     * 参数：path：输入，非空本机数据库路径，同步借用，作为独立 --workspace 参数传递。
     * 返回：系统接受启动时为true并请求原程序退出；启动失败为false。
     * 失败：不保证新进程随后打开数据库成功；启动失败不退出原进程。
     * 副作用：成功启动独立进程并请求退出事件循环，调用方显示启动失败提示。
     * 线程与生命周期：仅 GUI 同步调用，不保存路径引用，不等待新进程初始化。
     */
    static bool restartAt(const QString& path);

    /* 当前库路径，构造确定，实例持有；后台复制，进程内不切库。 */
    std::filesystem::path database_path_;

    /* 最近记录，初始空；loadRecent/登记/遗忘由 GUI 更新，新登记最多20项。 */
    QVariantList recent_;

    /* 世界创建或工作区打开在途标志，初始false；GUI 置位和完成回调复位。 */
    bool busy_{false};

    /* 项目操作中文状态，初始说明工作区已打开；GUI 更新。 */
    QString status_text_{tr("当前工作区已打开")};

    /* 项目操作安全中文错误，初始空；GUI 清理和回填写入。 */
    QString error_text_;

    /* 实际世界轻量目录，初始空；最新目录代次的 GUI 回调替换。 */
    QVariantList worlds_;

    /* 世界目录请求代次，初始0；GUI 刷新递增，旧代次结果不能回填。 */
    std::uint64_t world_listing_generation_{0};

    /* 最近创建操作成功关联来源 ID，初始空；新操作清空，GUI 成功回填写入。 */
    QString created_source_id_;

    /* 最近成功关联小说章节个数，初始0；新操作归零，GUI 完成更新。 */
    int created_chapter_count_{0};

    /* 选中世界稳定 ID，初始空；GUI 选择、首目录或创建成功时设置。 */
    QString active_world_id_;

    /* 主题协议值，默认 dark；恢复/GUI 设置时仅接受 dark/light。 */
    QString theme_id_{QStringLiteral("dark")};

    /* 语言协议值，默认 zh-CN；当前只开放简体中文，预留接口。 */
    QString language_id_{QStringLiteral("zh-CN")};
};
