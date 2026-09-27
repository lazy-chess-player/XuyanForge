#include "world_views_view_model.h"
#include "candidate_review_view_model.h"
#include "source_view_model.h"
#include "extraction_job_view_model.h"

#include "xuyan/application/world_graph_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/application/evidence_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

/** @brief 在断言失败时给出具体的回归场景。 */
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

/** @brief 为本次进程创建独立的临时数据库目录并在退出时清理。 */
class TemporaryWorkspace final {
public:
    TemporaryWorkspace() {
        directory_ = std::filesystem::temp_directory_path() /
            ("xuyan-vm-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(directory_);
    }
    ~TemporaryWorkspace() { std::error_code ignored; std::filesystem::remove_all(directory_, ignored); }
    /** @brief 返回仅供当前测试使用的数据库文件路径。 */
    std::filesystem::path database() const { return directory_ / "workspace.sqlite"; }
private:
    std::filesystem::path directory_;
};

/** @brief 循环处理 Qt 事件直到任一视图模型的异步读取结束或超时。 */
template <typename ViewModel>
void waitUntilIdle(ViewModel& view_model) {
    if (!view_model.busy()) return;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&view_model, &ViewModel::changed, &loop, [&] {
        if (!view_model.busy()) loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    require(!view_model.busy(), "world view asynchronous read timed out");
}

/** @brief 等待来源片段异步落地，避免用固定休眠掩盖回调时序问题。 */
void waitUntilPreviewReady(SourceViewModel& view_model) {
    if (!view_model.previewLoading()) return;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&view_model, &SourceViewModel::changed, &loop, [&] {
        if (!view_model.previewLoading()) loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    require(!view_model.previewLoading(), "source preview read timed out");
    require(view_model.errorText().isEmpty(), "source preview read failed");
}

/** @brief 执行仅用于当前临时数据库的合成 SQL，失败时携带 SQLite 原因。 */
void executeFixtureSql(const std::filesystem::path& database, const char* sql) {
    sqlite3* handle = nullptr;
    require(sqlite3_open(database.string().c_str(), &handle) == SQLITE_OK && handle != nullptr,
            "candidate fixture database must open");
    const auto status = sqlite3_exec(handle, sql, nullptr, nullptr, nullptr);
    const auto error = status == SQLITE_OK ? std::string{} : std::string(sqlite3_errmsg(handle));
    sqlite3_close(handle);
    if (status != SQLITE_OK) throw std::runtime_error("candidate fixture SQL failed: " + error);
}

/** @brief 构造超过一页的两世界候选，仅用于离线校对列表回归。 */
void seedCandidates(const std::filesystem::path& database) {
    executeFixtureSql(database, R"SQL(
PRAGMA foreign_keys=ON;
BEGIN;
INSERT INTO source_document VALUES('source-a','world-a','来源甲','sha-a','','','1','2026-09-27');
INSERT INTO source_document VALUES('source-b','world-b','来源乙','sha-b','','','1','2026-09-27');
INSERT INTO extraction_job(id,source_id,status,schema_version,prompt_version,provider_connection_id,model_id,
    total_steps,completed_steps,cancel_requested,revision,created_at,updated_at)
VALUES('job-a','source-a','completed','candidate-v1','extract-v1','','',101,101,0,1,'2026-09-27','2026-09-27'),
      ('job-b','source-b','completed','candidate-v1','extract-v1','','',1,1,0,1,'2026-09-27','2026-09-27');
WITH RECURSIVE numbers(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM numbers WHERE n<102)
INSERT INTO extraction_candidate(id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,
    start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,
    prompt_version,revision,created_at,updated_at)
SELECT printf('candidate-%03d',n),CASE WHEN n<=101 THEN 'job-a' ELSE 'job-b' END,n,
       CASE WHEN n<=101 THEN 'source-a' ELSE 'source-b' END,'entity',printf('候选 %03d',n),
       '{}',n,n+1,'证据','fixture-hash','original_fact','candidate','candidate-v1','extract-v1',
       1,'2026-09-27','2026-09-27' FROM numbers;
COMMIT;
)SQL");
}

/** @brief 给两个世界分别写入一条合成时间事件，以检查跨世界隔离。 */
void seedWorlds(const std::filesystem::path& database) {
    xuyan::storage::WorkspaceRepository repository(database);
    require(repository.createWorldTemplate("world-a", "测试世界甲").ok(), "seed world A failed");
    require(repository.createWorldTemplate("world-b", "测试世界乙").ok(), "seed world B failed");
    xuyan::application::WorldGraphService graph(database);
    for (const auto& world : {std::string("world-a"), std::string("world-b")}) {
        xuyan::domain::TimelineEvent event;
        event.id = "event-" + world;
        event.world_id = world;
        event.name = "事件-" + world;
        require(graph.saveTimelineEvent("command-" + world, std::move(event), 0).ok(),
                "seed timeline event failed");
    }
}

/** @brief 检查空选择不回退首个世界，以及 A→B→A 切换不串数据。 */
void testSelectionAndIsolation(const std::filesystem::path& database) {
    WorldViewsViewModel view_model(database);
    require(!view_model.busy() && view_model.timeline().isEmpty(), "empty selection must not load data");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty(), "world A load failed");
    require(view_model.timeline().size() == 1 &&
            view_model.timeline().front().toMap().value("id").toString() == "event-world-a",
            "world A timeline incorrect");
    view_model.setWorldId("world-b");
    waitUntilIdle(view_model);
    require(view_model.timeline().size() == 1 &&
            view_model.timeline().front().toMap().value("id").toString() == "event-world-b",
            "world B timeline incorrect");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.timeline().size() == 1 &&
            view_model.timeline().front().toMap().value("id").toString() == "event-world-a",
            "return to world A timeline incorrect");
    view_model.setWorldId({});
    require(!view_model.busy() && view_model.timeline().isEmpty(), "empty selection must clear old data");
    view_model.refresh();
    require(!view_model.busy() && view_model.timeline().isEmpty(), "refresh must not select first world");
}

/** @brief 阻塞线程池后排队两次读取，验证迟到的 A 回调不能覆盖 B。 */
void testLateCallbackIsolation(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    WorldViewsViewModel view_model(database);
    view_model.setWorldId("world-a");
    view_model.setWorldId("world-b");
    require(view_model.timeline().isEmpty(), "world switch must clear stale data immediately");
    release.release();
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty(), "world B queued load failed");
    require(view_model.timeline().size() == 1 &&
            view_model.timeline().front().toMap().value("id").toString() == "event-world-b",
            "late world A callback overwrote world B");
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(view_model.timeline().front().toMap().value("id").toString() == "event-world-b",
            "late queued callback overwrote world B after idle");
    pool->setMaxThreadCount(old_limit);
}

