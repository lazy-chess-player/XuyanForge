#include <QtQuickTest>

#include <QCoreApplication>
#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QString>
#include <QUrl>
#include <QVariantList>

/** @brief 为首页测试提供完全内存化的空工作区状态，不连接数据库或网络。 */
class EmptyWorkspaceCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy MEMBER busy_ CONSTANT)
    Q_PROPERTY(QString statusText MEMBER status_text_ CONSTANT)
    Q_PROPERTY(QString errorText MEMBER error_text_ CONSTANT)
    Q_PROPERTY(int createCount READ createCount NOTIFY createCountChanged)
    Q_PROPERTY(QVariantList worlds MEMBER worlds_ CONSTANT)
    Q_PROPERTY(QVariantList recentWorkspaces MEMBER recent_workspaces_ CONSTANT)
    Q_PROPERTY(QString activeWorldId MEMBER active_world_id_ CONSTANT)
    Q_PROPERTY(QString themeId READ themeId NOTIFY themeIdChanged)

public:
    /** @brief 返回测试期间创建操作被调用的次数，用于确认禁用按钮没有副作用。 */
    [[nodiscard]] int createCount() const { return create_count_; }

    /** @brief 记录首页创建请求；替身不写磁盘，也不访问模型服务。 */
    Q_INVOKABLE void createWorld(const QString &, const QUrl &) {
        ++create_count_;
        emit createCountChanged();
    }

    /** @brief 返回当前内存主题标识，模拟正式工作区的外观设置。 */
    [[nodiscard]] QString themeId() const { return theme_id_; }

    /** @brief 仅在内存中切换深浅主题，不读写用户设置文件。 */
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
    /** @brief 通知测试断言创建调用次数发生变化。 */
    void createCountChanged();

    /** @brief 通知 QML 刷新依赖主题标识的颜色和按钮文案。 */
    void themeIdChanged();

private:
    bool busy_ = false;
    QString status_text_;
    QString error_text_;
    int create_count_ = 0;
    QVariantList worlds_;
    QVariantList recent_workspaces_;
    QString active_world_id_;
    QString theme_id_ = QStringLiteral("dark");
};

/** @brief 在测试文件加载前配置隔离的应用身份和首页上下文。 */
class TestSetup final : public QObject {
    Q_OBJECT

public slots:
    /** @brief 将标准数据路径切换到测试模式，避免触及正式应用的数据目录。 */
    void applicationAvailable() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("XuyanForgeTests");
        QCoreApplication::setOrganizationDomain("tests.local");
        QCoreApplication::setApplicationName("qmltests");
    }

    /** @brief 向 QML 注入无数据目录、无远程调用能力的工作区目录替身。 */
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("workspaceCatalog", &empty_catalog_);
    }

private:
    EmptyWorkspaceCatalog empty_catalog_;
};

QUICK_TEST_MAIN_WITH_SETUP(qmltests, TestSetup)

#include "main.moc"
