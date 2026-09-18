#include "simulation_view_model.h"
#include "workspace_view_model.h"
#include "workspace_catalog_view_model.h"
#include "source_view_model.h"
#include "character_view_model.h"
#include "package_view_model.h"
#include "provider_view_model.h"
#include "extraction_job_view_model.h"
#include "candidate_review_view_model.h"
#include "world_views_view_model.h"
#include "xuyan/application/simulation_service.h"
#include "xuyan/application/demo_world_service.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char* argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XuyanForge"));
    QCoreApplication::setApplicationName(QStringLiteral("叙演工坊"));

    const auto dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataRoot);
    const auto arguments = application.arguments();
    const auto workspaceArgument = arguments.indexOf(QStringLiteral("--workspace"));
    const auto databaseText = workspaceArgument >= 0 && workspaceArgument + 1 < arguments.size()
        ? QFileInfo(arguments.at(workspaceArgument + 1)).absoluteFilePath()
        : dataRoot + QStringLiteral("/grey-harbor.sqlite");
    const auto databasePath = databaseText.toStdWString();
    if (arguments.contains(QStringLiteral("--install-demo"))) {
        xuyan::application::DemoWorldService demo(databasePath);
        auto installed = demo.install();
        if (!installed.ok() || !installed.value->ready) return 3;
    }
    if (arguments.contains(QStringLiteral("--production-demo"))) {
        xuyan::application::SimulationService seed(databasePath);
        auto root = seed.open();
        if (root.ok()) {
            auto session = seed.createSession("ui-production-demo-create", root.value->branch_id, 20, false, 120);
            if (session.ok() && session.value->turns.empty())
                seed.stepSessionMock("ui-production-demo-turn-1", session.value->id);
        }
    }
    WorkspaceCatalogViewModel workspaceCatalog(databasePath);
    SimulationViewModel simulation(databasePath);
    WorkspaceViewModel workspace(databasePath);
    SourceViewModel sources(databasePath);
    CharacterViewModel characters(databasePath);
    PackageViewModel packages(databasePath);
    ProviderViewModel providers(databasePath);
    ExtractionJobViewModel extractionJobs(databasePath);
    CandidateReviewViewModel candidateReview(databasePath);
    WorldViewsViewModel worldViews(databasePath);
    QObject::connect(&packages, &PackageViewModel::worldImported, &workspace, [&workspace] { workspace.refresh(); });
    QObject::connect(&packages, &PackageViewModel::characterImported, &characters, [&characters] { characters.refresh(); });
    QObject::connect(&packages, &PackageViewModel::backupRestored, &workspaceCatalog,
                     [&workspaceCatalog](const QString& path) { workspaceCatalog.openWorkspace(QUrl::fromLocalFile(path)); });
    QObject::connect(&candidateReview, &CandidateReviewViewModel::candidateAccepted,
                     &workspace, [&workspace] { workspace.refresh(); });
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::demoInstalled,
                     &workspace, [&workspace] { workspace.refresh(); });
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::demoInstalled,
                     &sources, [&sources] { sources.refresh(); });
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::demoInstalled,
                     &characters, [&characters] { characters.refresh(); });
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::demoInstalled,
                     &worldViews, [&worldViews] { worldViews.refresh(); });
    QObject::connect(&workspaceCatalog, &WorkspaceCatalogViewModel::demoInstalled,
                     &simulation, [&simulation] { simulation.reload(); });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("simulation"), &simulation);
    engine.rootContext()->setContextProperty(QStringLiteral("workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("workspaceCatalog"), &workspaceCatalog);
    engine.rootContext()->setContextProperty(QStringLiteral("sources"), &sources);
    engine.rootContext()->setContextProperty(QStringLiteral("characters"), &characters);
    engine.rootContext()->setContextProperty(QStringLiteral("packages"), &packages);
    engine.rootContext()->setContextProperty(QStringLiteral("providers"), &providers);
    engine.rootContext()->setContextProperty(QStringLiteral("extractionJobs"), &extractionJobs);
    engine.rootContext()->setContextProperty(QStringLiteral("candidateReview"), &candidateReview);
    engine.rootContext()->setContextProperty(QStringLiteral("worldViews"), &worldViews);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("XuyanForge"), QStringLiteral("Main"));

    if (arguments.contains(QStringLiteral("--workspace-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 1);
    }
    if (arguments.contains(QStringLiteral("--sources-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 2);
    }
    if (arguments.contains(QStringLiteral("--characters-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 3);
    }
    if (arguments.contains(QStringLiteral("--packages-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 4);
    }
    if (arguments.contains(QStringLiteral("--providers-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 5);
    }
    if (arguments.contains(QStringLiteral("--workspaces-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 6);
    }
    if (arguments.contains(QStringLiteral("--tasks-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 7);
    }
    if (arguments.contains(QStringLiteral("--review-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 8);
    }
    if (arguments.contains(QStringLiteral("--views-page")) && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().first()->setProperty("activePage", 9);
    }
    const auto screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        const auto screenshotPath = arguments.at(screenshotIndex + 1);
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