/** @brief 检查候选校对在世界、页码和末页删除后的数据隔离。 */
void testCandidatePaging(const std::filesystem::path& database) {
    CandidateReviewViewModel view_model(database);
    require(!view_model.busy() && view_model.candidates().isEmpty(), "empty candidate selection must not load data");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty() && view_model.totalCount() == 101
            && view_model.pageCount() == 2 && view_model.candidates().size() == 100,
            "world A first candidate page incorrect");
    require(view_model.candidates().front().toMap().value("id").toString() == "candidate-001",
            "candidate paging order incorrect");
    view_model.nextPage();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 1 && view_model.candidates().size() == 1
            && view_model.selectedId() == "candidate-101", "world A last candidate page incorrect");
    executeFixtureSql(database, "DELETE FROM extraction_candidate WHERE id='candidate-101';");
    view_model.refresh();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 0 && view_model.totalCount() == 100
            && view_model.candidates().size() == 100, "deleted last candidate page must recover");
    view_model.setWorldId("world-b");
    waitUntilIdle(view_model);
    require(view_model.totalCount() == 1 && view_model.selectedId() == "candidate-102",
            "world B candidate isolation failed");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.totalCount() == 100 && view_model.selectedId() == "candidate-001",
            "return to world A candidate isolation failed");
    view_model.setWorldId({});
    require(!view_model.busy() && view_model.candidates().isEmpty() && view_model.totalCount() == 0,
            "empty world must clear candidate queue");
}

