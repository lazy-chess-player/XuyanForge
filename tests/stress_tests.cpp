#include "xuyan/storage/workspace_repository.h"
#include "xuyan/domain/hash.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/*
 * 功能：把压力断言失败交给入口统一报告。
 * 参数：condition：输入，true 为通过；message：输入，失败场景说明，可为空，只读引用在调用期间借用。
 * 返回：无。
 * 失败：condition 为 false 抛带 message 的 runtime_error，异常分配失败传播。
 * 副作用：不打印，不改变被测数据。
 * 线程与生命周期：调用线程同步执行，不保留引用。
 */
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/*
 * 功能：移除当前压力测试拥有的数据库及旁路文件。
 * 参数：path：输入，自有临时数据库路径，只读借用；函数不校验归属，禁止用户工作区。
 * 返回：无。
 * 失败：remove 的错误由 error_code 忽略；路径或字符串构造异常可传播。
 * 副作用：分别删除精确文件及 -wal/-shm，不删除父目录。
 * 线程与生命周期：同步执行，调用前必须释放连接且不能有其他进程使用路径。
 */
void removeDatabase(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
}

/*
 * 职责：作用域持有压力测试数据库清理责任，不拥有活动 SQLite 连接。
 * 生命周期与线程：调用线程最后销毁；仅当前用例使用，不复制到其他清理责任对象。
 */
struct DatabaseCleanup final {
    /* 压力测试拥有的精确数据库路径，无默认值，初始化时写入、析构只读，不接管父目录。 */
    std::filesystem::path path;
    /*
     * 功能：在测试仓储销毁后清理数据库及旁路文件。
     * 参数：无。
     * 返回：完成清理尝试及守卫销毁。
     * 失败：文件删除错误忽略；removeDatabase 的路径分配异常未捕获，在隐式 noexcept 析构中可导致终止。
     * 副作用：只删除 path 及 -wal/-shm，不删除目录，不清理其他素材。
     * 线程与生命周期：调用线程同步析构，关联连接须先关闭。
     */
    ~DatabaseCleanup() { removeDatabase(path); }
};

/*
 * 功能：为已初始化的压力测试库生成一万候选，其中末条替换为 v3 身份候选，另生成一个异世界候选。
 * 参数：path：输入，测试独占临时 SQLite 路径，须已存在所需业务表；只借用本次调用。
 * 返回：无。
 * 失败：连接、SQL 执行、准备或替换行数不符时抛 runtime_error；绑定返回值未单独检查。
 * 副作用：事务插入两世界及候选，提交后单独更新末条；更新失败不会回滚已提交夹具，不联网。
 * 线程与生命周期：同步创建独立连接和语句，由 unique_ptr 释放；不接管文件所有权。
 */
