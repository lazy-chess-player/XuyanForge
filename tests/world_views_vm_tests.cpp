#include "world_views_view_model.h"
#include "candidate_review_view_model.h"
#include "source_view_model.h"
#include "extraction_job_view_model.h"
#include "package_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/world_graph_service.h"
#include "xuyan/application/extraction_job_service.h"
#include "xuyan/application/mock_extraction_processor.h"
#include "xuyan/application/remote_extraction_processor.h"
#include "xuyan/application/provider_connection_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/application/evidence_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"
#include "xuyan/package/json.h"
#include "xuyan/package/zip_archive.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QRegularExpression>
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
#include <atomic>
#include <memory>

/*
 * 职责：测试专用友元入口，只等待解析模型自有线程，不消费界面结束回调，以复现末尾竞态。
 * 生命周期与线程：无成员资源；测试主线程调用，所借模型及其线程池须存活。
 */
struct ExtractionJobViewModelTestAccess {
    /*
     * 功能：等待已排队的自有线程，不处理Qt事件，以稳定复现检查点末尾竞态。
     * 参数：model：输入，仍存活的视图模型引用，仅借用；允许 worker 已结束而界面回调未处理。
     * 返回：无。
     * 失败：10000 毫秒内线程池未结束抛 runtime_error。
     * 副作用：等待自有线程池，不处理 Qt 事件、不执行排队的结束回调。
     * 线程与生命周期：测试主线程同步调用，模型须覆盖 waitForDone；用于稳定制造末尾竞态。
     */
    static void joinWorker(ExtractionJobViewModel& model) {
        if (!model.worker_pool_.waitForDone(10000)) throw std::runtime_error("等待解析工作线程退出超时");
    }
};