/** @brief 验证排队的旧世界候选读取不能覆盖当前世界的队列。 */
void testCandidateLateCallback(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    CandidateReviewViewModel view_model(database);
    view_model.setWorldId("world-a");
    view_model.setWorldId("world-b");
    release.release();
    waitUntilIdle(view_model);
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(view_model.totalCount() == 1 && view_model.selectedId() == "candidate-102",
            "late candidate callback overwrote world B");
    pool->setMaxThreadCount(old_limit);
}

/** @brief 在旧页读取尚未开始时切换审核筛选，确保旧结果不能回填。 */
void testCandidateFilterDuringRead(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    CandidateReviewViewModel view_model(database);
    view_model.setWorldId("world-a");
    view_model.setFilter("accepted");
    release.release();
    waitUntilIdle(view_model);
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(view_model.errorText().isEmpty() && view_model.filter() == "accepted"
            && view_model.candidates().isEmpty() && view_model.totalCount() == 0,
            "old pending candidate page must not overwrite new status filter");
    view_model.setFilter("candidate");
    waitUntilIdle(view_model);
    require(view_model.totalCount() == 100 && view_model.candidates().size() == 100,
            "returning to pending status must reload scoped candidates");
    pool->setMaxThreadCount(old_limit);
}

/** @brief 删除末页全部候选后，页码和翻页状态都恢复为空队列。 */
void testCandidateEmptyLastPage(const std::filesystem::path& database) {
    CandidateReviewViewModel view_model(database);
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    view_model.nextPage();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 1 && view_model.candidates().size() == 1,
            "candidate empty-page fixture must start on last page");
    executeFixtureSql(database, "DELETE FROM extraction_candidate WHERE source_id='source-a';");
    view_model.refresh();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 0 && view_model.pageCount() == 0
            && view_model.totalCount() == 0 && view_model.candidates().isEmpty()
            && !view_model.canPreviousPage() && !view_model.canNextPage(),
            "empty last page must reset navigation and visible candidates");
}

/** @brief 验证来源列表只展示当前世界，空世界不会回退或泄露旧章节。 */
void testSourceSelection(const std::filesystem::path& database) {
    SourceViewModel view_model(database);
    require(view_model.sourceItems().isEmpty(), "empty source selection must not load data");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty() && view_model.sourceItems().size() == 1
            && view_model.selectedSourceId() == "source-a", "world A source list incorrect");
    view_model.setWorldId("world-b");
    waitUntilIdle(view_model);
    require(view_model.sourceItems().size() == 1 && view_model.selectedSourceId() == "source-b",
            "world B source list incorrect");
    view_model.setWorldId({});
    require(view_model.sourceItems().isEmpty() && view_model.selectedSourceId().isEmpty(),
            "empty world must clear source list");
}

/** @brief 验证来源后台读取经历 A→B→A 后不会重放第一轮的结果。 */
void testSourceLateCallback(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    SourceViewModel view_model(database);
    view_model.setWorldId("world-a");
    view_model.setWorldId("world-b");
    view_model.setWorldId("world-a");
    release.release();
    waitUntilIdle(view_model);
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(view_model.errorText().isEmpty() && view_model.sourceItems().size() == 1
            && view_model.selectedSourceId() == "source-a",
            "late source callback polluted current world");
    pool->setMaxThreadCount(old_limit);
}

