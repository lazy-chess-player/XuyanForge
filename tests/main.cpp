#include <QtQuickTest>
#include "framework_chinese_translator.h"
#include "framework_option_probe.h"

#include <QCoreApplication>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <array>

/*
 * 职责：在 Qt Quick 首页及侧栏回归中提供空目录和内存主题，拥有属性值而不拥有数据库。
 * 生命周期与线程：随 TestSetup 在测试主线程创建、销毁；QML 只在该对象存活期间借用。
 */
class EmptyWorkspaceCatalog final : public QObject {
    Q_OBJECT
    /* 只读忙碌状态，始终 false，表示替身没有后台任务；值随对象存活。 */
    Q_PROPERTY(bool busy MEMBER busy_ CONSTANT)
    /* 只读状态文案，默认空，供空首页渲染，不由替身产生假进度。 */
    Q_PROPERTY(QString statusText MEMBER status_text_ CONSTANT)
    /* 只读错误文案，默认空；替身没有文件或网络失败来源。 */
    Q_PROPERTY(QString errorText MEMBER error_text_ CONSTANT)
    /* 创建请求累计次数，初始 0、单位次；createWorld 修改，QML 断言读取。 */
    Q_PROPERTY(int createCount READ createCount NOTIFY createCountChanged)
    /* 只读世界列表，默认且始终为空；不创建任何生产或测试世界。 */
    Q_PROPERTY(QVariantList worlds MEMBER worlds_ CONSTANT)
    /* 只读最近目录列表，始终为空；不读取真实用户的最近工作区。 */
    Q_PROPERTY(QVariantList recentWorkspaces MEMBER recent_workspaces_ CONSTANT)
    /* 当前世界标识，始终为空，表达无选择而不是回退第一个世界。 */
    Q_PROPERTY(QString activeWorldId MEMBER active_world_id_ CONSTANT)
    /* 空工作区路径，仅满足工作区管理页读取；不对应真实用户文件。 */
    Q_PROPERTY(QString currentPath MEMBER current_path_ CONSTANT)
    /* 内部主题协议 dark/light，默认 dark；中文显示由 QML 映射，变更发通知。 */
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeIdChanged)

public:
    /*
     * 功能：读取替身累计收到的创建请求数，供禁用按钮的无副作用断言使用。
     * 参数：无。返回：从 0 起累计的调用次数，单位次。
     * 失败：无显式失败路径。副作用：只读内存，不发信号。
     * 线程与生命周期：测试主线程同步调用，结果为独立整数值。
     */
    [[nodiscard]] int createCount() const { return create_count_; }

    /*
     * 功能：记录首页发出的创建请求，不构造世界资料。
     * 参数：第一个未命名 QString 引用为输入名称；第二个未命名 QUrl 引用为输入来源网址；
     *   均只在调用期间借用并忽略，可为空。
     * 返回：无。失败：不校验输入；信号槽异常按 Qt 调用语义传播。
     * 副作用：计数加一并发出 createCountChanged；不读写磁盘或访问模型。
     * 线程与生命周期：QML 在测试主线程同步调用，对象须存活到信号处理结束。
     */
    Q_INVOKABLE void createWorld(const QString &, const QUrl &) {
        ++create_count_;
        emit createCountChanged();
    }

    /*
     * 功能：读取当前内存主题的内部协议值。
     * 参数：无。返回：dark 或 light 的 QString 值，初始为 dark。
     * 失败：字符串复制可能传播分配异常。副作用：只读，不访问用户设置。
     * 线程与生命周期：测试主线程同步读取，返回值自行持有字符串。
     */
    [[nodiscard]] QString themeId() const { return theme_id_; }

    /*
     * 功能：切换内存主题，并仅在有效值实际变化时通知 QML。
     * 参数：theme_id：输入，只在调用期间借用的内部协议值，仅接受 dark/light，空值忽略。
     * 返回：无。失败：无效值静默忽略；字符串赋值和信号调用异常不在此捕获。
     * 副作用：更新 theme_id_ 并发出 themeIdChanged，不读写设置文件。
     * 线程与生命周期：在测试主线程同步执行，主题值由对象持有至下一次更新或销毁。
     */
    Q_INVOKABLE void setThemeId(const QString &theme_id) {
        if (theme_id != QStringLiteral("dark") && theme_id != QStringLiteral("light")) {
            return;
        }
        if (theme_id_ == theme_id) {
            return;
        }
        theme_id_ = theme_id;
        emit themeIdChanged();
    }