void seedCandidateLoad(const std::filesystem::path& path) {
    sqlite3* opened = nullptr;
    require(sqlite3_open(path.string().c_str(), &opened) == SQLITE_OK && opened != nullptr,
            "stress candidate database must open");
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(opened, &sqlite3_close);
    constexpr auto fixture = R"SQL(
PRAGMA foreign_keys=ON;
BEGIN;
INSERT OR IGNORE INTO world_template(id,name,source_id,created_at)
VALUES('world-stress','压力世界','','2026-09-27'),('world-other','其他世界','','2026-09-27');
INSERT INTO source_document VALUES('stress-source','world-stress','压力来源','sha-stress','','','1','2026-09-27');
INSERT INTO source_document VALUES('other-source','world-other','其他来源','sha-other','','','1','2026-09-27');
INSERT INTO extraction_job(id,source_id,status,schema_version,prompt_version,provider_connection_id,model_id,
    total_steps,completed_steps,cancel_requested,revision,created_at,updated_at)
VALUES('stress-job','stress-source','completed','candidate-v1','extract-v1','','',10000,10000,0,1,'2026-09-27','2026-09-27'),
      ('other-job','other-source','completed','candidate-v1','extract-v1','','',1,1,0,1,'2026-09-27','2026-09-27');
WITH RECURSIVE numbers(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM numbers WHERE n<10000)
INSERT INTO extraction_candidate(id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,
    start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,
    prompt_version,revision,created_at,updated_at)
SELECT printf('stress-candidate-%05d',n),'stress-job',n,'stress-source','entity',
       printf('候选 %05d',n),'{}',n,n+1,'证据','fixture-hash','original_fact',
       'candidate','candidate-v1','extract-v1',1,'2026-09-27','2026-09-27' FROM numbers;
INSERT INTO extraction_candidate(id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,
    start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,
    prompt_version,revision,created_at,updated_at)
VALUES('other-candidate','other-job',1,'other-source','entity','其他候选','{}',1,2,
       '证据','fixture-hash','original_fact','candidate','candidate-v1','extract-v1',1,'2026-09-27','2026-09-27');
COMMIT;
)SQL";
    const auto status = sqlite3_exec(database.get(), fixture, nullptr, nullptr, nullptr);
    if (status != SQLITE_OK) throw std::runtime_error(std::string{"candidate stress fixture failed: "}
        + sqlite3_errmsg(database.get()));
    const std::string quote = "长篇条目9999又称别名9999。";
    sqlite3_stmt* raw_update = nullptr;
    require(sqlite3_prepare_v2(database.get(),
        "UPDATE extraction_candidate SET name=?,fields_json=?,quote=?,quote_hash=?,"
        "schema_version='candidate-v3',prompt_version='extract-v3' WHERE id='stress-candidate-10000'",
        -1, &raw_update, nullptr) == SQLITE_OK, "typed stress candidate update must prepare");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> update(raw_update, &sqlite3_finalize);
    const std::string fields = R"({"kind":"character","aliases":["别名9999"]})";
    const auto quote_hash = xuyan::domain::sha256(quote);
    sqlite3_bind_text(update.get(), 1, "长篇条目9999", -1, SQLITE_STATIC);
    sqlite3_bind_text(update.get(), 2, fields.data(), static_cast<int>(fields.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(update.get(), 3, quote.data(), static_cast<int>(quote.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(update.get(), 4, quote_hash.data(), static_cast<int>(quote_hash.size()), SQLITE_TRANSIENT);
    require(sqlite3_step(update.get()) == SQLITE_DONE && sqlite3_changes(database.get()) == 1,
            "typed stress candidate must replace one synthetic row");
}

} // namespace

/*
 * 功能：显式生成一万实体及候选，验证首/中/末页、世界隔离、精确身份建议和反复重开成本。
 * 参数：无。
 * 返回：断言全部满足返回 0；捕获 std::exception 后报告并返回 1。
 * 失败：断言不满足抛异常；30 秒限值按实际本机耗时检查，不证明所有平台性能；非标准异常不捕获。
 * 副作用：重建系统临时 xuyanforge-tests/stress.sqlite，导入合成数据并打印统计，守卫退出尝试删除数据库。
 * 线程与生命周期：主线程同步执行；固定文件名需由本压力进程独占，仓储先于清理守卫销毁。
 */
int main() {
    try {
        const auto path = std::filesystem::temp_directory_path() / "xuyanforge-tests" / "stress.sqlite";
        std::filesystem::create_directories(path.parent_path()); removeDatabase(path);
        DatabaseCleanup cleanup{path};
        const auto started = std::chrono::steady_clock::now();
        xuyan::storage::WorkspaceRepository repository(path);
        std::vector<xuyan::domain::WorldEntity> entities;
        entities.reserve(10000);
        for (int index = 0; index < 10000; ++index) {
            xuyan::domain::WorldEntity entity;
            entity.id = "stress-entity-" + std::to_string(index);
            entity.world_id = "world-stress"; entity.kind = index % 2 == 0 ? "event" : "character";
            entity.name = "长篇条目" + std::to_string(index);
            entity.description = std::string(256, static_cast<char>('a' + index % 26));
            entity.aliases = {"别名" + std::to_string(index)}; entity.tags = {"压力回归", "中文"};
            entity.revision = 1;
            entities.push_back(std::move(entity));
        }
        auto imported = repository.importEntities("stress-import", "stress-package-hash", std::move(entities));
        require(imported.ok() && *imported.value == 10000, "ten thousand entities must import in one transaction");
        auto first = repository.searchEntities("长篇条目", "", 0, 25);
        auto middle = repository.searchEntities("长篇条目", "", 4975, 25);
        auto last = repository.searchEntities("长篇条目", "", 9975, 25);
        require(first.ok() && middle.ok() && last.ok() && first.value->total == 10000
                    && first.value->items.size() == 25 && middle.value->items.size() == 25 && last.value->items.size() == 25,
                "large-world search must stay paged instead of loading the full result set");
        seedCandidateLoad(path);
        const auto first_candidates = repository.listExtractionCandidatesPage("world-stress", "", "candidate", 100, 0);
        const auto middle_candidates = repository.listExtractionCandidatesPage("world-stress", "", "candidate", 100, 5000);
        const auto last_candidates = repository.listExtractionCandidatesPage("world-stress", "", "candidate", 100, 9900);
        const auto other_candidates = repository.listExtractionCandidatesPage("world-other", "", "candidate", 100, 0);
        require(first_candidates.ok() && middle_candidates.ok() && last_candidates.ok() && other_candidates.ok()
                    && first_candidates.value->total == 10000 && middle_candidates.value->total == 10000
                    && last_candidates.value->total == 10000 && other_candidates.value->total == 1
                    && first_candidates.value->items.size() == 100 && middle_candidates.value->items.size() == 100
                    && last_candidates.value->items.size() == 100
                    && first_candidates.value->items.front().id == "stress-candidate-00001"
                    && middle_candidates.value->items.front().id == "stress-candidate-05001"
                    && last_candidates.value->items.back().id == "stress-candidate-10000"
                    && other_candidates.value->items.front().id == "other-candidate",
                "10k candidate pagination must bound every page and isolate worlds");
        const auto identity_matches = repository.matchCandidateEntities("stress-candidate-10000", 1, 25, 0);
        require(identity_matches.ok() && identity_matches.value->total == 1
                    && identity_matches.value->items.size() == 1
                    && identity_matches.value->items.front().entity_id == "stress-entity-9999"
                    && identity_matches.value->items.front().name_match
                    && identity_matches.value->items.front().alias_match,
                "candidate identity matching must stay exact and bounded across ten thousand current entities");
        const auto reopen_started = std::chrono::steady_clock::now();
        for (int repeat = 0; repeat < 10; ++repeat) {
            // 模拟界面每次翻页重新打开工作区，检查连接初始化不会主导分页成本。
            xuyan::storage::WorkspaceRepository reopened(path);
            const auto page = reopened.listExtractionCandidatesPage("world-stress", "", "candidate", 100, repeat * 100);
            require(page.ok() && page.value->total == 10000 && page.value->items.size() == 100,
                    "reopened workspace must keep candidate pages consistent");
        }
        const auto reopen_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - reopen_started).count();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        require(elapsed < 30, "local 10k-entity import and three paged searches exceeded 30 seconds");
        std::cout << "叙演工坊一万条目压力回归通过，耗时 " << elapsed
                  << " 秒；候选分页重开 10 次耗时 " << reopen_milliseconds << " 毫秒。\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "压力测试失败： " << exception.what() << '\n';
        return 1;
    }
}