/** @brief 验证导入和章节保存失败会结束忙碌态，并能在修复本地文件后重试。 */
void testSourceFailureAndRetry() {
    TemporaryWorkspace workspace;
    const auto database = workspace.database();
    xuyan::storage::WorkspaceRepository repository(database);
    require(repository.createWorldTemplate("world-retry", "临时重试世界").ok(),
            "retry test world must be created");
    SourceViewModel view_model(database);
    view_model.setWorldId("world-retry");
    waitUntilIdle(view_model);

    const auto manuscript = database.parent_path() / "retry-source.md";
    const auto manuscript_url = QUrl::fromLocalFile(QString::fromStdWString(manuscript.wstring()));
    view_model.importFile(manuscript_url, "world-retry");
    waitUntilIdle(view_model);
    require(!view_model.busy() && !view_model.errorText().isEmpty() && view_model.sourceItems().isEmpty(),
            "missing import file must report an error without leaving the view busy");

    {
        std::ofstream output(manuscript, std::ios::binary);
        output << "# 第一章 测试\n这是运行时生成的正文。\n";
        require(output.good(), "retry source must be written");
    }
    view_model.importFile(manuscript_url, "world-retry");
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.errorText().isEmpty() && view_model.sourceItems().size() == 1
            && view_model.selectedChapterIndex() == 0,
            "import retry must select the newly imported source");

    auto source = repository.loadSource(view_model.selectedSourceId().toStdString());
    require(source.ok(), "imported retry source must be readable");
    const auto normalized = database.parent_path() / source.value->normalized_asset_ref;
    auto hidden = normalized;
    hidden += ".hidden";
    std::filesystem::rename(normalized, hidden);
    const auto chapter = view_model.chapterItems()[0].toMap();
    view_model.saveChapter(0, QStringLiteral("第一章 修订"),
                           chapter.value("start").toLongLong(), chapter.value("end").toLongLong());
    waitUntilIdle(view_model);
    require(!view_model.busy() && !view_model.errorText().isEmpty()
            && view_model.chapterItems()[0].toMap().value("title") == chapter.value("title"),
            "missing normalized asset must reject chapter save without changing the visible revision");

    std::filesystem::rename(hidden, normalized);
    view_model.saveChapter(0, QStringLiteral("第一章 修订"),
                           chapter.value("start").toLongLong(), chapter.value("end").toLongLong());
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.errorText().isEmpty() && !view_model.busy()
            && view_model.chapterItems()[0].toMap().value("title").toString() == QStringLiteral("第一章 修订"),
            "chapter save retry must succeed after restoring the local source asset");
}