signals:
    /*
     * 功能：通知 QML 重新读取创建次数。参数：无。返回：无。
     * 失败：槽的执行服从 Qt 连接语义，本信号不返回错误。
     * 副作用：createWorld 递增后发出；测试主线程触发，不携带借用资源。
     */
    void createCountChanged();

    /*
     * 功能：通知 QML 重读主题并刷新颜色与文案。参数：无。返回：无。
     * 失败：槽的执行服从 Qt 连接语义，本信号不返回错误。
     * 副作用：仅在主题有效且变化后发出；测试主线程触发，接收者读取对象持有的值。
     */
    void themeIdChanged();

private:
    /* 固定空路径，无文件含义，管理页只读，随替身销毁。 */
    QString current_path_;
    /* 无异步操作的忙碌标记，默认 false，替身不修改，属性读取，生命周期随目录。 */
    bool busy_ = false;
    /* 中文状态文本存储，默认空，固定属性读取，不读取生产状态。 */
    QString status_text_;
    /* 错误文本存储，默认空且不变，固定属性读取，生命周期随目录。 */
    QString error_text_;
    /* 本对象累计创建调用数，单位次，默认 0；createWorld 写入，createCount 读取。 */
    int create_count_ = 0;
    /* 世界值列表，默认空且不变，由 QML 只读；不拥有数据库对象。 */
    QVariantList worlds_;
    /* 最近工作区值列表，默认空且不变，由 QML 只读；不访问真实目录。 */
    QVariantList recent_workspaces_;
    /* 无选择的世界稳定标识，默认空且不变，供 QML 空态判断。 */
    QString active_world_id_;
    /* 内部主题标识，默认 dark，setThemeId 修改，themeId 读取；不持久化。 */
    QString theme_id_ = QStringLiteral("dark");
};

/*
 * 职责：为设置页与资料页提供空内存字段，不组装应用服务、小说或连接。
 * 生命周期与线程：TestSetup在GUI线程拥有；动态属性仅用于呈现空表单，测试不保存或发起网络操作。
 */