namespace {

/*
 * 功能：在断言失败时给出具体的回归场景。
 * 参数：condition：输入，true 为通过；message：输入，非空零结尾失败说明，仅本调用借用。
 * 返回：无。
 * 失败：condition 为 false 抛 runtime_error，异常构造失败继续传播。
 * 副作用：只生成失败异常，不改变视图模型或测试数据。
 * 线程与生命周期：在调用线程同步执行，不保存指针。
 */
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

/*
 * 职责：持有 UUID 独占的系统临时目录清理责任，向用例提供数据库路径，不拥有活动连接。
 * 生命周期与线程：主线程用例作用域内创建、最后销毁；不得复制出第二份清理责任。
 */
class TemporaryWorkspace final {
public:
    /*
     * 功能：创建带 UUID 的独占测试目录。
     * 参数：无。
     * 返回：初始化 directory_ 并尝试建立目录。
     * 失败：临时路径、UUID 字符串或建目录异常传播；不单独检查 create_directories 的布尔返回。
     * 副作用：只在系统临时根写目录，不读取用户工作区或预置资料。
     * 线程与生命周期：主线程同步执行，目录寿命由本守卫覆盖所有服务/线程。
     */
    TemporaryWorkspace() {
        directory_ = std::filesystem::temp_directory_path() /
            ("xuyan-vm-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
        std::filesystem::create_directories(directory_);
    }
    /*
     * 功能：清理当前用例的 UUID 临时目录及内容。
     * 参数：无。
     * 返回：完成删除尝试及守卫销毁。
     * 失败：删除错误通过 error_code 忽略，无失败上报；不重试。
     * 副作用：递归删除本对象创建的 directory_，不删除临时根。
     * 线程与生命周期：主线程同步析构；所有模型、文件和工作线程必须先退出，不接管用户目录。
     */
    ~TemporaryWorkspace() { std::error_code ignored; std::filesystem::remove_all(directory_, ignored); }
    /*
     * 功能：返回仅供当前测试使用的数据库文件路径。
     * 参数：无。
     * 返回：本对象独占目录下 workspace.sqlite 的路径值，尚未建库也可返回。
     * 失败：路径构造/分配异常传播。
     * 副作用：只生成路径，不读取文件或创建数据库。
     * 线程与生命周期：调用线程同步只读；路径所指目录只在 TemporaryWorkspace 存活期间有效。
     */
    std::filesystem::path database() const { return directory_ / "workspace.sqlite"; }
private:
    /* 自有 UUID 临时目录绝对路径，构造时赋值、无外部默认目录；database 读取、析构清理，寿命随守卫。 */
    std::filesystem::path directory_;
};

/*
 * 功能：循环处理 Qt 事件直到任一视图模型的异步读取结束或超时。
 * 参数：view_model：输入，调用线程拥有的视图模型引用，须有 busy 与 changed 接口。
 * 返回：无；返回时 busy 为 false，不代表 errorText 为空。
 * 失败：事件循环超时且仍 busy 时断言抛 runtime_error；未单独检查业务错误。
 * 副作用：必要时运行最多 10000 毫秒 Qt 局部事件循环，处理读取回调与信号。
 * 线程与生命周期：主线程执行；临时 loop 为连接上下文，销毁时断开借用捕获，模型须全程存活。
 */
template <typename ViewModel>
void waitUntilIdle(ViewModel& view_model) {
    if (!view_model.busy()) return;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    /*
     * 功能：收到 changed 后结束已空闲模型的局部等待。
     * 参数：无。
     * 返回：无。
     * 失败：无显式失败路径。
     * 副作用：busy 为 false 时调用 loop.quit。
     * 线程与生命周期：主线程信号回调；loop 是连接上下文，销毁自动断开；模型及 loop 引用仅等待期间有效。
     */
    QObject::connect(&view_model, &ViewModel::changed, &loop, [&] {
        if (!view_model.busy()) loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    require(!view_model.busy(), "等待视图模型异步读取超时");
}

/*
 * 功能：等待来源片段异步落地，避免用固定休眠掩盖回调时序问题。
 * 参数：view_model：输入，来源视图模型可变引用，测试主线程对象，调用期间存活。
 * 返回：无；previewLoading 为 false 且 errorText 为空时返回。
 * 失败：10000 毫秒后仍加载或错误文本非空时断言抛 runtime_error。
 * 副作用：必要时运行局部事件循环处理预览结果，不主动改源文。
 * 线程与生命周期：主线程执行；连接以上下文 loop 管理，捕获只在等待期间有效。
 */
void waitUntilPreviewReady(SourceViewModel& view_model) {
    if (!view_model.previewLoading()) return;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    /*
     * 功能：收到 changed 后结束预览等待。
     * 参数：无。
     * 返回：无。
     * 失败：无显式失败路径。
     * 副作用：previewLoading 为 false 时退出 loop。
     * 线程与生命周期：主线程信号回调，loop 上下文管理连接和借用生命周期。
     */
    QObject::connect(&view_model, &SourceViewModel::changed, &loop, [&] {
        if (!view_model.previewLoading()) loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(10000);
    loop.exec();
    require(!view_model.previewLoading(), "等待来源预览读取超时");
    require(view_model.errorText().isEmpty(), "来源预览读取失败");
}

/*
 * 功能：执行仅用于当前临时数据库的合成 SQL，失败时携带 SQLite 原因。
 * 参数：database：输入，测试独占且结构已初始化的 SQLite 路径，只读借用；sql：输入，非空零结尾自有 SQL，不接受用户文本。
 * 返回：无。
 * 失败：连接失败断言抛异常；SQL 失败关闭正常打开的连接后抛 SQLite 原因；不包装任意其他异常。
 * 副作用：打开测试连接并执行 SQL，可写数据；不自行加事务，语句是否原子由传入 SQL 决定。
 * 线程与生命周期：调用线程同步执行，正常执行路径关闭连接，不接管数据库文件。
 */
void executeFixtureSql(const std::filesystem::path& database, const char* sql) {
    sqlite3* handle = nullptr;
    require(sqlite3_open(database.string().c_str(), &handle) == SQLITE_OK && handle != nullptr,
            "candidate fixture database must open");
    const auto status = sqlite3_exec(handle, sql, nullptr, nullptr, nullptr);
    const auto error = status == SQLITE_OK ? std::string{} : std::string(sqlite3_errmsg(handle));
    sqlite3_close(handle);
    if (status != SQLITE_OK) throw std::runtime_error("candidate fixture SQL failed: " + error);
}

/*
 * 功能：构造超过一页的两世界候选及完整原文任务快照，仅用于离线列表回归。
 * 参数：database：输入，测试独占且已建两世界的库路径，须无同标识来源/任务/候选，只借用本次调用。
 * 返回：无。
 * 失败：SQL 建立失败经 executeFixtureSql 抛异常，不转换成空列表。
 * 副作用：在单事务插入甲世界 101 条、乙世界 1 条候选及来源/任务/输入和生成快照。
 * 线程与生命周期：同步调用，不访问模型或用户工作区；所有文本为测试自有。
 */
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
INSERT INTO extraction_job_input_snapshot(job_id,mode,density,algorithm_version)
SELECT id,'raw','none','source-v1' FROM extraction_job;
INSERT INTO extraction_job_generation_snapshot(job_id,reasoning_effort,output_format)
SELECT id,'provider_default','provider_schema_v1' FROM extraction_job;
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

/*
 * 功能：给两个世界分别写入一条合成时间事件，以检查跨世界隔离。
 * 参数：database：输入，当前测试专用库路径，不得已有 world-a/world-b，调用期间借用。
 * 返回：无。
 * 失败：创建世界或事件失败时断言抛异常，前面已成功的独立命令不会整体回滚。
 * 副作用：显式创建两个世界及各一个时间事件，不注入生产初始化路径。
 * 线程与生命周期：同步执行本线程仓储，连接由局部服务释放。
 */
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

/*
 * 功能：验证关系/地点到界面数据的映射保留未知值、已知零、方向及真实性，不修改可见控件。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：新建独占世界，写已知零/未知关系和位置，断言 QVariant 映射保留方向和真实性。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testGraphSemanticMapping() {
    TemporaryWorkspace workspace;
    xuyan::storage::WorkspaceRepository repository(workspace.database());
    require(repository.createWorldTemplate("semantic-vm-world", "语义映射回归").ok(), "semantic view world must create");
    for (int index = 0; index < 4; ++index) {
        xuyan::domain::WorldEntity entity;
        entity.id = "semantic-vm-entity-" + std::to_string(index); entity.world_id = "semantic-vm-world";
        entity.name = "界面语义条目" + std::to_string(index); entity.kind = index < 2 ? "character" : "location";
        require(repository.createEntity("semantic-vm-create-" + std::to_string(index), entity).ok(), "semantic view entity must create");
    }
    xuyan::domain::DirectedRelation known;
    known.id = "semantic-vm-known"; known.world_id = "semantic-vm-world";
    known.from_entity_id = "semantic-vm-entity-0"; known.to_entity_id = "semantic-vm-entity-1";
    known.dimension = "明确零值"; known.strength = 0;
    require(repository.saveDirectedRelation("semantic-vm-known-save", known, 0).ok(), "known zero relation must save");
    auto unknown = known;
    unknown.id = "semantic-vm-unknown"; unknown.strength.reset(); unknown.bidirectional = true;
    unknown.truth_status = "claim"; unknown.evidence_status = "assumption";
    require(repository.saveDirectedRelation("semantic-vm-unknown-save", unknown, 0).ok(), "unknown bidirectional claim must save");
    xuyan::domain::LocationPlacement unplaced;
    unplaced.location_id = "semantic-vm-entity-2";
    require(repository.saveLocationPlacement("semantic-vm-unplaced-save", unplaced, 0).ok(), "unplaced location must save");
    auto placed = unplaced;
    placed.location_id = "semantic-vm-entity-3"; placed.image_x = 0; placed.image_y = 0;
    placed.truth_status = "hypothesis"; placed.evidence_status = "assumption";
    require(repository.saveLocationPlacement("semantic-vm-placed-save", placed, 0).ok(), "zero coordinate hypothesis must save");

    WorldViewsViewModel model(workspace.database());
    model.setWorldId("semantic-vm-world"); waitUntilIdle(model);
    require(model.errorText().isEmpty() && model.relations().size() == 2 && model.locations().size() == 2,
            "semantic view mapping must load both relation and location pairs");
    QVariantMap known_item, unknown_item, unplaced_item, placed_item;
    for (const auto& item : model.relations()) {
        auto values = item.toMap();
        if (values.value("id").toString() == "semantic-vm-known") known_item = std::move(values);
        else if (values.value("id").toString() == "semantic-vm-unknown") unknown_item = std::move(values);
    }
    for (const auto& item : model.locations()) {
        auto values = item.toMap();
        if (values.value("id").toString() == "semantic-vm-entity-2") unplaced_item = std::move(values);
        else if (values.value("id").toString() == "semantic-vm-entity-3") placed_item = std::move(values);
    }
    // 不对未知强度执行 toInt：空 QVariant 与已知的整数零必须在传给 QML 时仍可区分。
    require(!known_item.isEmpty() && known_item.value("strength").isValid() && known_item.value("strength").toInt() == 0
            && !known_item.value("bidirectional").toBool() && known_item.value("truthStatus").toString() == "fact",
            "known zero and single-direction fact must remain explicit");
    require(!unknown_item.isEmpty() && unknown_item.contains("strength") && !unknown_item.value("strength").isValid()
            && unknown_item.value("bidirectional").toBool() && unknown_item.value("truthStatus").toString() == "claim",
            "unknown strength must not become zero or lose bidirectional claim state");
    require(!unplaced_item.isEmpty() && unplaced_item.value("x").toString() == QStringLiteral("未知")
            && unplaced_item.value("y").toString() == QStringLiteral("未知") && unplaced_item.value("truthStatus").toString() == "fact",
            "unknown coordinate pair must stay unknown");
    require(!placed_item.isEmpty() && placed_item.value("x").toString() == "0" && placed_item.value("y").toString() == "0"
            && placed_item.value("truthStatus").toString() == "hypothesis", "known zero coordinates must keep hypothesis state");
    model.setWorldId({});
    require(model.relations().isEmpty() && model.locations().isEmpty(), "clearing world must clear graph semantic values");
}

/*
 * 功能：检查空选择不回退首个世界，以及 A→B→A 切换不串数据。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：只读共享夹具库，异步选择 A→B→A 和清空，不回退首个世界。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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

/*
 * 功能：阻塞线程池后排队两次读取，验证迟到的 A 回调不能覆盖 B。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：临时把全局池设一线程，阻塞后排队 A/B 读取，放行等待并核对迟到结果；正常路径恢复池上限。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testLateCallbackIsolation(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    /*
     * 功能：为世界视图迟到结果占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
    pool->start([&] { started.release(); release.acquire(); });
    started.acquire();
    WorldViewsViewModel view_model(database);
    view_model.setWorldId("world-a");
    /* 忙碌期间重复刷新不能另开读取；世界切换须推进有效代次，使已排队的甲世界回调失效。 */
    view_model.refresh();
    view_model.refresh();
    view_model.setWorldId("world-b");
    view_model.refresh();
    require(view_model.busy() && view_model.timeline().isEmpty(), "重复刷新须保持新世界读取忙碌且不显示旧结果");
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

/*
 * 功能：验证世界视图仓储构造失败会回主线程报错、结束忙碌态，并能在修复临时路径后刷新。
 * 参数：无。
 * 返回：无；目录碰撞失败及修复后的时间线读取均满足断言时返回。
 * 失败：独占目录创建/删除、修复建库或异步等待失败抛异常；未捕获的工作线程异常属于被测缺陷。
 * 副作用：将自有 workspace.sqlite 路径暂建为空目录以制造 SQLite 打开错误，再只删除该空目录；
 *   在相同路径显式创建测试世界和一条事件，刷新并检查成功；不触碰用户资料、不开网络。
 * 线程与生命周期：测试主线程创建模型和运行事件循环；仓储初始化在模型后台执行。
 *   目录守卫覆盖模型及后台读取，所有引用仅在当前用例存活，退出由守卫清理。
 */
void testWorldRefreshFailureAndRetry() {
    TemporaryWorkspace workspace;
    const auto database = workspace.database();
    require(std::filesystem::create_directory(database), "世界视图失败回归必须在独占目录创建空目录碰撞");
    WorldViewsViewModel view_model(database);
    view_model.setWorldId("world-refresh-retry");
    waitUntilIdle(view_model);
    require(!view_model.busy() && !view_model.errorText().isEmpty()
                && view_model.versions().isEmpty() && view_model.timeline().isEmpty()
                && view_model.relations().isEmpty() && view_model.locations().isEmpty()
                && view_model.routes().isEmpty() && view_model.instances().isEmpty(),
            "世界视图读取失败必须结束忙碌态、显示错误并保持所有列表为空");
    /* 这里只删除刚创建且仍为空的精确碰撞目录；SQLite 构造失败已结束，不递归操作父目录。 */
    require(std::filesystem::remove(database), "世界视图失败回归必须移除本例创建的空碰撞目录");
    {
        xuyan::storage::WorkspaceRepository repository(database);
        require(repository.createWorldTemplate("world-refresh-retry", "视图刷新重试测试").ok(),
                "刷新重试必须显式创建临时世界");
        xuyan::domain::TimelineEvent event;
        event.id = "world-refresh-retry-event";
        event.world_id = "world-refresh-retry";
        event.name = "修复后的测试事件";
        require(repository.saveTimelineEvent("world-refresh-retry-create", std::move(event), 0).ok(),
                "刷新重试必须显式创建一条临时时间事件");
    }
    view_model.refresh();
    waitUntilIdle(view_model);
    require(!view_model.busy() && view_model.errorText().isEmpty()
                && view_model.timeline().size() == 1
                && view_model.timeline().front().toMap().value("id").toString() == "world-refresh-retry-event",
            "修复测试路径后世界视图刷新必须成功并清除旧错误");
}

/*
 * 功能：检查候选校对在世界、页码和末页删除后的数据隔离。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：读取两页并删除甲世界末条候选，刷新后恢复页码，检查切世界及清空。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testCandidatePaging(const std::filesystem::path& database) {
    CandidateReviewViewModel view_model(database);
    require(!view_model.busy() && view_model.candidates().isEmpty(), "empty candidate selection must not load data");
    view_model.setWorldId("world-a");
    waitUntilIdle(view_model);
    require(view_model.errorText().isEmpty() && view_model.totalCount() == 101
            && view_model.pageCount() == 2 && view_model.candidates().size() == 100,
            "world A first candidate page incorrect");
    require(!view_model.canPreviousPage() && view_model.canNextPage(),
            "first candidate page must only allow forward navigation");
    require(view_model.candidates().front().toMap().value("id").toString() == "candidate-001",
            "candidate paging order incorrect");
    view_model.nextPage();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 1 && view_model.candidates().size() == 1
            && view_model.selectedId() == "candidate-101"
            && view_model.canPreviousPage() && !view_model.canNextPage(),
            "world A last candidate page incorrect");
    view_model.previousPage();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 0 && view_model.selectedId() == "candidate-001",
            "candidate previous-page navigation must restore the first page");
    view_model.nextPage();
    waitUntilIdle(view_model);
    require(view_model.pageIndex() == 1 && view_model.selectedId() == "candidate-101",
            "candidate forward navigation must return to the last page");
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

/*
 * 功能：验证排队的旧世界候选读取不能覆盖当前世界的队列。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：阻塞单线程全局池，排队旧世界候选读取，检查新世界列表不被覆盖；正常路径恢复池配置。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testCandidateLateCallback(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    /*
     * 功能：为候选迟到结果占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
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

/*
 * 功能：在旧页读取尚未开始时切换审核筛选，确保旧结果不能回填。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：阻塞读取后切审核筛选，检查旧待审结果不回填；正常路径恢复全局池配置。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testCandidateFilterDuringRead(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    /*
     * 功能：为候选筛选竞态占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
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

/*
 * 功能：删除末页全部候选后，页码和翻页状态都恢复为空队列。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：删除甲世界全部候选后刷新，核对页码、总数、按钮状态和列表为空。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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

/*
 * 功能：验证来源列表只展示当前世界，空世界不会回退或泄露旧章节。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：只读夹具来源列表，切世界/清空后核对无旧来源泄露。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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

/*
 * 功能：验证来源后台读取经历 A→B→A 后不会重放第一轮的结果。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：阻塞池后排队 A→B→A，核对代次隔离；正常路径恢复线程池配置。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testSourceLateCallback(const std::filesystem::path& database) {
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    /*
     * 功能：为来源代次隔离占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
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

/*
 * 功能：验证导入和章节保存失败会结束忙碌态，并能在修复本地文件后重试。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：独占目录先导入缺失文件，再写合成正文重试；临时改名标准化资产制造章节保存失败，恢复后再保存。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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

/*
 * 功能：用运行时生成的长章节验证窗口边界、跨章证据定位和过期回调隔离。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：独占目录生成两章长文本及表情证据，检查 50000 码点窗口、跨章高亮、阅读位置恢复、外部改章和旧预览回调。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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
    /*
     * 功能：从合成原文换算码点并创建逐字证据。
     * 参数：command：输入，幂等测试命令标识；quote：输入，自有逐字引文，不能为空且须在 raw 存在；均只读借用。
     * 返回：引文的绝对 Unicode 码点起点，非字节或 UTF-16 下标。
     * 失败：找不到引文或证据创建/内容不符时断言抛异常。
     * 副作用：写当前临时库证据，保留原文不变。
     * 线程与生命周期：主线程同步调用；raw/服务/实体/来源捕获仅在当前用例存活。
     */
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

    /*
     * 功能：按绝对码点起点查当前来源的证据下标。
     * 参数：start：输入，非负 Unicode 码点起点，非字节。
     * 返回：当前列表从 0 开始的首个匹配下标，未找到为 -1。
     * 失败：无显式失败路径，不保证列表在异步刷新期间保持下标。
     * 副作用：只读 evidenceItems，不保存元素引用。
     * 线程与生命周期：主线程同步执行，view_model 引用只在当前用例借用。
     */
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
    /*
     * 功能：观察刷新/保存是否短暂回到错误首章。
     * 参数：无。
     * 返回：无。
     * 失败：无显式失败路径。
     * 副作用：selectedChapterIndex 为 0 时将 flashed_first_chapter 置 true，不复位。
     * 线程与生命周期：主线程 changed 回调；测试显式 disconnect 后才释放局部标记，模型为连接上下文。
     */
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
    /*
     * 功能：为长章窗口旧回调占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
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

/*
 * 功能：验证任务列表只显示当前世界，外界传入其他世界任务编号时不执行操作。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：只读夹具任务列表，传入异世界任务标识测试取消/抽样被拒绝，不向真实模型发送。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
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

/*
 * 功能：验证旧任务读取迟到时当前世界列表保持隔离。
 * 参数：database：输入，主入口为本进程创建且已播种的独占测试库路径，只借用本次调用，部分用例会修改候选。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：阻塞池并排队世界任务读取，核对旧结果不能回填；正常路径恢复池上限。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testJobLateCallback(const std::filesystem::path& database) {
    ExtractionJobViewModel view_model(database);
    waitUntilIdle(view_model);
    auto* pool = QThreadPool::globalInstance();
    const auto old_limit = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    QSemaphore started, release;
    /*
     * 功能：为任务世界隔离占用唯一池线程。
     * 参数：无。
     * 返回：无。
     * 失败：无超时检查，release 未放行时 acquire 会持续阻塞。
     * 副作用：先通知 started 再等待 release；不读库或触发业务发送。
     * 线程与生命周期：全局线程池执行；信号量引用须存活到正常路径 waitForDone，主线程放行后恢复池上限。
     */
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

/*
 * 功能：等待自有解析批次及其结束后的列表刷新，不依赖固定休眠。
 * 参数：model：输入，当前主线程仍存活的解析视图模型可变引用。
 * 返回：无；running/busy 均结束才通过。
 * 失败：局部事件循环限时 10000 毫秒，后续 waitUntilIdle 超时或仍 running 时抛异常。
 * 副作用：处理 Qt 事件、等待批次结束及刷新；不主动暂停或取消任务。
 * 线程与生命周期：主线程同步等待；loop 上下文销毁解除捕获，工作线程由模型拥有和等待。
 */
void waitUntilBatchStopped(ExtractionJobViewModel& model) {
    if (model.running() || model.busy()) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        /*
         * 功能：等待解析与列表刷新同时结束。
         * 参数：无。
         * 返回：无。
         * 失败：无显式失败路径。
         * 副作用：running/busy 都为 false 时退出 loop。
         * 线程与生命周期：主线程信号回调；loop 上下文销毁断开，model 须覆盖整个等待。
         */
        QObject::connect(&model, &ExtractionJobViewModel::changed, &loop, [&] {
            if (!model.running() && !model.busy()) loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000); loop.exec();
    }
    QCoreApplication::processEvents();
    waitUntilIdle(model);
    require(!model.running(), "后台解析批次未能停止");
}

/*
 * 功能：在私有临时目录生成多片章节并创建离线任务，正式程序不携带这些数据。
 * 参数：database：输入，调用方拥有的独占临时数据库路径，父目录已存在且无同名夹具，只借用本调用。
 * 返回：拥有完整步骤的离线任务值，断言至少四片。
 * 失败：文件写入、导入或切片断言失败抛异常，仓储/文件异常传播。
 * 副作用：写 batch.md 合成两章，显式创建两个世界、导入并建立离线任务，不发模型。
 * 线程与生命周期：调用线程同步执行，文件由外层 TemporaryWorkspace 最终清理。
 */
xuyan::domain::ExtractionJob createBatchFixture(const std::filesystem::path& database) {
    seedWorlds(database);
    const auto manuscript = database.parent_path() / "batch.md";
    {
        std::ofstream output(manuscript, std::ios::binary);
        for (int chapter = 1; chapter <= 2; ++chapter) {
            output << "# 第" << chapter << "章\n";
            for (int line = 0; line < 8; ++line) output << "记录者打开文档。" << std::string(160, 'a') << "\n";
        }
        require(output.good(), "batch fixture file failed");
    }
    auto source = xuyan::application::SourceImportService(database).importTextFile("batch-source", manuscript, "1", "world-a");
    require(source.ok(), "batch fixture import failed");
    auto job = xuyan::application::ExtractionJobService(database).create("batch-job", source.value->id, 1000, 0, 0, 2048);
    require(job.ok() && job.value->total_steps >= 4, "batch fixture must have multiple slices in each chapter");
    return *job.value;
}

/*
 * 功能：在第一个持久化检查点阻塞调度，测试可明确控制暂停与取消的先后顺序。
 * 参数：checkpoint：输入输出，首片到达通知信号量引用；release：输入输出，测试发许可的阻塞信号量引用；starts：输入输出，累计启动次数的原子整数引用，初始由用例设 0。
 * 返回：借用这些对象的 BatchRunner，首轮首片可被测试关卡暂停。
 * 失败：返回闭包内关卡 10000 毫秒超时抛异常；复制选项/调用进度回调的异常向模型工作线程传播。
 * 副作用：创建闭包时不启动工作；执行时原子增加启动数、同步调度离线批次并阻塞首片检查点。
 * 线程与生命周期：主线程创建，模型自有工作线程执行；三个捕获须晚于模型及线程退出销毁，闭包不能跨用例保存。
 */
ExtractionJobViewModel::BatchRunner gatedBatch(QSemaphore& checkpoint, QSemaphore& release, std::atomic<int>& starts) {
    /*
     * 功能：运行受首片关卡控制的离线批次。
     * 参数：path：输入，独占测试库路径引用；id：输入，任务标识引用；options：输入，离线选项只读引用，on_progress 须可调用且 stop_token 来自模型。
     * 返回：离线处理器 Result，成功含持久化任务/处理数/停止原因，失败按错误返回。
     * 失败：选项复制、处理器或进度回调异常传播到模型工作线程，关卡超时抛异常。
     * 副作用：原子增加 starts，首轮第一片通知 checkpoint 并等待 release，写离线检查点。
     * 线程与生命周期：模型工作线程同步执行；返回闭包借用信号量和 starts 到模型 join 结束；本次 options/first_run 覆盖内层回调。
     */
    return [&](const auto& path, const auto& id, const xuyan::application::OfflineBatchOptions& options) {
        const bool first_run = starts.fetch_add(1) == 0;
        auto gated = options;
        /*
         * 功能：将离线进度透传并阻塞首轮首片检查点。
         * 参数：progress：输入，本次检查点的只读进度，processed_steps 为本批累计片数。
         * 返回：第二次 options.on_progress 的调度动作。
         * 失败：关卡 10000 毫秒超时断言抛异常；原进度回调异常传播。
         * 副作用：原回调调用两次；首轮第一片通知 checkpoint 并等待 release，不改原文。
         * 线程与生命周期：工作线程在 processBatch 内同步回调；first_run/options 引用不逃逸外层 BatchRunner。
         */
        gated.on_progress = [&](const auto& progress) {
            options.on_progress(progress);
            if (first_run && progress.processed_steps == 1) {
                checkpoint.release();
                require(release.tryAcquire(1, 10000), "checkpoint gate timed out");
            }
            return options.on_progress(progress);
        };
        return xuyan::application::MockExtractionProcessor(path).processBatch(id, gated);
    };
}

/*
 * 功能：验证显式启动、重复点击防护、片段级暂停及跨进程可读取的续跑检查点。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：独占目录生成任务，通过信号量控制首片，检查重复启动拒绝、暂停落库和续跑不重做已完成片。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testBatchPauseAndResume() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    const auto id = QString::fromStdString(job.id);
    QSemaphore checkpoint, release;
    std::atomic<int> starts{0};
    ExtractionJobViewModel model(workspace.database(), nullptr, gatedBatch(checkpoint, release, starts));
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    model.refresh(); waitUntilIdle(model);
    require(starts == 0 && !model.running(), "creation and refresh must not start a batch");
    model.startJob(id);
    require(checkpoint.tryAcquire(1, 10000), "first checkpoint did not arrive");
    QCoreApplication::processEvents();
    auto card = model.jobs().front().toMap();
    require(card.value("completed").toInt() == 1 && card.value("completedChapters").toInt() == 0
            && card.value("totalChapters").toInt() == 2, "partial chapter must not count as complete");
    model.startJob(id);
    model.pauseJob(id);
    require(model.running() && model.stopping() && starts == 1, "pause or duplicate click state incorrect");
    release.release(); waitUntilBatchStopped(model);
    auto paused = xuyan::application::ExtractionJobService(workspace.database()).load(job.id);
    require(paused.ok() && paused.value->completed_steps == 1 && model.jobs().front().toMap().value("paused").toBool(),
            "pause must preserve exactly the committed checkpoint");
    model.resumeJob(id); waitUntilBatchStopped(model);
    auto completed = xuyan::application::ExtractionJobService(workspace.database()).load(job.id);
    require(completed.ok() && completed.value->status == "completed"
            && completed.value->completed_steps == job.total_steps && starts == 2
            && completed.value->steps.front().attempt == 1, "resume must not rerun completed slices");
    require(model.jobs().front().toMap().value("completedChapters").toInt() == 2,
            "all chapters must finish after remaining slices commit");
}

/*
 * 功能：验证运行中可取消，随后点暂停不能把永久取消降级，已提交片段不删除。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：在首片检查点取消再暂停，检查永久取消不降级且保留已提交片，不能恢复发送。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testBatchCancel() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    const auto id = QString::fromStdString(job.id);
    QSemaphore checkpoint, release;
    std::atomic<int> starts{0};
    ExtractionJobViewModel model(workspace.database(), nullptr, gatedBatch(checkpoint, release, starts));
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    model.startJob(id); require(checkpoint.tryAcquire(1, 10000), "cancel checkpoint did not arrive");
    model.cancelJob(id, 0); model.pauseJob(id); release.release(); waitUntilBatchStopped(model);
    auto cancelled = xuyan::application::ExtractionJobService(workspace.database()).load(job.id);
    require(cancelled.ok() && cancelled.value->status == "cancelled" && cancelled.value->completed_steps == 1
            && cancelled.value->steps.front().status == "completed", "cancel must retain committed slice");
    model.resumeJob(id);
    require(!model.running() && starts == 1, "cancelled queue must not restart");
}

/*
 * 功能：验证后台解析期间切换世界不会污染新世界列表或偷偷开始另一个任务。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：后台首片阻塞时切世界，核对旧任务结果仍持久化但不填入新世界列表或启动另一任务。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testBatchWorldIsolation() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    QSemaphore checkpoint, release;
    std::atomic<int> starts{0};
    ExtractionJobViewModel model(workspace.database(), nullptr, gatedBatch(checkpoint, release, starts));
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    model.startJob(QString::fromStdString(job.id));
    require(checkpoint.tryAcquire(1, 10000), "world switch checkpoint did not arrive");
    model.setWorldId("world-b"); waitUntilIdle(model);
    model.startJob(QString::fromStdString(job.id));
    release.release(); waitUntilBatchStopped(model);
    require(model.jobs().isEmpty() && model.errorText().isEmpty() && starts == 1,
            "old batch callback must not populate new world");
    model.setWorldId("world-a"); waitUntilIdle(model);
    require(model.jobs().front().toMap().value("status").toString() == "completed", "old world result must remain persisted");
}

/*
 * 功能：验证未知异常隐藏私有内容并清理运行态，随后可以重新启动同一任务。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：首轮工作闭包抛合成私密标记异常，核对错误脱敏及运行态清理，重启后完成。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testBatchFailureRecovery() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    std::atomic<int> calls{0};
    /*
     * 功能：首次执行注入异常，重试改为正常离线批次。
     * 参数：path：输入，测试库路径；id：输入，任务标识；options：输入，离线选项；均只借用本次执行。
     * 返回：正常重试返回 processBatch 的 Result。
     * 失败：calls 原子递增前值为 0 时抛合成私密标记 runtime_error，其余异常传播。
     * 副作用：原子增加 calls，重试写离线检查点，不联网。
     * 线程与生命周期：模型工作线程执行；calls 生命周期覆盖模型销毁与线程 join。
     */
    ExtractionJobViewModel model(workspace.database(), nullptr, [&](const auto& path, const auto& id, const auto& options) {
        if (calls.fetch_add(1) == 0) throw std::runtime_error("private-worker-detail");
        return xuyan::application::MockExtractionProcessor(path).processBatch(id, options);
    });
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    model.startJob(QString::fromStdString(job.id)); waitUntilBatchStopped(model);
    require(!model.running() && !model.busy() && !model.errorText().isEmpty() && !model.errorText().contains("private-worker-detail"),
            "worker failure must sanitize details and clear running state");
    model.startJob(QString::fromStdString(job.id)); waitUntilBatchStopped(model);
    require(model.jobs().front().toMap().value("status").toString() == "completed", "worker failure must remain recoverable");
}

/*
 * 功能：验证视图模型销毁时请求停止并真正等待自有工作线程，不遗留后台执行者。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：关卡阻塞模型自有线程，销毁模型请求停止并解除关卡，核对退出已 join 且无新片。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testBatchDestructionJoinsWorker() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    QSemaphore started, release;
    std::atomic<bool> exited{false};
    auto model = std::make_unique<ExtractionJobViewModel>(workspace.database(), nullptr,
        /*
         * 功能：等待销毁停止请求后退出工作线程。
         * 参数：path：输入，测试库路径；id：输入，任务标识；options：输入，含模型 stop_token 的只读选项；引用仅本次执行有效。
         * 返回：停止令牌已请求时离线处理结果。
         * 失败：关卡 10000 毫秒未放行抛异常；处理器异常传播，exited 不会提前置 true。
         * 副作用：注册停止回调唤醒关卡，通知 started，等待后调用处理器并原子设置 exited。
         * 线程与生命周期：模型自有工作线程运行；捕获信号量/exited 在模型.reset 后仍存活，局部 stop_callback 在返回时注销。
         */
        [&](const auto& path, const auto& id, const xuyan::application::OfflineBatchOptions& options) {
            /*
             * 功能：停止请求唤醒析构等待的工作线程。
             * 参数：无。
             * 返回：无。
             * 失败：QSemaphore::release 无显式业务失败路径。
             * 副作用：为 release 增加一个许可。
             * 线程与生命周期：可能在主线程 request_stop 或已请求 token 的注册线程同步调用；回调注册期覆盖所借信号量。
             */
            std::stop_callback wake(options.stop_token, [&] { release.release(); });
            started.release();
            require(release.tryAcquire(1, 10000), "destructor must request stop");
            auto result = xuyan::application::MockExtractionProcessor(path).processBatch(id, options);
            exited = true;
            return result;
        });
    waitUntilIdle(*model); model->setWorldId("world-a"); waitUntilIdle(*model);
    model->startJob(QString::fromStdString(job.id));
    require(started.tryAcquire(1, 10000), "destructor worker did not start");
    model.reset();
    require(exited && xuyan::application::ExtractionJobService(workspace.database()).load(job.id).value->completed_steps == 0,
            "destructor must join before source lifetime ends without sending new slices");
}

/*
 * 功能：使用真实远程处理器和无网络传输替身，验证显式启动与在途暂停、恢复。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：临时库保存内存伪凭据，使用真实远程处理器和无网络传输替身，首请求关卡控制暂停/恢复。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testRemoteBatchControls() {
    using namespace xuyan::application;
    TemporaryWorkspace workspace;
    const auto offline = createBatchFixture(workspace.database());
    InMemoryCredentialStore credentials;
    xuyan::domain::ProviderConnection connection;
    connection.id = "vm-owned-connection"; connection.name = "临时模型连接";
    connection.kind = "deepseek"; connection.endpoint = "https://provider.example";
    connection.default_model = "owned-model";
    require(ProviderConnectionService(workspace.database(), credentials).save(
        "vm-provider", connection, 0, std::string{"vm-owned-credential"}).ok(), "remote fixture provider failed");
    auto job = ExtractionJobService(workspace.database()).create("vm-remote-job", offline.source_id, 1000, 0, 0, 2048, connection.id);
    require(job.ok(), "remote fixture job failed");
    QSemaphore sending, release;
    /*
     * 职责：以第一请求关卡和空候选合成响应模拟远程在途控制，拥有计数而不拥有网络。
     * 生命周期与线程：局部于当前用例，模型工作线程调用；两信号量由主线程持有，必须晚于模型销毁。
     */
    class Transport final : public IProviderTransport {
    public:
        /*
         * 功能：借用第一请求到达通知与放行关卡。
         * 参数：started：输入输出，到达信号量引用；gate：输入输出，放行信号量引用；均由用例持有。
         * 返回：初始化两个引用和从零开始的调用计数。
         * 失败：无显式校验或失败路径。
         * 副作用：只保存借用引用，不执行发送、不获取许可。
         * 线程与生命周期：主线程构造，信号量寿命须覆盖模型工作线程调用。
         */
        Transport(QSemaphore& started, QSemaphore& gate) : started_(started), gate_(gate) {}
        /*
         * 功能：模拟第一请求在途等待，随后返回空 v3 候选。
         * 参数：第一个未命名 ProviderHttpRequest 引用为请求，第二个未命名 string 引用为伪凭据，第三个未命名 int 为毫秒超时；均忽略，不保存。
         * 返回：固定成功 HTTP 200、四类空候选结构，无真实网络。
         * 失败：第一请求关卡 10000 毫秒未获许可时断言抛异常，响应构造/分配异常传播。
         * 副作用：原子递增 calls；首请求通知 started_ 并阻塞 gate_，取消不打断在途模拟响应。
         * 线程与生命周期：模型工作线程同步调用；两信号量借用至调用结束，主线程通过关卡控制。
         */
        xuyan::domain::Result<ProviderTransportResponse> send(const xuyan::providers::ProviderHttpRequest&,
            const std::string&, int) override {
            if (calls.fetch_add(1) == 0) {
                started_.release();
                require(gate_.tryAcquire(1, 10000), "remote send gate timed out");
            }
            return xuyan::domain::Result<ProviderTransportResponse>::success({200, false, false,
                R"({"status":"completed","output":[{"content":[{"type":"output_text","text":"{\"schema_version\":\"candidate-v3\",\"prompt_version\":\"extract-v3\",\"entities\":[],\"events\":[],\"relations\":[],\"rules\":[]}"}]}]})"});
        }
        /* 累计 send 次数，单位次，默认 0；工作线程原子增加，主线程读取断言。 */
        std::atomic<int> calls{0};
    private:
        /* 借用的首请求到达信号量，无默认值，由构造绑定；工作线程 release，主线程等待，不拥有。 */
        QSemaphore& started_;
        /* 借用的放行信号量，无默认值，由构造绑定；主线程 release，首请求等待，须覆盖模型寿命。 */
        QSemaphore& gate_;
    } transport(sending, release);
    /*
     * 功能：把界面离线批次接口转接到伪传输远程处理器。
     * 参数：path：输入，测试库路径；id：输入，任务标识；options：输入，片数上限、停止令牌和有效 on_progress，均只读借用。
     * 返回：转为 OfflineBatchResult 的 Result，远程失败原样传播；非完成/暂停停止归 needs_attention。
     * 失败：远程处理器、配置复制及进度回调异常向模型工作线程传播。
     * 副作用：用内存 credentials 和无网络 transport 处理远程队列，写预算/片段检查点，构造停止原因映射。
     * 线程与生命周期：模型工作线程同步执行；凭据/传输先于模型构造、晚于模型销毁；内层进度捕获不逃逸。
     */
    ExtractionJobViewModel model(workspace.database(), nullptr, [&](const auto& path, const auto& id, const OfflineBatchOptions& options) {
        RemoteBatchOptions remote;
        remote.maximum_steps = options.maximum_steps; remote.stop_token = options.stop_token;
        /*
         * 功能：将远程持久化进度转换为界面离线动作。
         * 参数：progress：输入，远程进度只读引用，计数按原值传递。
         * 返回：离线 cancel/pause 分别映射远程 cancel/pause，其余为 proceed。
         * 失败：原 options.on_progress 异常传播。
         * 副作用：调用原进度回调，不直接写库或接触响应正文。
         * 线程与生命周期：工作线程 processBatch 内同步调用，options 引用只在外层转接调用有效。
         */
        remote.on_progress = [&](const auto& progress) {
            const auto action = options.on_progress({progress.job_id, progress.total_steps,
                progress.completed_steps, progress.processed_steps, progress.revision});
            if (action == OfflineBatchAction::cancel) return RemoteBatchAction::cancel;
            return action == OfflineBatchAction::pause ? RemoteBatchAction::pause : RemoteBatchAction::proceed;
        };
        auto result = RemoteExtractionProcessor(path, credentials, transport).processBatch(id, remote);
        using Result = xuyan::domain::Result<OfflineBatchResult>;
        if (!result.ok()) return Result::failure(*result.error);
        const auto reason = result.value->reason == RemoteBatchStopReason::completed ? OfflineBatchStopReason::completed
            : result.value->reason == RemoteBatchStopReason::paused ? OfflineBatchStopReason::paused : OfflineBatchStopReason::needs_attention;
        return Result::success({std::move(result.value->job), result.value->processed_steps, reason});
    });
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    model.refresh(); waitUntilIdle(model);
    require(transport.calls == 0, "remote model must not send on construction or refresh");
    const auto id = QString::fromStdString(job.value->id);
    model.startJob(id); require(sending.tryAcquire(1, 10000), "explicit remote start did not send");
    model.pauseJob(id); release.release(); waitUntilBatchStopped(model);
    auto paused = ExtractionJobService(workspace.database()).load(job.value->id);
    require(paused.ok() && paused.value->completed_steps == 1 && transport.calls == 1,
            "in-flight pause must commit current slice and not send next");
    model.resumeJob(id); waitUntilBatchStopped(model);
    require(transport.calls == job.value->total_steps
            && ExtractionJobService(workspace.database()).load(job.value->id).value->status == "completed",
            "remote resume must send only remaining slices");
}

