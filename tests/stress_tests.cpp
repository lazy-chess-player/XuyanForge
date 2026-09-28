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

/** @brief 将压力回归的失败条件转换为可报告的异常。 */
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/** @brief 移除本次压力测试的数据库及 SQLite 旁路文件。 */
void removeDatabase(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
}

/** @brief 在仓储连接关闭后清理本次压力测试的数据库。 */
struct DatabaseCleanup final {
    std::filesystem::path path;
    /** @brief 仅移除压力测试明确使用的数据库文件。 */
    ~DatabaseCleanup() { removeDatabase(path); }
};

/** @brief 只在压力测试数据库中生成一万条候选、一个类型化身份候选和一个异世界候选。 */
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

/** @brief 导入一万条合成实体并验证分页检索及候选身份建议仍保持精确有界。 */
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
        std::cout << "XuyanForge 10k-entity stress smoke passed in " << elapsed
                  << " s; 10 candidate-page reopen cycles: " << reopen_milliseconds << " ms.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Stress test failure: " << exception.what() << '\n';
        return 1;
    }
}