class EmptyFormModel final : public QQmlPropertyMap {
    Q_OBJECT
public:
    /*
     * 功能：初始化资料、连接、小说与包页面读取的空字段，使真实中文选项可独立测试。
     * 参数：无。返回：完成初始化；所有列表为空、选择无效、文本为空。
     * 失败：容器或字符串分配异常传播；不把任何用户数据加载为替身。
     * 副作用：只注册内存属性；不访问文件、凭据或模型，寿命由TestSetup覆盖测试引擎。
     */
    EmptyFormModel() : QQmlPropertyMap(this, nullptr) {
        /* 世界资料当前页；默认空，无预置条目，页面列表只读。 */
        insert("entityItems", QVariantList{});
        /* 模型连接当前列表；默认空，无默认端点或模型，设置页只读。 */
        insert("connections", QVariantList{});
        /* 当前页选择索引；-1表示未选择，页面据此禁用已有条目操作。 */
        insert("selectedIndex", -1);
        /* 未选条目的空稳定标识，表单按新建语义只读。 */
        insert("selectedId", QString{});
        /* 名称草稿，默认空，由资料表单读取，不代表实际条目。 */
        insert("selectedName", QString{});
        /* 说明草稿，默认空，只读呈现，不含样例正文。 */
        insert("selectedDescription", QString{});
        /* 别名草稿，默认空，由资料表单读取，无持久化。 */
        insert("selectedAliases", QString{});
        /* 标签草稿，默认空，由资料表单读取，无业务预置。 */
        insert("selectedTags", QString{});
        /* 属性草稿，默认空对象的序列化值，只满足表单语法读取。 */
        insert("selectedAttributes", QStringLiteral("{}"));
        /* 未选条目的类型协议，默认other；仅在类型显示回归中替换并发changed。 */
        insert("selectedKind", QStringLiteral("other"));
        /* 未选择时修订为0，无单位，只满足表单空态读取。 */
        insert("selectedRevision", 0);
        /* 上一页可用性，默认false；空列表始终不支持翻页。 */
        insert("hasPreviousPage", false);
        /* 下一页可用性，默认false；空列表始终不支持翻页。 */
        insert("hasNextPage", false);
        /* 模拟忙碌状态，默认false，不拥有后台线程。 */
        insert("busy", false);
        /* 页码显示，默认0 / 0，由空列表界面读取，不虚构页数。 */
        insert("pageText", QStringLiteral("0 / 0"));
        /* 状态文本，默认空，不伪造完成或进度。 */
        insert("statusText", QString{});
        /* 错误文本，默认空，替身不接触真实文件及模型。 */
        insert("errorText", QString{});
        /* 来源列表，默认空；只供小说页呈现空态，不导入文本。 */
        insert("sourceItems", QVariantList{});
        /* 章节列表，默认空；小说页只读，不组装真实或测试章节。 */
        insert("chapterItems", QVariantList{});
        /* 当前来源标识，默认空，表示没有选择来源。 */
        insert("selectedSourceId", QString{});
        /* 当前章节零基索引，默认-1表示无章节，测试只读。 */
        insert("selectedChapterIndex", -1);
        /* 预览文本，默认空，不读取任何小说原文。 */
        insert("previewText", QString{});
        /* 覆盖确认替身的目标存在标记，默认false；用例明确设置，不查询真实文件。 */
        insert("destinationPresent", false);
        /* 导出动作计数，初始0次；测试核对返回与确认边界，不表示真实导出。 */
        insert("exportCount", 0);
        /* 最后导出目标，默认空URL；按值持有，仅供内存断言，不写文件。 */
        insert("lastExportFile", QUrl{});
    }
    /* 功能：模拟只读目标存在性，供中文覆盖弹窗用例控制分支。
     * 参数：destination为输入URL，本替身只保持签名而忽略地址，不接触文件。
     * 返回：用例设置的destinationPresent值，默认false；失败：不校验路径或产生文件错误。
     * 副作用：只读内存，GUI线程调用，对象寿命覆盖用例。 */
    Q_INVOKABLE bool destinationExists(const QUrl& destination) const {
        static_cast<void>(destination);
        return value("destinationPresent").toBool();
    }
    /* 功能：记录界面是否明确请求导出，防止取消弹窗仍消费目标。
     * 参数：destination为借用的目标URL，按值复制到内存，不写出任何路径。
     * 返回：无。失败：属性分配异常交框架；副作用：只递增exportCount并更新lastExportFile。 */
    Q_INVOKABLE void exportWorld(const QUrl& destination) {
        insert("exportCount", value("exportCount").toInt() + 1);
        insert("lastExportFile", destination);
    }
signals:
    /* 功能：通知资料表单重读内存选择类型。参数：无。返回：无。
     * 失败：槽按Qt语义执行。副作用：GUI线程更新显示，不读写数据库或发送请求。 */
    void changed();
};

/*
 * 职责：配置测试身份、中文框架翻译和空内存模型，使页面回归与生产资料隔离。
 * 生命周期与线程：入口在GUI线程拥有；模型和翻译器的寿命覆盖测试引擎访问。
 */