/*
 * 功能：后台已让出而结束通知仍排队时，取消不能被最终状态刷新吞掉。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：工作线程结束但 Qt 完成回调尚未处理时取消，核对持久化取消不被刷新吞掉。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testLateCheckpointCancellation() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    /*
     * 功能：单次只处理一个片段以制造结束回调排队窗口。
     * 参数：path：输入，测试库路径；id：输入，任务标识；options：输入，当前离线选项，只读借用。
     * 返回：processBatch 的 Result，最多处理一片。
     * 失败：选项复制或处理器异常传播到模型工作线程。
     * 副作用：复制选项并将 maximum_steps 设 1，不改变调用方 options；写最多一片检查点。
     * 线程与生命周期：无捕获，模型工作线程同步执行；选项副本覆盖处理期。
     */
    ExtractionJobViewModel model(workspace.database(), nullptr, [](const auto& path, const auto& id, const auto& options) {
        auto one_step = options;
        one_step.maximum_steps = 1;
        return xuyan::application::MockExtractionProcessor(path).processBatch(id, one_step);
    });
    waitUntilIdle(model); model.setWorldId("world-a"); waitUntilIdle(model);
    const auto id = QString::fromStdString(job.id);
    model.startJob(id);
    ExtractionJobViewModelTestAccess::joinWorker(model);
    require(model.running(), "queued completion callback must not have run during thread join");
    model.cancelJob(id, 0);
    waitUntilBatchStopped(model);
    auto cancelled = xuyan::application::ExtractionJobService(workspace.database()).load(job.id);
    require(cancelled.ok() && cancelled.value->status == "cancelled" && cancelled.value->completed_steps == 1,
            "late checkpoint cancellation must persist without losing committed output");
}