/** @brief 用运行时生成的长章节验证窗口边界、跨章证据定位和过期回调隔离。 */
void testLongChapterWindows() {
    TemporaryWorkspace workspace;
    const auto database = workspace.database();
    const auto manuscript = database.parent_path() / "long-chapter.md";
    const std::string first_quote = "甲🙂乙";
    const std::string second_quote = "丙🙂丁";
    const std::string crossing_quote = "\n# 第二章 转折\n";
    const std::string raw = std::string("# 第一章 长篇\n") + std::string(75000, 'a') + first_quote
        + std::string(45000, 'b') + "\n# 第二章 转折\n" + std::string(60000, 'c') + second_quote + "\n";
    {
        std::ofstream output(manuscript, std::ios::binary);
        output.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        require(output.good(), "synthetic long chapter must be written");
    }
    xuyan::storage::WorkspaceRepository repository(database);
    require(repository.createWorldTemplate("world-preview", "临时测试世界").ok(), "preview world seed failed");
    xuyan::domain::WorldEntity entity;
    entity.id = "preview-entity";
    entity.world_id = "world-preview";
    entity.kind = "item";
    entity.name = "临时条目";
    require(repository.createEntity("preview-entity-command", entity).ok(), "preview entity seed failed");
    xuyan::application::SourceImportService importer(database);
    auto imported = importer.importTextFile("preview-source-command", manuscript, "1", "world-preview");
    require(imported.ok() && imported.value->chapters.size() == 2, "long chapter import failed");
    xuyan::application::EvidenceService evidence_service(database);
    /** @brief 以标准化文本的真实码点坐标创建临时证据，不在产品内置样例。 */
    const auto make_evidence = [&](const std::string& command, const std::string& quote) {
        const auto byte = raw.find(quote);
        require(byte != std::string::npos, "synthetic quote missing");
        const auto start = xuyan::domain::utf8CodepointCount(std::string_view(raw).substr(0, byte));
        const auto length = xuyan::domain::utf8CodepointCount(quote);
        auto result = evidence_service.create(command, entity.id, "description", imported.value->id,
                                              start, start + length, "original_fact");
        require(result.ok() && result.value->quote == quote, "synthetic evidence creation failed");
        return start;
    };
    const auto first_start = make_evidence("preview-evidence-first", first_quote);
    const auto second_start = make_evidence("preview-evidence-second", second_quote);
    const auto crossing_start = make_evidence("preview-evidence-crossing", crossing_quote);

    SourceViewModel view_model(database);
    view_model.setWorldId("world-preview");
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.chapterItems().size() == 2 && view_model.selectedChapterIndex() == 0,
            "preview must start at first chapter");
    require(view_model.previewEnd() - view_model.previewStart() == 50000
            && !view_model.canPreviousWindow() && view_model.canNextWindow(),
            "first long-chapter window boundaries incorrect");
    view_model.nextWindow();
    waitUntilPreviewReady(view_model);
    require(view_model.previewStart() == 50000 && view_model.canPreviousWindow()
            && view_model.previewText().contains(QString::fromStdString(first_quote)),
            "next window must read the middle of the first chapter");
    view_model.previousWindow();
    waitUntilPreviewReady(view_model);
    require(view_model.previewStart() == 0 && !view_model.canPreviousWindow(),
            "previous window must restore first chapter start");

    /** @brief 从当前来源的证据列表按绝对码点坐标取得稳定下标。 */
    const auto find_evidence = [&](std::size_t start) {
        for (int index = 0; index < view_model.evidenceItems().size(); ++index) {
            if (view_model.evidenceItems()[index].toMap().value("start").toLongLong()
                == static_cast<qlonglong>(start)) return index;
        }
        return -1;
    };
    require(find_evidence(first_start) >= 0 && find_evidence(second_start) >= 0
            && find_evidence(crossing_start) >= 0, "all evidence references must be listed");
    view_model.selectEvidence(find_evidence(first_start));
    waitUntilPreviewReady(view_model);
    require(view_model.selectedChapterIndex() == 0
            && view_model.previewStart() <= static_cast<qlonglong>(first_start)
            && view_model.previewEnd() >= static_cast<qlonglong>(first_start + 3)
            && view_model.previewText().mid(view_model.highlightStart(),
                                            view_model.highlightEnd() - view_model.highlightStart())
                == QString::fromStdString(first_quote),
            "first evidence must jump to its window with UTF-16 selection");
    view_model.selectEvidence(find_evidence(crossing_start));
    waitUntilPreviewReady(view_model);
    require(view_model.selectedChapterIndex() == 0
            && view_model.previewText().mid(view_model.highlightStart(),
                                            view_model.highlightEnd() - view_model.highlightStart())
                == QString::fromStdString(crossing_quote),
            "cross-chapter evidence must show its complete quote in the starting chapter");
    view_model.selectEvidence(find_evidence(second_start));
    waitUntilPreviewReady(view_model);
    require(view_model.selectedChapterIndex() == 1
            && view_model.previewText().mid(view_model.highlightStart(),
                                            view_model.highlightEnd() - view_model.highlightStart())
                == QString::fromStdString(second_quote),
            "second evidence must jump across chapters with UTF-16 selection");
    bool flashed_first_chapter = false;
    /** @brief 记录刷新与保存期间是否曾把阅读位置错误地公布为第一章。 */
    const auto chapter_observer = QObject::connect(&view_model, &SourceViewModel::changed, &view_model, [&] {
        if (view_model.selectedChapterIndex() == 0) flashed_first_chapter = true;
    });
    const auto saved_window_start = view_model.previewStart();
    view_model.refresh();
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.selectedChapterIndex() == 1 && view_model.previewStart() == saved_window_start,
            "refresh must preserve the selected chapter and long-chapter window");
    const auto selected_chapter = view_model.chapterItems()[1].toMap();
    view_model.saveChapter(1, QStringLiteral("第二章 转折（校正）"),
                           selected_chapter.value("start").toLongLong(),
                           selected_chapter.value("end").toLongLong());
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.errorText().isEmpty()
            && view_model.chapterItems()[1].toMap().value("title").toString() == QStringLiteral("第二章 转折（校正）")
            && view_model.selectedChapterIndex() == 1 && view_model.previewStart() == saved_window_start,
            "saving chapter layout must preserve the selected chapter and reading window");
    const auto first_chapter = view_model.chapterItems()[0].toMap();
    const auto revised_boundary = first_chapter.value("end").toLongLong() + 1;
    view_model.saveChapter(0, first_chapter.value("title").toString(),
                           first_chapter.value("start").toLongLong(), revised_boundary);
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.errorText().isEmpty()
            && view_model.chapterItems()[0].toMap().value("end").toLongLong() == revised_boundary
            && view_model.chapterItems()[1].toMap().value("start").toLongLong() == revised_boundary
            && view_model.selectedChapterIndex() == 1 && view_model.previewStart() == saved_window_start,
            "single-chapter boundary correction must move the adjacent boundary without losing reading position");
    QObject::disconnect(chapter_observer);
    require(!flashed_first_chapter, "refresh and save must not flash the first chapter during restoration");
    view_model.nextWindow();
    waitUntilPreviewReady(view_model);
    require(!view_model.canNextWindow(), "last chapter window must stop at chapter end");

    // 把新旧两个窗口请求排在同一工作线程后，验证旧回调不会覆盖最终窗口。
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    view_model.selectChapter(0);
    view_model.nextWindow();
    release.release();
    waitUntilPreviewReady(view_model);
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(!view_model.previewLoading() && view_model.previewStart() == 50000
            && view_model.previewText().contains(QString::fromStdString(first_quote)),
            "stale preview callback overwrote current window");
    pool->setMaxThreadCount(old_limit);

    view_model.selectEvidence(find_evidence(second_start));
    waitUntilPreviewReady(view_model);
    const auto position_before_relayout = view_model.previewStart();
    auto current_source = repository.loadSource(imported.value->id);
    require(current_source.ok(), "source must remain readable before external chapter relayout");
    auto renamed_chapters = current_source.value->chapters;
    renamed_chapters[1].id = "replacement-chapter-id";
    auto relayout = importer.saveChapters("external-chapter-relayout", imported.value->id,
                                         current_source.value->chapter_revision, std::move(renamed_chapters));
    require(relayout.ok(), "external chapter relayout must save");
    view_model.refresh();
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.selectedChapterIndex() == 1 && view_model.previewStart() == position_before_relayout,
            "refresh must recover reading position by codepoint when a chapter ID changes");
    view_model.selectEvidence(find_evidence(second_start));
    waitUntilPreviewReady(view_model);
    require(view_model.previewText().mid(view_model.highlightStart(),
                                         view_model.highlightEnd() - view_model.highlightStart())
                == QString::fromStdString(second_quote),
            "external chapter relayout must not move absolute source evidence");

    const auto second_manuscript = database.parent_path() / "another-source.md";
    {
        std::ofstream output(second_manuscript, std::ios::binary);
        output << "# 第一章 新来源\n另一部小说的正文。\n";
        require(output.good(), "second synthetic source must be written");
    }
    auto second_source = importer.importTextFile("preview-second-source-command", second_manuscript,
                                                 "1", "world-preview");
    require(second_source.ok(), "second synthetic source must import");
    view_model.refresh();
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    view_model.selectSourceId(QString::fromStdString(imported.value->id));
    waitUntilPreviewReady(view_model);
    int second_source_index = -1;
    for (int index = 0; index < view_model.sourceItems().size(); ++index) {
        if (view_model.sourceItems()[index].toMap().value("id").toString()
            == QString::fromStdString(second_source.value->id)) second_source_index = index;
    }
    require(second_source_index >= 0, "second source must appear in the current world");
    view_model.selectSource(second_source_index);
    waitUntilPreviewReady(view_model);
    view_model.refresh();
    waitUntilIdle(view_model);
    waitUntilPreviewReady(view_model);
    require(view_model.selectedSourceId() == QString::fromStdString(second_source.value->id),
            "manual source choice must supersede a previous ID-based selection after refresh");
}