class TestSetup final : public QObject {
    Q_OBJECT

public slots:
    /*
     * 功能：启用测试路径和专用身份，加载与正式程序一致的中文框架翻译链。
     * 参数：无。返回：无。失败：Qt 设置调用的异常不在此捕获。
     * 副作用：修改进程路径模式、身份及默认区域，读取SDK词库并安装翻译器；不创建工作区。
     *   可选Qt词库加载失败时不安装该词库；本地补充表仍安装，中文词条由回归验证。
     * 线程与生命周期：测试主线程初始化时调用，设置影响该测试进程后续生命周期。
     * 补充失败条件：TestSetup拥有的独占空目录创建失败时立即以中文原因终止测试，不回退用户目录。
     * 生命周期：临时目录随TestSetup构造取得，退出时清理自身目录；本方法只核对创建结果。
     */
    void applicationAvailable() {
        // 每次测试进程独占空目录，文件框不展示用户目录或历史私有素材。
        if (!option_directory_.isValid()) qFatal("无法创建选项测试的独占临时目录");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("XuyanForgeTests");
        QCoreApplication::setOrganizationDomain("tests.local");
        QCoreApplication::setApplicationName("qmltests");
        QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
        // 与正式启动相同的Qt中文词库；覆盖文件框与框架内置动作，不能只验证产品源文。
        const QStringList names{QStringLiteral("qt_zh_CN"), QStringLiteral("qtbase_zh_CN"),
                                QStringLiteral("qtdeclarative_zh_CN")};
        for (int index = 0; index < names.size(); ++index) {
            auto& translator = framework_translators_[static_cast<std::size_t>(index)];
            if (translator.load(names.at(index), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
                QCoreApplication::installTranslator(&translator);
        }
        QCoreApplication::installTranslator(&framework_chinese_supplement_);
    }

    /*
     * 功能：在QML加载前注入空目录、空表单和框架版本，隔离文件、凭据及模型操作。
     * 参数：engine：输入，框架提供的非空引擎观察指针；不接管所有权，引擎须有根上下文。
     * 返回：无。失败：不校验空指针；调用方必须满足框架前置条件。
     * 副作用：设置各页面模型及框架版本上下文属性，不访问用户文件或网络。
     * 线程与生命周期：主线程执行；两类替身必须覆盖QML使用期间，随TestSetup销毁。
     * 测试专用上下文：同时注入只读框架探针及自有空目录URL；不创建、打开或发送小说。
     * 生命周期：探针不缓存控件指针，临时目录和所有替身覆盖引擎使用期间，由TestSetup拥有。
     */
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("workspaceCatalog", &empty_catalog_);
        engine->rootContext()->setContextProperty("workspace", &empty_form_);
        engine->rootContext()->setContextProperty("providers", &empty_form_);
        engine->rootContext()->setContextProperty("packages", &empty_form_);
        engine->rootContext()->setContextProperty("sources", &empty_form_);
        engine->rootContext()->setContextProperty("qtVersion", QString::fromLatin1(qVersion()));
        engine->rootContext()->setContextProperty("frameworkOptions", &framework_options_);
        engine->rootContext()->setContextProperty("optionTestFolder", QUrl::fromLocalFile(option_directory_.path()));
    }

private:
    /* 本进程拥有的空临时目录，构造时独占创建，文件框只读；退出时仅清理自身目录。 */
    QTemporaryDir option_directory_;
    /* 测试专用框架选项探针，无缓存和业务资源；GUI线程只读控件，寿命覆盖测试引擎。 */
    FrameworkOptionProbe framework_options_;
    /* 拥有的空目录 QObject，默认空列表和深色主题；主线程读写，寿命覆盖测试引擎访问。 */
    EmptyWorkspaceCatalog empty_catalog_;
    /* 四类页面共享的空表单替身，GUI线程读取，TestSetup拥有且比引擎存活更久；仅有内存计数能力。 */
    EmptyFormModel empty_form_;
    /* Qt框架中文翻译器，启动时加载，寿命覆盖全部测试引擎和弹窗。 */
    std::array<QTranslator, 3> framework_translators_;
    /* 正式程序同一中文补充表，初始化后覆盖缺失Quick词条；寿命覆盖测试引擎。 */
    FrameworkChineseTranslator framework_chinese_supplement_;
};

/*
 * 功能：由 Qt QuickTest 生成入口，建立应用、调用 TestSetup 并执行 QML 测试文件。
 * 参数：宏生成的 argc/argv 接收测试运行参数，由 Qt 解析；宏参数为测试名和初始化类型。
 * 返回：框架按测试结果返回进程状态。失败：测试断言和初始化失败由框架报告。
 * 副作用：创建测试应用、引擎及事件循环，使用测试路径；无生产数据库或模型组装。
 * 线程与生命周期：入口主线程管理初始化对象和引擎，清理顺序由 Qt QuickTest 负责。
 */
QUICK_TEST_MAIN_WITH_SETUP(qmltests, TestSetup)

#include "main.moc"