/*
 * 功能：明确取消后立即关闭视图模型，尚未处理的结束回调不应丢失取消意图。
 * 参数：无。
 * 返回：无；全部断言满足时正常返回。
 * 失败：断言、10000 毫秒等待超时或未预期的文件/仓储异常向入口传播；预期业务失败须按断言保持拒绝。
 * 副作用：工作线程结束后取消并立即销毁模型，核对取消落库及迟到回调安全。
 * 线程与生命周期：测试主线程调用，Qt 回调回主线程；后台由测试池或模型拥有，引用捕获须覆盖线程等待；失败路径不承诺恢复所有全局池设置。
 */
void testLateCancellationBeforeExit() {
    TemporaryWorkspace workspace;
    const auto job = createBatchFixture(workspace.database());
    auto model = std::make_unique<ExtractionJobViewModel>(workspace.database(), nullptr,
        /*
         * 功能：单片处理后保留排队结束通知供立即关闭回归。
         * 参数：path：输入，测试库路径；id：输入，任务标识；options：输入，当前离线选项，均只读借用。
         * 返回：processBatch 的 Result，片数上限固定一片。
         * 失败：选项复制或处理器异常传播到模型工作线程。
         * 副作用：只改选项副本上限、写一片检查点，不修改原选项。
         * 线程与生命周期：无捕获，工作线程执行，模型销毁时等待结束，不留下后台对象。
         */
        [](const auto& path, const auto& id, const auto& options) {
            auto one_step = options; one_step.maximum_steps = 1;
            return xuyan::application::MockExtractionProcessor(path).processBatch(id, one_step);
        });
    waitUntilIdle(*model); model->setWorldId("world-a"); waitUntilIdle(*model);
    const auto id = QString::fromStdString(job.id);
    model->startJob(id);
    ExtractionJobViewModelTestAccess::joinWorker(*model);
    model->cancelJob(id, 0);
    model.reset();
    auto cancelled = xuyan::application::ExtractionJobService(workspace.database()).load(job.id);
    require(cancelled.ok() && cancelled.value->status == "cancelled" && cancelled.value->completed_steps == 1,
            "closing immediately after late cancel must persist cancellation");
    QCoreApplication::processEvents();
}

