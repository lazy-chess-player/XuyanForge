#include "workspace_view_model.h"
#include "workspace_catalog_view_model.h"
#include "source_view_model.h"
#include "character_view_model.h"
#include "package_view_model.h"
#include "provider_view_model.h"
#include "extraction_job_view_model.h"
#include "candidate_review_view_model.h"
#include "world_views_view_model.h"
#include "framework_chinese_translator.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QTemporaryDir>
#include <QDebug>
#include <array>
#include <memory>

namespace {
/*
 * 功能：将目录的世界选择绑定到一个页面模型，并立即同步当前选择。
 * 参数：catalog为GUI线程中的目录模型；model为同线程页面模型，均借用到程序关闭。
 * 返回：无。失败：Qt连接或字符串分配的错误沿启动路径传播，不创建替代世界。
 * 副作用：调用model.setWorldId；目录变化后由接收者上下文触发同步，接收者销毁即断开。
 */
template <typename Model>
void bindWorldSelection(WorkspaceCatalogViewModel& catalog, Model& model) {
    /* 功能：接收目录变化，读取最新稳定世界标识而非捕获旧选择。
     * 参数：无。返回：无。副作用：在GUI线程刷新关联页面；两对象由启动栈拥有。
     * 失败：页面模型按自己的刷新契约处理读取失败；此回调不发送模型请求。
     */
    QObject::connect(&catalog, &WorkspaceCatalogViewModel::changed, &model,
                     [&catalog, &model] { model.setWorldId(catalog.activeWorldId()); });
    model.setWorldId(catalog.activeWorldId());
}

/* 功能：把资料变化信号绑定到页面模型的默认刷新调用，保留其默认筛选参数语义。
 * 参数：sender为借用的信号对象；signal为其Qt成员信号；receiver为GUI线程页面模型，均由启动栈拥有。
 * 返回：无。失败：刷新按页面模型契约报告错误，不把C++默认参数当成Qt信号实参。
 * 副作用：信号触发后调用receiver.refresh()；接收者销毁自动断开，不发送模型请求。
 */
template <typename Sender, typename Signal, typename Receiver>
void bindRefresh(Sender& sender, Signal signal, Receiver& receiver) {
    /* 功能：使用C++调用语义取得刷新默认参数，不要求信号提供这些参数。
     * 参数：无。返回：无。失败：由页面模型处理刷新错误。
     * 副作用：GUI线程刷新列表；借用receiver，连接上下文销毁即失效。
     */
    QObject::connect(&sender, signal, &receiver, [&receiver] { receiver.refresh(); });
}

/*
 * 功能：把显式命令行页面参数映射为已有工作区页面，不改变正常首启页面。
 * 参数：arguments为启动参数的只读副本；engine为已加载的界面引擎，调用期间借用。
 * 返回：无。失败：根对象为空时不操作；多个页面参数按下面固定表的末项优先。
 * 副作用：仅修改根对象activePage属性，不创建世界、不读写数据库或访问网络。
 */
void selectRequestedPage(const QStringList& arguments, QQmlApplicationEngine& engine) {
    if (engine.rootObjects().isEmpty()) return;
    // 参数表与现有Main页面编号保持一致；标识属于内部接口，不作为界面选项显示。
    const std::array<const char*, 9> pageArguments{
        "--workspace-page", "--sources-page", "--characters-page", "--packages-page",
        "--providers-page", "--workspaces-page", "--tasks-page", "--review-page", "--views-page"};
    for (std::size_t index = 0; index < pageArguments.size(); ++index) {
        if (arguments.contains(QString::fromLatin1(pageArguments[index])))
            engine.rootObjects().first()->setProperty("activePage", static_cast<int>(index + 1));
    }
}
} // namespace

/*
 * 功能：启动中文桌面程序，组装本地工作区服务及界面，或生成隔离的界面截图。
 * 参数：argc 为进程参数数量；argv 为有效期覆盖整个调用的参数数组，由运行时拥有。
 * 返回：正常关闭为 0；界面加载失败为 -1；截图或临时目录创建失败为 2。
 * 副作用：初始化指定数据库、读取设置并运行 GUI 事件循环；启动本身不发送模型请求。
 * 生命周期：视图模型、翻译器及引擎均由栈管理；无显式工作区的截图使用独占临时目录。
 */
