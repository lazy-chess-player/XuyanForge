#include <QtQuickTest>

#include <QCoreApplication>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariantList>

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
 * 职责：配置 Qt Quick 测试身份并拥有空目录替身，避免接触生产工作区。
 * 生命周期与线程：由测试入口在主线程管理；注入引擎的目录指针只在本对象存活期间有效。
 */
class TestSetup final : public QObject {
    Q_OBJECT

public slots:
    /*
     * 功能：在测试应用可用后启用 Qt 测试路径，并设置专用应用身份。
     * 参数：无。返回：无。失败：Qt 设置调用的异常不在此捕获。
     * 副作用：修改本进程的标准路径模式、组织及应用标识，不创建工作区。
     * 线程与生命周期：测试主线程初始化时调用，设置影响该测试进程后续生命周期。
     */
    void applicationAvailable() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("XuyanForgeTests");
        QCoreApplication::setOrganizationDomain("tests.local");
        QCoreApplication::setApplicationName("qmltests");
    }

    /*
     * 功能：在测试 QML 加载前注入内存空目录上下文属性。
     * 参数：engine：输入，框架提供的非空引擎观察指针；不接管所有权，引擎须有根上下文。
     * 返回：无。失败：不校验空指针；调用方必须满足框架前置条件。
     * 副作用：设置 workspaceCatalog 上下文属性，不访问文件或网络。
     * 线程与生命周期：主线程执行；empty_catalog_ 必须覆盖 QML 使用期间，随 TestSetup 销毁。
     */
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("workspaceCatalog", &empty_catalog_);
    }

private:
    /* 拥有的空目录 QObject，默认空列表和深色主题；主线程读写，寿命覆盖测试引擎访问。 */
    EmptyWorkspaceCatalog empty_catalog_;
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