/*
 * 功能：确认凭据补偿失败显示独立中文警示，不把底层详情或秘密直接送上界面。
 * 参数：无。
 * 返回：无；断言满足则正常结束。
 * 失败：错误分类映射丢失或泄露错误正文时由 require 抛异常。
 * 副作用：仅构造内存错误值，不访问凭据设施、数据库或网络。
 */
void testCredentialConsistencyDisplayText() {
    const xuyan::domain::Error error{xuyan::domain::ErrorCode::credential_consistency_failed,
        "private-error-detail", false, "private-action-detail"};
    const auto displayed = view_model_text::errorText(error);
    require(displayed.contains(QStringLiteral("人工核对"))
            && !displayed.contains(QStringLiteral("private-error-detail"))
            && !displayed.contains(QStringLiteral("private-action-detail")),
            "compensation failure must show a distinct safe Chinese instruction");
}

/*
 * 功能：核对全部服务错误分类和任务/候选状态的显示边界，防止非首页状态回显英文协议或外部详情。
 * 参数：无。
 * 返回：无；所有已知分类及未知回退均产生非空中文且不泄露测试详情时正常结束。
 * 失败：文案为空、含拉丁字母或状态误回显时由 require 抛异常；凭据专用警示另由相邻用例核对。
 * 副作用：仅构造内存错误和状态值、调用生产纯映射，不访问凭据、数据库、文件或网络。
 * 线程与生命周期：测试主线程同步调用，输入及输出均在本函数作用域拥有，不安排回调。
 */