/** @brief 验证任务列表只显示当前世界，外界传入其他世界任务编号时不执行操作。 */
void testJobSelection(const std::filesystem::path& database) {
    ExtractionJobViewModel view_model(database);
    waitUntilIdle(view_model);
    require(view_model.jobs().isEmpty(), "empty job selection must not load tasks");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty() && view_model.jobs().size() == 1
            && view_model.jobs().front().toMap().value("id").toString() == "job-a",
            "world A task list incorrect");
    view_model.cancelJob("job-b", 1);
    view_model.runRemoteSample("job-b");
    require(!view_model.busy() && view_model.jobs().size() == 1,
            "another world's task must not be executable from current list");
    view_model.setWorldId("world-b");
    waitUntilIdle(view_model);
    require(view_model.jobs().size() == 1
            && view_model.jobs().front().toMap().value("id").toString() == "job-b",
            "world B task list incorrect");
    view_model.setWorldId({});
    require(view_model.jobs().isEmpty() && !view_model.busy(), "empty world must clear task list");
}

/** @brief 验证旧任务读取迟到时当前世界列表保持隔离。 */
void testJobLateCallback(const std::filesystem::path& database) {
    ExtractionJobViewModel view_model(database);
    waitUntilIdle(view_model);
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    view_model.setWorldId("world-a");
    view_model.setWorldId("world-b");
    view_model.setWorldId("world-a");
    release.release();
    waitUntilIdle(view_model);
    pool->waitForDone(10000);
    QCoreApplication::processEvents();
    require(view_model.errorText().isEmpty() && view_model.jobs().size() == 1
            && view_model.jobs().front().toMap().value("id").toString() == "job-a",
            "late task callback polluted current world");
    pool->setMaxThreadCount(old_limit);
}

} // namespace

/** @brief 运行世界视图模型的异步选择与回调隔离回归。 */
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        TemporaryWorkspace workspace;
        seedWorlds(workspace.database());
        seedCandidates(workspace.database());
        testSelectionAndIsolation(workspace.database());
        testLateCallbackIsolation(workspace.database());
        testCandidatePaging(workspace.database());
        testCandidateLateCallback(workspace.database());
        testCandidateFilterDuringRead(workspace.database());
        testSourceSelection(workspace.database());
        testSourceLateCallback(workspace.database());
        testSourceFailureAndRetry();
        testLongChapterWindows();
        testJobSelection(workspace.database());
        testJobLateCallback(workspace.database());
        {
            TemporaryWorkspace empty_page_workspace;
            seedWorlds(empty_page_workspace.database());
            seedCandidates(empty_page_workspace.database());
            testCandidateEmptyLastPage(empty_page_workspace.database());
        }
        std::cout << "World, candidate, source and job view-model tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