int main(int argc, char* argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    // 程序级GUI对象最后销毁，为所有视图模型和线程清理保留Qt运行环境。
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XuyanForge"));
    QCoreApplication::setApplicationName(QStringLiteral("叙演工坊"));
    QCoreApplication::setApplicationVersion(QStringLiteral(XUYANFORGE_VERSION));

    const auto arguments = application.arguments();
    const auto screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    const auto workspaceArgument = arguments.indexOf(QStringLiteral("--workspace"));
    if ((screenshotIndex >= 0 && screenshotIndex + 1 >= arguments.size())
        || (workspaceArgument >= 0 && workspaceArgument + 1 >= arguments.size())) {
        qCritical().noquote() << QStringLiteral("启动参数缺少截图路径或工作区路径");
        return 2;
    }
    if (screenshotIndex >= 0)
        QCoreApplication::setOrganizationName(QStringLiteral("XuyanForgePreview"));
    const auto dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataRoot);
    // 截图不复用真实用户数据库；临时目录最后析构，晚于视图模型和界面。
    const auto previewDirectory = screenshotIndex >= 0
        ? std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/xuyan-preview-XXXXXX")) : nullptr;
    if (previewDirectory && !previewDirectory->isValid()) return 2;

    // Qt 控件的内置动作也必须中文；部署目录优先，SDK 目录仅供本机构建运行。
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    // 翻译器由启动栈拥有，生命周期覆盖事件循环；每项对应一个Qt框架中文词库。
    std::array<QTranslator, 3> frameworkTranslators;
    const QStringList translationNames{QStringLiteral("qt_zh_CN"), QStringLiteral("qtbase_zh_CN"),
                                       QStringLiteral("qtdeclarative_zh_CN")};
    for (int index = 0; index < translationNames.size(); ++index) {
        auto& translator = frameworkTranslators[static_cast<std::size_t>(index)];
        if (translator.load(translationNames.at(index), application.applicationDirPath() + QStringLiteral("/translations"))
            || translator.load(translationNames.at(index), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            application.installTranslator(&translator);
    }
    // Qt词库不保证包含Quick新增动作；精确补齐已核对的文件框和右键菜单上下文。
    FrameworkChineseTranslator frameworkChineseSupplement;
    application.installTranslator(&frameworkChineseSupplement);
    const auto databaseText = workspaceArgument >= 0 && workspaceArgument + 1 < arguments.size()
        ? QFileInfo(arguments.at(workspaceArgument + 1)).absoluteFilePath()
        : (previewDirectory ? previewDirectory->path() : dataRoot) + QStringLiteral("/workspace.sqlite");
    const auto databasePath = databaseText.toStdWString();
    // 页面模型持有路径及各自任务状态；操作在执行线程打开连接，不共享活动连接句柄。
    WorkspaceCatalogViewModel workspaceCatalog(databasePath);
    const auto themePreviewIndex = arguments.indexOf(QStringLiteral("--theme-preview"));
    if (screenshotIndex >= 0 && themePreviewIndex >= 0 && themePreviewIndex + 1 < arguments.size())
        workspaceCatalog.setThemeId(arguments.at(themePreviewIndex + 1));
    WorkspaceViewModel workspace(databasePath);
    SourceViewModel sources(databasePath);
    CharacterViewModel characters(databasePath);
    PackageViewModel packages(databasePath);
    ProviderViewModel providers(databasePath);
    ExtractionJobViewModel extractionJobs(databasePath);
    CandidateReviewViewModel candidateReview(databasePath);
    WorldViewsViewModel worldViews(databasePath);
    // 五个页面共享明确世界边界；通用绑定避免新增页面时遗漏首次同步或切换同步。
    bindWorldSelection(workspaceCatalog, workspace);
    bindWorldSelection(workspaceCatalog, worldViews);
    bindWorldSelection(workspaceCatalog, sources);
    bindWorldSelection(workspaceCatalog, extractionJobs);
    bindWorldSelection(workspaceCatalog, candidateReview);

    // refresh声明可能有默认参数，通过统一零参数调用绑定，不改变原筛选语义。
    bindRefresh(packages, &PackageViewModel::worldImported, workspace);
    bindRefresh(packages, &PackageViewModel::characterImported, characters);
    /* 功能：备份恢复成功后请求打开其实际文件，不回退为当前或默认工作区。
     * 参数：path为信号提供的本地恢复路径，调用期间借用。返回：无。
     * 失败：打开错误交由目录模型显示中文提示；副作用：成功时重启到该工作区。
     * 生命周期：GUI线程接收，目录模型为连接上下文，销毁后回调失效。
     */
    QObject::connect(&packages, &PackageViewModel::backupRestored, &workspaceCatalog,
                     [&workspaceCatalog](const QString& path) { workspaceCatalog.openWorkspace(QUrl::fromLocalFile(path)); });
    bindRefresh(candidateReview, &CandidateReviewViewModel::candidateAccepted, workspace);
    bindRefresh(workspaceCatalog, &WorkspaceCatalogViewModel::worldCreated, sources);
    QObject::connect(&sources, &SourceViewModel::sourceImported,
                     &workspaceCatalog, &WorkspaceCatalogViewModel::refreshWorlds);

    // 产品翻译器先创建后析构，覆盖引用它的界面引擎及回调整个有效期。
    QTranslator applicationTranslator;
    QQmlApplicationEngine engine;
    /* 功能：显式语言设置变化后重新安装产品翻译并刷新QML绑定。
     * 参数：无。返回：无。失败：缺少翻译文件时保持中文源文案，不自动开放其他语言。
     * 副作用：只替换本应用翻译器，不改变模型协议和用户内容；GUI线程执行。
     * 生命周期：捕获对象均为启动栈借用；引擎为连接上下文，销毁即断开。
     */
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::languageChanged,
                     &engine, [&application, &engine, &applicationTranslator, &workspaceCatalog] {
        application.removeTranslator(&applicationTranslator);
        if (workspaceCatalog.languageId() != QStringLiteral("zh-CN")) {
            const auto directory = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
            if (applicationTranslator.load(QStringLiteral("xuyanforge_") + workspaceCatalog.languageId(), directory))
                application.installTranslator(&applicationTranslator);
        }
        engine.retranslate();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("workspaceCatalog"), &workspaceCatalog);
    engine.rootContext()->setContextProperty(QStringLiteral("sources"), &sources);
    engine.rootContext()->setContextProperty(QStringLiteral("characters"), &characters);
    engine.rootContext()->setContextProperty(QStringLiteral("packages"), &packages);
    engine.rootContext()->setContextProperty(QStringLiteral("providers"), &providers);
    engine.rootContext()->setContextProperty(QStringLiteral("extractionJobs"), &extractionJobs);
    engine.rootContext()->setContextProperty(QStringLiteral("candidateReview"), &candidateReview);
    engine.rootContext()->setContextProperty(QStringLiteral("worldViews"), &worldViews);
    /* 功能：根界面创建失败时终止启动，防止无窗口进程伪装成正常运行。
     * 参数：忽略Qt信号附带的失败对象地址。返回：无。
     * 失败与副作用：排队在GUI线程请求退出码-1；application上下文销毁即断开。
     */
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("XuyanForge"), QStringLiteral("Main"));

    selectRequestedPage(arguments, engine);
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        const auto screenshotPath = arguments.at(screenshotIndex + 1);
        /* 功能：等待初始界面绘制后保存一次真实窗口截图，并终止隔离预览。
         * 参数：无；screenshotPath为值捕获的输出路径，engine为借用的活动引擎。
         * 返回：无。失败：根对象不是窗口或保存失败则退出2，不报告截图成功。
         * 副作用：写入指定图片；GUI线程执行，application销毁后不会调用。
         */
        QTimer::singleShot(1200, &application, [&engine, screenshotPath] {
            const auto roots = engine.rootObjects();
            auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
            if (window == nullptr || !window->grabWindow().save(screenshotPath)) {
                QCoreApplication::exit(2);
                return;
            }
            QCoreApplication::quit();
        });
    }
    return application.exec();
}