void testAllDisplayMappings() {
    using xuyan::domain::ErrorCode;
    /* 稳定失败分类及一个未来分类探针，后者必须进入中文通用回退而非显示数字或内部名称。 */
    const ErrorCode codes[]{ErrorCode::validation_failed, ErrorCode::revision_conflict,
        ErrorCode::rule_conflict, ErrorCode::missing_context, ErrorCode::storage_error,
        ErrorCode::command_conflict, ErrorCode::credential_consistency_failed, static_cast<ErrorCode>(999)};
    /* 只识别产品自带标签中的拉丁文字；这里不处理、替换或禁止用户提供的名称与真实模型标识。 */
    const QRegularExpression latin(QStringLiteral("[A-Za-z]"));
    for (const auto code : codes) {
        const xuyan::domain::Error input{code, "private-display-detail", false, "private-display-action"};
        const auto text = view_model_text::errorText(input);
        require(!text.isEmpty() && !text.contains(latin)
                && !text.contains(QStringLiteral("private-display-detail"))
                && !text.contains(QStringLiteral("private-display-action")),
                "服务错误必须显示中文安全说明，不回显英文诊断或外部建议");
    }
    /* 所有当前映射状态以及空值/未来状态，只属于测试输入，不创建任何产品任务或候选。 */
    const std::string states[]{"queued", "ready", "running", "completed", "failed", "unknown",
        "cancelled", "cancelling", "candidate", "accepted", "rejected", "conflicted", "", "future-test-state"};
    for (const auto& state : states) {
        const auto text = view_model_text::stateLabel(state);
        require(!text.isEmpty() && !text.contains(latin), "任务和候选状态必须显示中文，不回显内部协议");
    }
    require(view_model_text::stateLabel("") == QStringLiteral("未知状态")
            && view_model_text::stateLabel("future-test-state") == QStringLiteral("未知状态"),
            "空值和未来状态必须使用中文未知回退");
}

/* 职责：用共享信号量占用全局线程池唯一线程，确定性检验排队期间切世界和销毁边界。
 * 生命周期：主线程作用域守卫，失败路径仍放行并恢复线程数；信号量由worker与守卫共同持有，不借用栈。
 * 边界：仅测试使用；析构等待最多10000毫秒，超时不销毁worker仍持有的信号量，不操作用户资料。
 */
class PackageWorkerGate final {
public:
    /* 功能：等待已有测试池操作后建立唯一线程门闩，不靠休眠制造竞态。
     * 参数：无。返回：已确认占用池线程的守卫。
     * 失败：已有工作或占用确认超过10000毫秒抛中文异常，建门闩失败先放行并恢复池设置。
     * 副作用：临时限制全局池并排队一个只等信号量的任务；仅主线程构造。 */
    PackageWorkerGate() : pool_(QThreadPool::globalInstance()), previous_limit_(pool_->maxThreadCount()),
                          signals_(std::make_shared<Signals>()) {
        require(pool_->waitForDone(10000), "包测试开始前线程池必须空闲");
        pool_->setMaxThreadCount(1);
        /* 功能：占用唯一工作线程并通知测试可以排队实际导出。
         * 参数：无，gate_state按值共享持有两个信号量。返回：无。
         * 失败：不设主动错误，守卫失败/析构也会放行。
         * 副作用：仅释放started并等待release；信号量覆盖整个后台等待，不读写资料。 */
        pool_->start([gate_state = signals_] { gate_state->started.release(); gate_state->release.acquire(); });
        if (!signals_->started.tryAcquire(1, 10000)) {
            signals_->release.release(); pool_->setMaxThreadCount(previous_limit_);
            throw std::runtime_error("包测试线程门闩启动超时");
        }
    }
    /* 功能：失败或正常离开时放行所有已排队工作并恢复池并发限制。
     * 参数：无。返回：无。失败：等待超时不抛析构异常；共享信号量仍由worker持有。
     * 副作用：放行门闩，最多等待10000毫秒后恢复全局池；测试目录应晚于后台工作释放。 */
    ~PackageWorkerGate() { release(); pool_->waitForDone(10000); pool_->setMaxThreadCount(previous_limit_); }
    /* 功能：禁止复制全局池设置恢复责任。参数：另一守卫。返回：无；编译期拒绝，无副作用。 */
    PackageWorkerGate(const PackageWorkerGate&) = delete;
    /* 功能：禁止覆盖恢复责任。参数：另一守卫。返回：无；编译期拒绝，无副作用。 */
    PackageWorkerGate& operator=(const PackageWorkerGate&) = delete;
    /* 功能：显式允许排队的导出执行，重复调用无额外许可。
     * 参数：无。返回：无。失败：无主动业务错误。
     * 副作用：首次调用释放一个许可并置released_，主线程同步执行。 */
    void release() { if (!released_) { released_ = true; signals_->release.release(); } }
private:
    /* 职责：持有线程启动/放行两个计数信号量；默认0许可，仅门闩worker和守卫共享，不含业务数据。 */
    struct Signals {
        /* worker占用线程后释放，主线程消费，初始0许可，不用于业务进度。 */
        QSemaphore started;
        /* 主线程显式/析构释放，worker消费，初始0许可，保证失败路径也能退出。 */
        QSemaphore release;
    };
    /* Qt持有的全局池观察指针，构造取得，守卫只临时改并发限制、不销毁池。 */
    QThreadPool* pool_;
    /* 构造前并发上限，单位线程，构造取得、析构恢复。 */
    int previous_limit_;
    /* worker共享拥有的门闩信号量，构造创建，最后一个持有者结束后释放。 */
    std::shared_ptr<Signals> signals_;
    /* 是否已发放一次放行许可，默认false，主线程release更新，析构据此避免重复释放。 */
    bool released_{false};
};

/* 功能：读取测试导出包的世界身份，确认模型传递的是稳定标识而非同名目录首项。
 * 参数：path为借用的测试自有包路径，必须已经完成写入。
 * 返回：world.json中世界标识的拥有型字符串。
 * 失败：包/JSON/字段缺失或不是字符串抛中文异常。
 * 副作用：仅读取独占测试包，不读用户工作区或网络；调用线程同步执行。 */
std::string exportedWorldIdentity(const std::filesystem::path& path) {
    const auto archive = xuyan::package::readZip(path);
    require(archive.ok(), "包视图模型输出必须可读取");
    for (const auto& entry : *archive.value) {
        if (entry.path != "world.json") continue;
        const auto world = xuyan::package::parseJson(entry.data);
        require(world.ok(), "包世界元数据必须可解析");
        const auto* id = world.value->find("world_id");
        require(id && id->isString(), "包世界身份必须存在");
        return id->string();
    }
    throw std::runtime_error("包世界元数据缺失");
}

/* 功能：验证包模型的明确世界选择、排队切换隔离、未知世界错误隔离及销毁后的文件语义。
 * 参数：无。返回：无；所有输入/身份/忙碌/迟到回调断言成立则正常结束。
 * 失败：仓储、导出、事件循环、门闩超时或断言失败抛中文异常。
 * 副作用：独占临时库显式创建两个同名世界，真实写测试ZIP；不启动窗口、读用户资料或联网。
 * 线程与生命周期：测试主线程操作模型，门闩控制线程池；测试目录最后析构，销毁不撤销已排队写入。 */
void testPackageWorldSelection() {
    TemporaryWorkspace temporary;
    xuyan::storage::WorkspaceRepository repository(temporary.database());
    require(repository.createWorldTemplate("a-export-world", "同名世界").ok(), "包测试须创建首世界");
    require(repository.createWorldTemplate("z-export-world", "同名世界").ok(), "包测试须创建选定世界");
    PackageViewModel model(temporary.database());
    require(QThreadPool::globalInstance()->waitForDone(10000), "分支目录构造查询须先结束");
    QCoreApplication::processEvents();
    const auto target = temporary.database().parent_path() / "selected.zip";
    const auto target_url = QUrl::fromLocalFile(QString::fromStdWString(target.wstring()));
    require(!model.destinationExists(target_url), "导出前本地目标不存在");
    model.exportWorld(target_url);
    require(!model.busy() && !model.errorText().isEmpty() && !std::filesystem::exists(target), "未选择世界必须显示中文错误且不写文件");
    model.setWorldId(QStringLiteral("z-export-world"));
    require(model.errorText().isEmpty(), "新世界选择必须清除旧错误");
    model.exportWorld(QUrl(QStringLiteral("https://example.test/not-local.zip")));
    require(!model.busy() && !model.errorText().isEmpty(), "远程输出URL须明确拒绝");
    {
        PackageWorkerGate gate;
        model.exportWorld(target_url);
        require(model.busy(), "明确点击导出必须排队操作");
        model.setWorldId(QStringLiteral("a-export-world"));
        model.exportWorld(QUrl::fromLocalFile(QString::fromStdWString((target.parent_path() / "duplicate.zip").wstring())));
        require(model.busy(), "切世界不能提前释放忙碌状态并允许重复写入");
        gate.release(); waitUntilIdle(model);
        require(model.errorText().isEmpty() && !model.statusText().contains(QStringLiteral("已导出")), "旧世界完成回调不能覆盖新世界说明");
    }
    require(exportedWorldIdentity(target) == "z-export-world", "在途导出必须保留开始时的世界身份");
    require(!std::filesystem::exists(target.parent_path() / "duplicate.zip"), "忙碌期间第二次导出不得排队");
    require(model.destinationExists(target_url), "真实写入后覆盖查询必须识别目标");
    model.exportWorld(target_url); waitUntilIdle(model);
    require(model.errorText().isEmpty() && model.statusText().contains(QStringLiteral("已导出"))
            && exportedWorldIdentity(target) == "a-export-world", "后续导出必须使用新的明确世界");
    {
        PackageWorkerGate gate;
        model.setWorldId(QStringLiteral("missing-export-world")); model.exportWorld(target_url);
        model.setWorldId(QStringLiteral("a-export-world"));
        gate.release(); waitUntilIdle(model);
        require(model.errorText().isEmpty() && exportedWorldIdentity(target) == "a-export-world", "迟到失败不能污染新选择且不得覆盖输出");
    }
    {
        PackageWorkerGate gate;
        model.exportWorld(target_url);
        model.setWorldId(QStringLiteral("z-export-world")); model.setWorldId(QStringLiteral("a-export-world"));
        gate.release(); waitUntilIdle(model);
        require(!model.statusText().contains(QStringLiteral("已导出")), "同标识返回也须拒绝旧代次完成通知");
    }
    const auto destroyed_target = target.parent_path() / "destroyed.zip";
    {
        auto destroyed_model = std::make_unique<PackageViewModel>(temporary.database());
        require(QThreadPool::globalInstance()->waitForDone(10000), "销毁回归前目录查询须结束");
        QCoreApplication::processEvents();
        PackageWorkerGate gate;
        destroyed_model->setWorldId(QStringLiteral("z-export-world"));
        destroyed_model->exportWorld(QUrl::fromLocalFile(QString::fromStdWString(destroyed_target.wstring())));
        destroyed_model.reset();
        gate.release();
        require(QThreadPool::globalInstance()->waitForDone(10000), "销毁后的已排队文件用例须安全结束");
        QCoreApplication::processEvents();
    }
    require(exportedWorldIdentity(destroyed_target) == "z-export-world", "模型销毁不撤销用户已经明确开始的文件写入");
}

} // namespace

/*
 * 功能：运行世界视图模型的异步选择与回调隔离回归。
 * 参数：argc：输入输出，命令行参数数目，Qt 可修改；argv：输入输出，命令行字符指针数组，由调用环境提供并须覆盖应用寿命。
 * 返回：所有所执行断言满足返回 0；捕获 std::exception 打印失败后返回 1。
 * 失败：用例异常进入入口 catch，非标准异常不捕获。
 * 副作用：建立 Qt Core 应用、独占临时库并运行异步回归，打印结果；使用伪传输，不访问付费网络。
 * 线程与生命周期：入口主线程运行事件循环等待，测试模型负责线程退出，目录晚于模型释放。
 */
int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    try {
        TemporaryWorkspace workspace;
        seedWorlds(workspace.database());
        seedCandidates(workspace.database());
        testCredentialConsistencyDisplayText();
        testAllDisplayMappings();
        testPackageWorldSelection();
        testGraphSemanticMapping();
        testSelectionAndIsolation(workspace.database());
        testLateCallbackIsolation(workspace.database());
        testWorldRefreshFailureAndRetry();
        testCandidatePaging(workspace.database());
        testCandidateLateCallback(workspace.database());
        testCandidateFilterDuringRead(workspace.database());
        testSourceSelection(workspace.database());
        testSourceLateCallback(workspace.database());
        testSourceFailureAndRetry();
        testLongChapterWindows();
        testJobSelection(workspace.database());
        testJobLateCallback(workspace.database());
        testBatchPauseAndResume();
        testBatchCancel();
        testBatchWorldIsolation();
        testBatchFailureRecovery();
        testBatchDestructionJoinsWorker();
        testRemoteBatchControls();
        testLateCheckpointCancellation();
        testLateCancellationBeforeExit();
        {
            TemporaryWorkspace empty_page_workspace;
            seedWorlds(empty_page_workspace.database());
            seedCandidates(empty_page_workspace.database());
            testCandidateEmptyLastPage(empty_page_workspace.database());
        }
        std::cout << "世界、候选、来源与任务视图模型测试通过\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
