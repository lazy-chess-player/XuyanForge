#include "xuyan/storage/workspace_repository.h"
#include "xuyan/domain/hash.h"

#include <sqlite3.h>

#include <chrono>
#include <algorithm>
#include <iomanip>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace xuyan::storage {
namespace {

using xuyan::domain::BranchInfo;
using xuyan::domain::CharacterState;
using xuyan::domain::CommitView;
using xuyan::domain::Error;
using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::domain::ScenarioState;
using xuyan::domain::WorldEntity;
using xuyan::domain::EntityPage;
using xuyan::domain::SourceDocument;
using xuyan::domain::SourceChapter;
using xuyan::domain::CharacterBlueprint;
using xuyan::domain::ProviderConnection;
using xuyan::domain::EvidenceReference;

class Statement {
public:
    Statement(sqlite3* database, const char* sql) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(database));
        }
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    sqlite3_stmt* get() const noexcept { return statement_; }

private:
    sqlite3_stmt* statement_{nullptr};
};

std::shared_ptr<std::mutex> writeMutexFor(sqlite3* database) {
    static std::mutex registry_mutex;
    static std::unordered_map<std::string, std::weak_ptr<std::mutex>> registry;
    const auto* filename = sqlite3_db_filename(database, "main");
    const std::string key = filename == nullptr ? std::string{"<memory>"} : std::filesystem::weakly_canonical(filename).string();
    std::lock_guard lock(registry_mutex);
    auto& entry = registry[key];
    auto shared = entry.lock();
    if (!shared) { shared = std::make_shared<std::mutex>(); entry = shared; }
    return shared;
}

class Transaction {
public:
    explicit Transaction(sqlite3* database)
        : database_(database), write_mutex_(writeMutexFor(database)), write_lock_(*write_mutex_) {
        char* message = nullptr;
        if (sqlite3_exec(database_, "BEGIN IMMEDIATE", nullptr, nullptr, &message) != SQLITE_OK) {
            std::string detail = message == nullptr ? "cannot begin transaction" : message;
            sqlite3_free(message);
            throw std::runtime_error(detail);
        }
    }
    ~Transaction() {
        if (!committed_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(database_));
        }
        committed_ = true;
    }

private:
    sqlite3* database_;
    std::shared_ptr<std::mutex> write_mutex_;
    std::unique_lock<std::mutex> write_lock_;
    bool committed_{false};
};

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("cannot bind SQLite text value");
    }
}

std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
}

std::string randomId(std::string_view prefix) {
    static thread_local std::mt19937_64 generator{std::random_device{}()};
    std::ostringstream out;
    out << prefix << '-' << std::hex << std::setfill('0') << std::setw(16) << generator();
    return out.str();
}

std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm value{};
#ifdef _WIN32
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    std::ostringstream out;
    out << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

Error storageError(const std::exception& exception) {
    return Error{ErrorCode::storage_error, exception.what(), true, "检查工作区路径和磁盘空间后重试"};
}

std::string stateJson(const ScenarioState& state) {
    std::ostringstream out;
    out << "{\"revision\":" << state.revision << ",\"turn\":" << state.turn
        << ",\"elapsed_ticks\":" << state.elapsed_ticks << ",\"seal_holder_id\":\""
        << state.seal_holder_id << "\",\"seal_inspected\":" << (state.seal_inspected ? "true" : "false")
        << ",\"paused\":" << (state.paused ? "true" : "false")
        << ",\"completed\":" << (state.completed ? "true" : "false") << '}';
    return out.str();
}

CommitView readCommit(sqlite3* database, const std::string& commit_id) {
    Statement query(database,
        "SELECT s.branch_id,s.commit_id,s.parent_commit_id,s.state_hash,s.revision,s.turn,"
        "s.elapsed_ticks,s.seal_holder_id,s.seal_inspected,s.paused,s.completed,s.narration,"
        "s.xu_gate,s.xu_forgery,s.xu_trust,s.shen_gate,s.shen_forgery,s.shen_trust "
        "FROM state_snapshot s WHERE s.commit_id=?");
    bindText(query.get(), 1, commit_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) {
        throw std::runtime_error("找不到提交：" + commit_id);
    }
    CommitView view;
    view.branch_id = columnText(query.get(), 0);
    view.commit_id = columnText(query.get(), 1);
    view.parent_commit_id = columnText(query.get(), 2);
    view.state_hash = columnText(query.get(), 3);
    auto& state = view.state;
    state.revision = sqlite3_column_int(query.get(), 4);
    state.turn = sqlite3_column_int(query.get(), 5);
    state.elapsed_ticks = sqlite3_column_int(query.get(), 6);
    state.seal_holder_id = columnText(query.get(), 7);
    state.seal_inspected = sqlite3_column_int(query.get(), 8) != 0;
    state.paused = sqlite3_column_int(query.get(), 9) != 0;
    state.completed = sqlite3_column_int(query.get(), 10) != 0;
    state.narration = columnText(query.get(), 11);
    state.characters = {
        CharacterState{"actor-xucheng", "许澄", sqlite3_column_int(query.get(), 12) != 0,
                       sqlite3_column_int(query.get(), 13) != 0, sqlite3_column_int(query.get(), 14)},
        CharacterState{"actor-shentang", "沈棠", sqlite3_column_int(query.get(), 15) != 0,
                       sqlite3_column_int(query.get(), 16) != 0, sqlite3_column_int(query.get(), 17)},
    };
    return view;
}

void insertSnapshot(sqlite3* database, const CommitView& view) {
    const auto* xu = xuyan::domain::findCharacter(view.state, "actor-xucheng");
    const auto* shen = xuyan::domain::findCharacter(view.state, "actor-shentang");
    if (xu == nullptr || shen == nullptr) {
        throw std::runtime_error("灰港状态缺少必要人物");
    }
    Statement insert(database,
        "INSERT INTO state_snapshot(branch_id,commit_id,parent_commit_id,state_hash,state_json,revision,turn,"
        "elapsed_ticks,seal_holder_id,seal_inspected,paused,completed,narration,xu_gate,xu_forgery,xu_trust,"
        "shen_gate,shen_forgery,shen_trust,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    int index = 1;
    bindText(insert.get(), index++, view.branch_id);
    bindText(insert.get(), index++, view.commit_id);
    bindText(insert.get(), index++, view.parent_commit_id);
    bindText(insert.get(), index++, view.state_hash);
    bindText(insert.get(), index++, stateJson(view.state));
    sqlite3_bind_int(insert.get(), index++, view.state.revision);
    sqlite3_bind_int(insert.get(), index++, view.state.turn);
    sqlite3_bind_int(insert.get(), index++, view.state.elapsed_ticks);
    bindText(insert.get(), index++, view.state.seal_holder_id);
    sqlite3_bind_int(insert.get(), index++, view.state.seal_inspected ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, view.state.paused ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, view.state.completed ? 1 : 0);
    bindText(insert.get(), index++, view.state.narration);
    sqlite3_bind_int(insert.get(), index++, xu->knows_gate_closure ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, xu->knows_seal_forgery ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, xu->trust);
    sqlite3_bind_int(insert.get(), index++, shen->knows_gate_closure ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, shen->knows_seal_forgery ? 1 : 0);
    sqlite3_bind_int(insert.get(), index++, shen->trust);
    bindText(insert.get(), index++, utcNow());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

std::optional<std::pair<std::string, std::string>> commandResult(sqlite3* database, const std::string& command_id) {
    Statement query(database, "SELECT payload_hash,result_commit_id FROM command_log WHERE command_id=?");
    bindText(query.get(), 1, command_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return std::make_pair(columnText(query.get(), 0), columnText(query.get(), 1));
}

void recordCommand(sqlite3* database, const std::string& command_id, const std::string& payload_hash,
                   const std::string& commit_id) {
    Statement insert(database,
        "INSERT INTO command_log(command_id,payload_hash,result_commit_id,created_at) VALUES(?,?,?,?)");
    bindText(insert.get(), 1, command_id);
    bindText(insert.get(), 2, payload_hash);
    bindText(insert.get(), 3, commit_id);
    bindText(insert.get(), 4, utcNow());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

std::string joinValues(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result.push_back('\x1f');
        result += value;
    }
    return result;
}

std::vector<std::string> splitValues(std::string_view value) {
    std::vector<std::string> values;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('\x1f', begin);
        values.emplace_back(value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return values;
}

std::string entityPayload(const WorldEntity& entity, int expected_revision, std::string_view operation) {
    std::ostringstream out;
    out << operation << '|' << expected_revision << '|' << entity.id << '|' << entity.world_id << '|'
        << entity.kind << '|' << entity.name << '|' << joinValues(entity.aliases) << '|'
        << joinValues(entity.tags) << '|' << entity.description << '|' << entity.attributes_json << '|'
        << entity.review_status << '|' << entity.deleted;
    return out.str();
}

WorldEntity readEntityRevision(sqlite3* database, const std::string& entity_id, int revision = -1) {
    const char* sql = revision < 0
        ? "SELECT e.id,e.world_id,r.kind,r.name,r.aliases,r.tags,r.description,r.attributes_json,"
          "r.review_status,r.revision,r.deleted FROM world_entity e JOIN entity_revision r "
          "ON r.entity_id=e.id AND r.revision=e.head_revision WHERE e.id=?"
        : "SELECT e.id,e.world_id,r.kind,r.name,r.aliases,r.tags,r.description,r.attributes_json,"
          "r.review_status,r.revision,r.deleted FROM world_entity e JOIN entity_revision r "
          "ON r.entity_id=e.id WHERE e.id=? AND r.revision=?";
    Statement query(database, sql);
    bindText(query.get(), 1, entity_id);
    if (revision >= 0) sqlite3_bind_int(query.get(), 2, revision);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到世界条目：" + entity_id);
    WorldEntity entity;
    entity.id = columnText(query.get(), 0);
    entity.world_id = columnText(query.get(), 1);
    entity.kind = columnText(query.get(), 2);
    entity.name = columnText(query.get(), 3);
    entity.aliases = splitValues(columnText(query.get(), 4));
    entity.tags = splitValues(columnText(query.get(), 5));
    entity.description = columnText(query.get(), 6);
    entity.attributes_json = columnText(query.get(), 7);
    entity.review_status = columnText(query.get(), 8);
    entity.revision = sqlite3_column_int(query.get(), 9);
    entity.deleted = sqlite3_column_int(query.get(), 10) != 0;
    return entity;
}

void insertEntityRevision(sqlite3* database, const WorldEntity& entity) {
    Statement insert(database,
        "INSERT INTO entity_revision(entity_id,revision,kind,name,aliases,tags,description,attributes_json,"
        "review_status,deleted,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    bindText(insert.get(), 1, entity.id);
    sqlite3_bind_int(insert.get(), 2, entity.revision);
    bindText(insert.get(), 3, entity.kind);
    bindText(insert.get(), 4, entity.name);
    bindText(insert.get(), 5, joinValues(entity.aliases));
    bindText(insert.get(), 6, joinValues(entity.tags));
    bindText(insert.get(), 7, entity.description);
    bindText(insert.get(), 8, entity.attributes_json);
    bindText(insert.get(), 9, entity.review_status);
    sqlite3_bind_int(insert.get(), 10, entity.deleted ? 1 : 0);
    bindText(insert.get(), 11, utcNow());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database));
}

void updateEntityHead(sqlite3* database, const WorldEntity& entity) {
    Statement update(database, "UPDATE world_entity SET head_revision=?,deleted=? WHERE id=?");
    sqlite3_bind_int(update.get(), 1, entity.revision); sqlite3_bind_int(update.get(), 2, entity.deleted ? 1 : 0);
    bindText(update.get(), 3, entity.id);
    if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database) != 1)
        throw std::runtime_error("无法更新条目头修订");
}

std::optional<std::tuple<std::string, std::string, int>> entityCommandResult(sqlite3* database,
                                                                             const std::string& command_id) {
    Statement query(database, "SELECT payload_hash,entity_id,revision FROM entity_command_log WHERE command_id=?");
    bindText(query.get(), 1, command_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) return std::nullopt;
    return std::make_tuple(columnText(query.get(), 0), columnText(query.get(), 1), sqlite3_column_int(query.get(), 2));
}

void recordEntityCommand(sqlite3* database, const std::string& command_id, const std::string& payload,
                         const WorldEntity& entity) {
    Statement insert(database,
        "INSERT INTO entity_command_log(command_id,payload_hash,entity_id,revision,created_at) VALUES(?,?,?,?,?)");
    bindText(insert.get(), 1, command_id);
    bindText(insert.get(), 2, payload);
    bindText(insert.get(), 3, entity.id);
    sqlite3_bind_int(insert.get(), 4, entity.revision);
    bindText(insert.get(), 5, utcNow());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database));
}

SourceDocument readSource(sqlite3* database, const std::string& source_id) {
    Statement query(database,
        "SELECT id,world_id,name,sha256,original_asset_ref,normalized_asset_ref,edition "
        "FROM source_document WHERE id=?");
    bindText(query.get(), 1, source_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到来源文档：" + source_id);
    SourceDocument document;
    document.id = columnText(query.get(), 0);
    document.world_id = columnText(query.get(), 1);
    document.name = columnText(query.get(), 2);
    document.sha256 = columnText(query.get(), 3);
    document.original_asset_ref = columnText(query.get(), 4);
    document.normalized_asset_ref = columnText(query.get(), 5);
    document.edition = columnText(query.get(), 6);
    {
        Statement encoding(database, "SELECT encoding FROM source_encoding WHERE source_id=?");
        bindText(encoding.get(), 1, source_id);
        if (sqlite3_step(encoding.get()) == SQLITE_ROW) document.detected_encoding = columnText(encoding.get(), 0);
    }
    {
        Statement head(database, "SELECT revision FROM source_chapter_head WHERE source_id=?");
        bindText(head.get(), 1, source_id);
        if (sqlite3_step(head.get()) == SQLITE_ROW) document.chapter_revision = sqlite3_column_int(head.get(), 0);
    }
    Statement chapters(database,
        "SELECT id,title,ordinal,start_byte,end_byte,start_codepoint,end_codepoint "
        "FROM source_chapter WHERE document_id=? ORDER BY ordinal");
    bindText(chapters.get(), 1, source_id);
    while (sqlite3_step(chapters.get()) == SQLITE_ROW) {
        document.chapters.push_back(SourceChapter{
            columnText(chapters.get(), 0), columnText(chapters.get(), 1), sqlite3_column_int(chapters.get(), 2),
            static_cast<std::size_t>(sqlite3_column_int64(chapters.get(), 3)),
            static_cast<std::size_t>(sqlite3_column_int64(chapters.get(), 4)),
            static_cast<std::size_t>(sqlite3_column_int64(chapters.get(), 5)),
            static_cast<std::size_t>(sqlite3_column_int64(chapters.get(), 6)),
        });
    }
    return document;
}

CharacterBlueprint readBlueprint(sqlite3* database, const std::string& id, int version = -1) {
    const char* sql = version < 0
        ? "SELECT b.id,v.version,v.name,v.summary,v.core_values,v.traits,v.long_term_goal,v.short_term_goal,"
          "v.speech_style,v.abilities_json,v.equipment,v.background,v.private_notes,v.extensions_json,v.deleted "
          "FROM character_blueprint b JOIN character_blueprint_version v ON v.blueprint_id=b.id AND v.version=b.head_version "
          "WHERE b.id=?"
        : "SELECT b.id,v.version,v.name,v.summary,v.core_values,v.traits,v.long_term_goal,v.short_term_goal,"
          "v.speech_style,v.abilities_json,v.equipment,v.background,v.private_notes,v.extensions_json,v.deleted "
          "FROM character_blueprint b JOIN character_blueprint_version v ON v.blueprint_id=b.id "
          "WHERE b.id=? AND v.version=?";
    Statement query(database, sql);
    bindText(query.get(), 1, id);
    if (version >= 0) sqlite3_bind_int(query.get(), 2, version);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到人物卡或指定版本：" + id);
    CharacterBlueprint result;
    result.id = columnText(query.get(), 0);
    result.version = sqlite3_column_int(query.get(), 1);
    result.name = columnText(query.get(), 2);
    result.summary = columnText(query.get(), 3);
    result.values = splitValues(columnText(query.get(), 4));
    result.traits = splitValues(columnText(query.get(), 5));
    result.long_term_goal = columnText(query.get(), 6);
    result.short_term_goal = columnText(query.get(), 7);
    result.speech_style = columnText(query.get(), 8);
    result.abilities_json = columnText(query.get(), 9);
    result.equipment = splitValues(columnText(query.get(), 10));
    result.background = columnText(query.get(), 11);
    result.private_notes = columnText(query.get(), 12);
    result.extensions_json = columnText(query.get(), 13);
    result.deleted = sqlite3_column_int(query.get(), 14) != 0;
    return result;
}

std::string blueprintPayload(const CharacterBlueprint& blueprint, int expected, std::string_view operation) {
    std::ostringstream out;
    out << operation << '|' << expected << '|' << blueprint.id << '|' << blueprint.name << '|'
        << blueprint.summary << '|' << joinValues(blueprint.values) << '|' << joinValues(blueprint.traits) << '|'
        << blueprint.long_term_goal << '|' << blueprint.short_term_goal << '|' << blueprint.speech_style << '|'
        << blueprint.abilities_json << '|' << joinValues(blueprint.equipment) << '|' << blueprint.background << '|'
        << blueprint.private_notes << '|' << blueprint.extensions_json << '|' << blueprint.deleted;
    return out.str();
}

void insertBlueprintVersion(sqlite3* database, const CharacterBlueprint& blueprint) {
    Statement insert(database,
        "INSERT INTO character_blueprint_version(blueprint_id,version,name,summary,core_values,traits,long_term_goal,"
        "short_term_goal,speech_style,abilities_json,equipment,background,private_notes,extensions_json,deleted,created_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    bindText(insert.get(), 1, blueprint.id);
    sqlite3_bind_int(insert.get(), 2, blueprint.version);
    bindText(insert.get(), 3, blueprint.name);
    bindText(insert.get(), 4, blueprint.summary);
    bindText(insert.get(), 5, joinValues(blueprint.values));
    bindText(insert.get(), 6, joinValues(blueprint.traits));
    bindText(insert.get(), 7, blueprint.long_term_goal);
    bindText(insert.get(), 8, blueprint.short_term_goal);
    bindText(insert.get(), 9, blueprint.speech_style);
    bindText(insert.get(), 10, blueprint.abilities_json);
    bindText(insert.get(), 11, joinValues(blueprint.equipment));
    bindText(insert.get(), 12, blueprint.background);
    bindText(insert.get(), 13, blueprint.private_notes);
    bindText(insert.get(), 14, blueprint.extensions_json);
    sqlite3_bind_int(insert.get(), 15, blueprint.deleted ? 1 : 0);
    bindText(insert.get(), 16, utcNow());
    if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database));
}

ProviderConnection readProvider(sqlite3* database, const std::string& id) {
    Statement query(database,
        "SELECT id,name,kind,endpoint,default_model,credential_ref,data_policy,enabled,deleted,revision "
        "FROM provider_connection WHERE id=?");
    bindText(query.get(), 1, id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到模型连接：" + id);
    ProviderConnection value;
    value.id = columnText(query.get(), 0); value.name = columnText(query.get(), 1);
    value.kind = columnText(query.get(), 2); value.endpoint = columnText(query.get(), 3);
    value.default_model = columnText(query.get(), 4); value.credential_ref = columnText(query.get(), 5);
    value.data_policy = columnText(query.get(), 6); value.enabled = sqlite3_column_int(query.get(), 7) != 0;
    value.deleted = sqlite3_column_int(query.get(), 8) != 0; value.revision = sqlite3_column_int(query.get(), 9);
    return value;
}

std::string providerPayload(const ProviderConnection& value, int expected, std::string_view operation) {
    std::ostringstream out;
    out << operation << '|' << expected << '|' << value.id << '|' << value.name << '|' << value.kind << '|'
        << value.endpoint << '|' << value.default_model << '|' << value.credential_ref << '|'
        << value.data_policy << '|' << value.enabled << '|' << value.deleted;
    return out.str();
}

EvidenceReference readEvidence(sqlite3_stmt* query) {
    EvidenceReference value;
    value.id = columnText(query, 0); value.entity_id = columnText(query, 1);
    value.field_path = columnText(query, 2); value.source_id = columnText(query, 3);
    value.start_codepoint = static_cast<std::size_t>(sqlite3_column_int64(query, 4));
    value.end_codepoint = static_cast<std::size_t>(sqlite3_column_int64(query, 5));
    value.quote = columnText(query, 6); value.quote_hash = columnText(query, 7);
    value.provenance_type = columnText(query, 8); value.revision = sqlite3_column_int(query, 9);
    return value;
}

xuyan::domain::ExtractionJob readExtractionJob(sqlite3* database, const std::string& job_id) {
    Statement query(database, "SELECT id,source_id,status,schema_version,prompt_version,provider_connection_id,model_id,total_steps,completed_steps,cancel_requested,revision FROM extraction_job WHERE id=?");
    bindText(query.get(), 1, job_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到提取任务：" + job_id);
    xuyan::domain::ExtractionJob job;
    job.id = columnText(query.get(), 0); job.source_id = columnText(query.get(), 1); job.status = columnText(query.get(), 2);
    job.schema_version = columnText(query.get(), 3); job.prompt_version = columnText(query.get(), 4);
    job.provider_connection_id = columnText(query.get(), 5); job.model_id = columnText(query.get(), 6);
    job.total_steps = sqlite3_column_int(query.get(), 7); job.completed_steps = sqlite3_column_int(query.get(), 8);
    job.cancel_requested = sqlite3_column_int(query.get(), 9) != 0; job.revision = sqlite3_column_int(query.get(), 10);
    {
        Statement budget(database, "SELECT estimated_input_tokens,output_token_limit,max_requests,consumed_requests,sample_steps,price_known,estimated_cost_microunits,currency FROM extraction_budget WHERE job_id=?");
        bindText(budget.get(), 1, job_id);
        if (sqlite3_step(budget.get()) == SQLITE_ROW) {
            job.budget.estimated_input_tokens = static_cast<std::size_t>(sqlite3_column_int64(budget.get(), 0));
            job.budget.output_token_limit_per_request = sqlite3_column_int(budget.get(), 1);
            job.budget.max_requests = sqlite3_column_int(budget.get(), 2);
            job.budget.consumed_requests = sqlite3_column_int(budget.get(), 3);
            job.budget.sample_steps = sqlite3_column_int(budget.get(), 4);
            job.budget.price_known = sqlite3_column_int(budget.get(), 5) != 0;
            job.budget.estimated_cost_microunits = sqlite3_column_int64(budget.get(), 6);
            job.budget.currency = columnText(budget.get(), 7);
        }
    }
    Statement steps(database, "SELECT id,job_id,ordinal,start_codepoint,end_codepoint,chunk_hash,status,attempt,output_json,error_message FROM extraction_step WHERE job_id=? ORDER BY ordinal");
    bindText(steps.get(), 1, job_id);
    while (sqlite3_step(steps.get()) == SQLITE_ROW) {
        xuyan::domain::ExtractionStep step;
        step.id = columnText(steps.get(), 0); step.job_id = columnText(steps.get(), 1);
        step.ordinal = sqlite3_column_int(steps.get(), 2);
        step.start_codepoint = static_cast<std::size_t>(sqlite3_column_int64(steps.get(), 3));
        step.end_codepoint = static_cast<std::size_t>(sqlite3_column_int64(steps.get(), 4));
        step.chunk_hash = columnText(steps.get(), 5); step.status = columnText(steps.get(), 6);
        step.attempt = sqlite3_column_int(steps.get(), 7); step.output_json = columnText(steps.get(), 8);
        step.error_message = columnText(steps.get(), 9); job.steps.push_back(std::move(step));
    }
    return job;
}

xuyan::domain::ExtractionCandidate readCandidate(sqlite3_stmt* query) {
    xuyan::domain::ExtractionCandidate value;
    value.id = columnText(query, 0); value.job_id = columnText(query, 1); value.step_ordinal = sqlite3_column_int(query, 2);
    value.source_id = columnText(query, 3); value.candidate_type = columnText(query, 4); value.name = columnText(query, 5);
    value.fields_json = columnText(query, 6); value.start_codepoint = static_cast<std::size_t>(sqlite3_column_int64(query, 7));
    value.end_codepoint = static_cast<std::size_t>(sqlite3_column_int64(query, 8)); value.quote = columnText(query, 9);
    value.quote_hash = columnText(query, 10); value.provenance_type = columnText(query, 11);
    value.review_status = columnText(query, 12); value.schema_version = columnText(query, 13);
    value.prompt_version = columnText(query, 14); value.revision = sqlite3_column_int(query, 15);
    return value;
}

xuyan::domain::WorldVersion readWorldVersion(sqlite3* database, const std::string& version_id) {
    Statement query(database, "SELECT id,world_id,parent_id,status,content_hash,published_at FROM world_version WHERE id=?");
    bindText(query.get(), 1, version_id);
    if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到世界版本：" + version_id);
    xuyan::domain::WorldVersion version;
    version.id = columnText(query.get(), 0); version.world_id = columnText(query.get(), 1);
    version.parent_id = columnText(query.get(), 2); version.status = columnText(query.get(), 3);
    version.content_hash = columnText(query.get(), 4); version.published_at = columnText(query.get(), 5);
    Statement members(database, "SELECT m.entity_id,m.entity_revision,COALESCE(s.scope_revision,0) FROM world_version_member m LEFT JOIN world_version_member_scope s ON s.version_id=m.version_id AND s.entity_id=m.entity_id WHERE m.version_id=? ORDER BY m.entity_id");
    bindText(members.get(), 1, version_id);
    while (sqlite3_step(members.get()) == SQLITE_ROW)
        version.members.push_back({columnText(members.get(), 0), sqlite3_column_int(members.get(), 1), sqlite3_column_int(members.get(), 2)});
    return version;
}

xuyan::domain::TimelineEvent readTimelineEvent(sqlite3* database, const std::string& event_id) {
    Statement query(database, "SELECT id,world_id,name,has_story_time,story_time,narrative_order,relative_time,truth_status,revision FROM timeline_event WHERE id=?");
    bindText(query.get(), 1, event_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到时间事件");
    xuyan::domain::TimelineEvent event; event.id = columnText(query.get(), 0); event.world_id = columnText(query.get(), 1);
    event.name = columnText(query.get(), 2); if (sqlite3_column_int(query.get(), 3)) event.story_time = sqlite3_column_int64(query.get(), 4);
    event.narrative_order = sqlite3_column_int(query.get(), 5); event.relative_time = columnText(query.get(), 6);
    event.truth_status = columnText(query.get(), 7); event.revision = sqlite3_column_int(query.get(), 8);
    Statement edges(database, "SELECT edge_kind,target_event_id FROM timeline_event_edge WHERE event_id=? ORDER BY edge_kind,target_event_id");
    bindText(edges.get(), 1, event_id);
    while (sqlite3_step(edges.get()) == SQLITE_ROW) {
        const auto kind = columnText(edges.get(), 0); const auto target = columnText(edges.get(), 1);
        if (kind == "prerequisite") event.prerequisites.push_back(target);
        else if (kind == "cause") event.causes.push_back(target); else event.results.push_back(target);
    }
    return event;
}

xuyan::domain::DirectedRelation readDirectedRelation(sqlite3* database, const std::string& relation_id) {
    Statement query(database, "SELECT id,world_id,from_entity_id,to_entity_id,dimension,strength,has_valid_from,valid_from,has_valid_to,valid_to,visibility,evidence_status,revision FROM directed_relation WHERE id=?");
    bindText(query.get(), 1, relation_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到定向关系");
    xuyan::domain::DirectedRelation relation; relation.id = columnText(query.get(), 0); relation.world_id = columnText(query.get(), 1);
    relation.from_entity_id = columnText(query.get(), 2); relation.to_entity_id = columnText(query.get(), 3);
    relation.dimension = columnText(query.get(), 4); relation.strength = sqlite3_column_int(query.get(), 5);
    if (sqlite3_column_int(query.get(), 6)) relation.valid_from = sqlite3_column_int64(query.get(), 7);
    if (sqlite3_column_int(query.get(), 8)) relation.valid_to = sqlite3_column_int64(query.get(), 9);
    relation.visibility = columnText(query.get(), 10); relation.evidence_status = columnText(query.get(), 11);
    relation.revision = sqlite3_column_int(query.get(), 12);
    Statement grants(database, "SELECT actor_id FROM directed_relation_grant WHERE relation_id=? ORDER BY actor_id");
    bindText(grants.get(), 1, relation_id);
    while (sqlite3_step(grants.get()) == SQLITE_ROW) relation.actor_grants.push_back(columnText(grants.get(), 0));
    return relation;
}

xuyan::domain::LocationPlacement readLocationPlacement(sqlite3* database, const std::string& location_id) {
    Statement query(database, "SELECT location_id,parent_location_id,has_image_point,image_x,image_y,background_asset_ref,evidence_status,revision FROM location_placement WHERE location_id=?");
    bindText(query.get(), 1, location_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到地点标注");
    xuyan::domain::LocationPlacement value; value.location_id = columnText(query.get(), 0); value.parent_location_id = columnText(query.get(), 1);
    if (sqlite3_column_int(query.get(), 2)) { value.image_x = sqlite3_column_int(query.get(), 3); value.image_y = sqlite3_column_int(query.get(), 4); }
    value.background_asset_ref = columnText(query.get(), 5); value.evidence_status = columnText(query.get(), 6); value.revision = sqlite3_column_int(query.get(), 7);
    return value;
}

xuyan::domain::TravelRoute readTravelRoute(sqlite3* database, const std::string& route_id) {
    Statement query(database, "SELECT id,from_location_id,to_location_id,has_travel_minutes,travel_minutes,bidirectional,evidence_status,revision FROM travel_route WHERE id=?");
    bindText(query.get(), 1, route_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到地点路线");
    xuyan::domain::TravelRoute value; value.id = columnText(query.get(), 0); value.from_location_id = columnText(query.get(), 1);
    value.to_location_id = columnText(query.get(), 2); if (sqlite3_column_int(query.get(), 3)) value.travel_minutes = sqlite3_column_int(query.get(), 4);
    value.bidirectional = sqlite3_column_int(query.get(), 5) != 0; value.evidence_status = columnText(query.get(), 6); value.revision = sqlite3_column_int(query.get(), 7);
    return value;
}

xuyan::domain::HistoricalSnapshot readHistoricalSnapshot(sqlite3* database, const std::string& snapshot_id) {
    Statement query(database, "SELECT id,world_version_id,story_time,content_hash FROM historical_snapshot WHERE id=?");
    bindText(query.get(), 1, snapshot_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到历史快照");
    xuyan::domain::HistoricalSnapshot snapshot; snapshot.id = columnText(query.get(), 0); snapshot.world_version_id = columnText(query.get(), 1);
    snapshot.story_time = sqlite3_column_int64(query.get(), 2); snapshot.content_hash = columnText(query.get(), 3);
    Statement members(database, "SELECT m.entity_id,m.entity_revision,COALESCE(s.scope_revision,0) FROM historical_snapshot_member m LEFT JOIN world_version_member_scope s ON s.version_id=? AND s.entity_id=m.entity_id WHERE m.snapshot_id=? ORDER BY m.entity_id");
    bindText(members.get(), 1, snapshot.world_version_id); bindText(members.get(), 2, snapshot_id);
    while (sqlite3_step(members.get()) == SQLITE_ROW) snapshot.included_members.push_back({columnText(members.get(), 0), sqlite3_column_int(members.get(), 1), sqlite3_column_int(members.get(), 2)});
    Statement unresolved(database, "SELECT entity_id FROM historical_snapshot_unresolved WHERE snapshot_id=? ORDER BY entity_id"); bindText(unresolved.get(), 1, snapshot_id);
    while (sqlite3_step(unresolved.get()) == SQLITE_ROW) snapshot.unresolved_entity_ids.push_back(columnText(unresolved.get(), 0));
    return snapshot;
}

xuyan::domain::CharacterInstance readCharacterInstance(sqlite3* database, const std::string& instance_id) {
    Statement query(database, "SELECT id,blueprint_id,blueprint_version,world_version_id,snapshot_id,name,adaptation_json,knowledge_policy,memory_json,status,revision FROM character_instance WHERE id=?");
    bindText(query.get(), 1, instance_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到人物实例");
    xuyan::domain::CharacterInstance value; value.id = columnText(query.get(), 0); value.blueprint_id = columnText(query.get(), 1);
    value.blueprint_version = sqlite3_column_int(query.get(), 2); value.world_version_id = columnText(query.get(), 3);
    value.snapshot_id = columnText(query.get(), 4); value.name = columnText(query.get(), 5); value.adaptation_json = columnText(query.get(), 6);
    value.knowledge_policy = columnText(query.get(), 7); value.memory_json = columnText(query.get(), 8);
    value.status = columnText(query.get(), 9); value.revision = sqlite3_column_int(query.get(), 10);
    Statement conflicts(database, "SELECT message FROM character_instance_conflict WHERE instance_id=? ORDER BY message"); bindText(conflicts.get(), 1, instance_id);
    while (sqlite3_step(conflicts.get()) == SQLITE_ROW) value.conflicts.push_back(columnText(conflicts.get(), 0));
    return value;
}

xuyan::domain::BranchRootBinding readBranchRootBinding(sqlite3* database, const std::string& branch_id) {
    Statement query(database, "SELECT branch_id,world_version_id,snapshot_id,history_mode,root_hash FROM branch_root_binding WHERE branch_id=?");
    bindText(query.get(), 1, branch_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到分支根绑定");
    xuyan::domain::BranchRootBinding value; value.branch_id = columnText(query.get(), 0); value.world_version_id = columnText(query.get(), 1);
    value.snapshot_id = columnText(query.get(), 2); value.history_mode = columnText(query.get(), 3); value.root_hash = columnText(query.get(), 4);
    Statement instances(database, "SELECT instance_id FROM branch_character_instance WHERE branch_id=? ORDER BY instance_id"); bindText(instances.get(), 1, branch_id);
    while (sqlite3_step(instances.get()) == SQLITE_ROW) value.character_instance_ids.push_back(columnText(instances.get(), 0));
    return value;
}

xuyan::domain::SimulationTurn readSimulationTurn(sqlite3* database, const std::string& turn_id) {
    Statement query(database, "SELECT id,session_id,ordinal,input_commit_id,committed_commit_id,actor_id,status,speech,operation,target_id,holder_consented,ends_scene,public_reason,draft_narration,final_narration,error_message,revision FROM simulation_turn WHERE id=?");
    bindText(query.get(), 1, turn_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到推演回合");
    xuyan::domain::SimulationTurn turn; turn.id = columnText(query.get(), 0); turn.session_id = columnText(query.get(), 1);
    turn.ordinal = sqlite3_column_int(query.get(), 2); turn.input_commit_id = columnText(query.get(), 3);
    turn.committed_commit_id = columnText(query.get(), 4); turn.actor_id = columnText(query.get(), 5); turn.status = columnText(query.get(), 6);
    turn.intent.actor_id = turn.actor_id; turn.intent.input_commit_id = turn.input_commit_id; turn.intent.speech = columnText(query.get(), 7);
    turn.intent.operation = columnText(query.get(), 8); turn.intent.target_id = columnText(query.get(), 9);
    turn.intent.holder_consented = sqlite3_column_int(query.get(), 10) != 0; turn.intent.ends_scene = sqlite3_column_int(query.get(), 11) != 0;
    turn.intent.public_reason = columnText(query.get(), 12); turn.draft_narration = columnText(query.get(), 13);
    turn.final_narration = columnText(query.get(), 14); turn.error_message = columnText(query.get(), 15); turn.revision = sqlite3_column_int(query.get(), 16);
    Statement call(database, "SELECT id,turn_id,provider_connection_id,model_id,request_hash,status,input_tokens,output_tokens,failure_kind FROM simulation_provider_call WHERE turn_id=?"); bindText(call.get(), 1, turn_id);
    if (sqlite3_step(call.get()) == SQLITE_ROW) { turn.call.id = columnText(call.get(), 0); turn.call.turn_id = columnText(call.get(), 1); turn.call.provider_connection_id = columnText(call.get(), 2); turn.call.model_id = columnText(call.get(), 3); turn.call.request_hash = columnText(call.get(), 4); turn.call.status = columnText(call.get(), 5); turn.call.input_tokens = sqlite3_column_int(call.get(), 6); turn.call.output_tokens = sqlite3_column_int(call.get(), 7); turn.call.failure_kind = columnText(call.get(), 8); }
    return turn;
}

xuyan::domain::SimulationSession readSimulationSession(sqlite3* database, const std::string& session_id) {
    Statement query(database, "SELECT id,branch_id,status,max_turns,continuous,no_progress_limit,max_calls,used_calls,reserved_calls,unknown_calls,pause_requested,cancel_requested,revision FROM simulation_session WHERE id=?");
    bindText(query.get(), 1, session_id); if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到推演会话");
    xuyan::domain::SimulationSession session; session.id = columnText(query.get(), 0); session.branch_id = columnText(query.get(), 1);
    session.status = columnText(query.get(), 2); session.max_turns = sqlite3_column_int(query.get(), 3); session.continuous = sqlite3_column_int(query.get(), 4) != 0;
    session.no_progress_limit = sqlite3_column_int(query.get(), 5); session.max_calls = sqlite3_column_int(query.get(), 6);
    session.used_calls = sqlite3_column_int(query.get(), 7); session.reserved_calls = sqlite3_column_int(query.get(), 8); session.unknown_calls = sqlite3_column_int(query.get(), 9);
    session.pause_requested = sqlite3_column_int(query.get(), 10) != 0; session.cancel_requested = sqlite3_column_int(query.get(), 11) != 0; session.revision = sqlite3_column_int(query.get(), 12);
    Statement actors(database, "SELECT actor_id,provider_connection_id,model_id FROM simulation_actor_binding WHERE session_id=? ORDER BY ordinal"); bindText(actors.get(), 1, session_id);
    while (sqlite3_step(actors.get()) == SQLITE_ROW) session.actors.push_back({columnText(actors.get(), 0), columnText(actors.get(), 1), columnText(actors.get(), 2)});
    Statement turns(database, "SELECT id FROM simulation_turn WHERE session_id=? ORDER BY ordinal"); bindText(turns.get(), 1, session_id); std::vector<std::string> turn_ids; while (sqlite3_step(turns.get()) == SQLITE_ROW) turn_ids.push_back(columnText(turns.get(), 0));
    for (const auto& id : turn_ids) session.turns.push_back(readSimulationTurn(database, id));
    return session;
}

} // namespace

WorkspaceRepository::WorkspaceRepository(const std::filesystem::path& database_path) {
    if (!database_path.parent_path().empty()) {
        std::filesystem::create_directories(database_path.parent_path());
    }
    const auto utf8 = database_path.u8string();
    if (sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &database_) != SQLITE_OK) {
        const auto message = database_ == nullptr ? "cannot open SQLite workspace" : sqlite3_errmsg(database_);
        if (database_ != nullptr) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(message);
    }
    sqlite3_busy_timeout(database_, 5000);
    auto migration_mutex = writeMutexFor(database_);
    std::lock_guard migration_lock(*migration_mutex);
    migrate();
}

WorkspaceRepository::~WorkspaceRepository() {
    if (database_ != nullptr) sqlite3_close(database_);
}

void WorkspaceRepository::migrate() {
    constexpr auto sql = R"SQL(
PRAGMA foreign_keys=ON;
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS branch(
  id TEXT PRIMARY KEY,name TEXT NOT NULL,parent_id TEXT,fork_commit_id TEXT NOT NULL,head_commit_id TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS state_snapshot(
  branch_id TEXT NOT NULL,commit_id TEXT PRIMARY KEY,parent_commit_id TEXT NOT NULL,state_hash TEXT NOT NULL,
  state_json TEXT NOT NULL,revision INTEGER NOT NULL,turn INTEGER NOT NULL,elapsed_ticks INTEGER NOT NULL,
  seal_holder_id TEXT NOT NULL,seal_inspected INTEGER NOT NULL,paused INTEGER NOT NULL,completed INTEGER NOT NULL,
  narration TEXT NOT NULL,xu_gate INTEGER NOT NULL,xu_forgery INTEGER NOT NULL,xu_trust INTEGER NOT NULL,
  shen_gate INTEGER NOT NULL,shen_forgery INTEGER NOT NULL,shen_trust INTEGER NOT NULL,created_at TEXT NOT NULL,
  FOREIGN KEY(branch_id) REFERENCES branch(id)
);
CREATE TABLE IF NOT EXISTS command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,result_commit_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS world_entity(
  id TEXT PRIMARY KEY,world_id TEXT NOT NULL,head_revision INTEGER NOT NULL,deleted INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS entity_revision(
  entity_id TEXT NOT NULL,revision INTEGER NOT NULL,kind TEXT NOT NULL,name TEXT NOT NULL,aliases TEXT NOT NULL,
  tags TEXT NOT NULL,description TEXT NOT NULL,attributes_json TEXT NOT NULL,review_status TEXT NOT NULL,
  deleted INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL,PRIMARY KEY(entity_id,revision),
  FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS entity_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,entity_id TEXT NOT NULL,revision INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_entity_kind ON entity_revision(kind);
CREATE INDEX IF NOT EXISTS idx_entity_name ON entity_revision(name);
CREATE TABLE IF NOT EXISTS source_document(
  id TEXT PRIMARY KEY,world_id TEXT NOT NULL,name TEXT NOT NULL,sha256 TEXT NOT NULL,
  original_asset_ref TEXT NOT NULL,normalized_asset_ref TEXT NOT NULL,edition TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS source_chapter(
  id TEXT PRIMARY KEY,document_id TEXT NOT NULL,title TEXT NOT NULL,ordinal INTEGER NOT NULL,
  start_byte INTEGER NOT NULL,end_byte INTEGER NOT NULL,start_codepoint INTEGER NOT NULL,end_codepoint INTEGER NOT NULL,
  FOREIGN KEY(document_id) REFERENCES source_document(id)
);
CREATE TABLE IF NOT EXISTS source_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,source_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_source_sha ON source_document(sha256);
CREATE TABLE IF NOT EXISTS character_blueprint(
  id TEXT PRIMARY KEY,head_version INTEGER NOT NULL,deleted INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS character_blueprint_version(
  blueprint_id TEXT NOT NULL,version INTEGER NOT NULL,name TEXT NOT NULL,summary TEXT NOT NULL,core_values TEXT NOT NULL,
  traits TEXT NOT NULL,long_term_goal TEXT NOT NULL,short_term_goal TEXT NOT NULL,speech_style TEXT NOT NULL,
  abilities_json TEXT NOT NULL,equipment TEXT NOT NULL,background TEXT NOT NULL,private_notes TEXT NOT NULL,
  extensions_json TEXT NOT NULL,deleted INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL,
  PRIMARY KEY(blueprint_id,version),FOREIGN KEY(blueprint_id) REFERENCES character_blueprint(id)
);
CREATE TABLE IF NOT EXISTS blueprint_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,blueprint_id TEXT NOT NULL,version INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS package_import_log(
  command_id TEXT PRIMARY KEY,package_hash TEXT NOT NULL,imported_count INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS provider_connection(
  id TEXT PRIMARY KEY,name TEXT NOT NULL,kind TEXT NOT NULL,endpoint TEXT NOT NULL,default_model TEXT NOT NULL,
  credential_ref TEXT NOT NULL,data_policy TEXT NOT NULL,enabled INTEGER NOT NULL,deleted INTEGER NOT NULL DEFAULT 0,
  revision INTEGER NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS provider_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,connection_id TEXT NOT NULL,revision INTEGER NOT NULL,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS evidence_reference(
  id TEXT PRIMARY KEY,entity_id TEXT NOT NULL,field_path TEXT NOT NULL,source_id TEXT NOT NULL,
  start_codepoint INTEGER NOT NULL,end_codepoint INTEGER NOT NULL,quote TEXT NOT NULL,quote_hash TEXT NOT NULL,
  provenance_type TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL,
  FOREIGN KEY(entity_id) REFERENCES world_entity(id),FOREIGN KEY(source_id) REFERENCES source_document(id)
);
CREATE TABLE IF NOT EXISTS evidence_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,evidence_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_evidence_source ON evidence_reference(source_id,start_codepoint);
CREATE INDEX IF NOT EXISTS idx_evidence_entity ON evidence_reference(entity_id,field_path);
CREATE TABLE IF NOT EXISTS entity_merge(
  id TEXT PRIMARY KEY,source_id TEXT NOT NULL,target_id TEXT NOT NULL,
  source_previous_revision INTEGER NOT NULL,target_previous_revision INTEGER NOT NULL,
  source_merged_revision INTEGER NOT NULL,target_merged_revision INTEGER NOT NULL,
  source_split_revision INTEGER,target_split_revision INTEGER,active INTEGER NOT NULL,created_at TEXT NOT NULL,split_at TEXT
);
CREATE TABLE IF NOT EXISTS evidence_entity_rewrite(
  merge_id TEXT NOT NULL,evidence_id TEXT NOT NULL,from_entity_id TEXT NOT NULL,to_entity_id TEXT NOT NULL,
  PRIMARY KEY(merge_id,evidence_id)
);
CREATE TABLE IF NOT EXISTS entity_merge_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,merge_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS source_encoding(
  source_id TEXT PRIMARY KEY,encoding TEXT NOT NULL,FOREIGN KEY(source_id) REFERENCES source_document(id)
);
CREATE TABLE IF NOT EXISTS source_chapter_head(
  source_id TEXT PRIMARY KEY,revision INTEGER NOT NULL,FOREIGN KEY(source_id) REFERENCES source_document(id)
);
INSERT OR IGNORE INTO source_chapter_head(source_id,revision) SELECT id,1 FROM source_document;
CREATE TABLE IF NOT EXISTS source_chapter_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,source_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS extraction_job(
  id TEXT PRIMARY KEY,source_id TEXT NOT NULL,status TEXT NOT NULL,schema_version TEXT NOT NULL,prompt_version TEXT NOT NULL,
  provider_connection_id TEXT NOT NULL,model_id TEXT NOT NULL,total_steps INTEGER NOT NULL,completed_steps INTEGER NOT NULL,
  cancel_requested INTEGER NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,
  FOREIGN KEY(source_id) REFERENCES source_document(id)
);
CREATE TABLE IF NOT EXISTS extraction_step(
  id TEXT PRIMARY KEY,job_id TEXT NOT NULL,ordinal INTEGER NOT NULL,start_codepoint INTEGER NOT NULL,end_codepoint INTEGER NOT NULL,
  chunk_hash TEXT NOT NULL,status TEXT NOT NULL,attempt INTEGER NOT NULL,output_json TEXT NOT NULL,error_message TEXT NOT NULL,
  updated_at TEXT NOT NULL,UNIQUE(job_id,ordinal),FOREIGN KEY(job_id) REFERENCES extraction_job(id)
);
CREATE TABLE IF NOT EXISTS extraction_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,job_id TEXT NOT NULL,step_ordinal INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_extraction_job_status ON extraction_job(status,updated_at);
CREATE INDEX IF NOT EXISTS idx_extraction_step_status ON extraction_step(job_id,status,ordinal);
CREATE TABLE IF NOT EXISTS extraction_candidate(
  id TEXT PRIMARY KEY,job_id TEXT NOT NULL,step_ordinal INTEGER NOT NULL,source_id TEXT NOT NULL,candidate_type TEXT NOT NULL,
  name TEXT NOT NULL,fields_json TEXT NOT NULL,start_codepoint INTEGER NOT NULL,end_codepoint INTEGER NOT NULL,
  quote TEXT NOT NULL,quote_hash TEXT NOT NULL,provenance_type TEXT NOT NULL,review_status TEXT NOT NULL,
  schema_version TEXT NOT NULL,prompt_version TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,
  FOREIGN KEY(job_id) REFERENCES extraction_job(id),FOREIGN KEY(source_id) REFERENCES source_document(id)
);
CREATE INDEX IF NOT EXISTS idx_candidate_review ON extraction_candidate(review_status,candidate_type,created_at);
CREATE INDEX IF NOT EXISTS idx_candidate_source ON extraction_candidate(source_id,start_codepoint);
CREATE TABLE IF NOT EXISTS candidate_review_history(
  candidate_id TEXT NOT NULL,revision INTEGER NOT NULL,name TEXT NOT NULL,fields_json TEXT NOT NULL,
  provenance_type TEXT NOT NULL,review_status TEXT NOT NULL,updated_at TEXT NOT NULL,
  PRIMARY KEY(candidate_id,revision),FOREIGN KEY(candidate_id) REFERENCES extraction_candidate(id)
);
INSERT OR IGNORE INTO candidate_review_history(candidate_id,revision,name,fields_json,provenance_type,review_status,updated_at)
SELECT id,revision,name,fields_json,provenance_type,review_status,updated_at FROM extraction_candidate;
CREATE TABLE IF NOT EXISTS candidate_acceptance(
  candidate_id TEXT PRIMARY KEY,entity_id TEXT NOT NULL,accepted_at TEXT NOT NULL,
  FOREIGN KEY(candidate_id) REFERENCES extraction_candidate(id),FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS candidate_review_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,candidate_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS extraction_step_cache_hit(
  job_id TEXT NOT NULL,step_ordinal INTEGER NOT NULL,source_job_id TEXT NOT NULL,source_step_ordinal INTEGER NOT NULL,
  chunk_hash TEXT NOT NULL,created_at TEXT NOT NULL,PRIMARY KEY(job_id,step_ordinal),
  FOREIGN KEY(job_id) REFERENCES extraction_job(id),FOREIGN KEY(source_job_id) REFERENCES extraction_job(id)
);
CREATE TABLE IF NOT EXISTS extraction_job_cache_context(
  job_id TEXT PRIMARY KEY,entity_index_hash TEXT NOT NULL,parameters_hash TEXT NOT NULL,
  FOREIGN KEY(job_id) REFERENCES extraction_job(id)
);
CREATE TABLE IF NOT EXISTS entity_retrieval_scope(
  entity_id TEXT PRIMARY KEY,has_valid_from INTEGER NOT NULL,valid_from INTEGER NOT NULL,
  has_valid_to INTEGER NOT NULL,valid_to INTEGER NOT NULL,visibility TEXT NOT NULL,revision INTEGER NOT NULL,
  FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS entity_retrieval_grant(
  entity_id TEXT NOT NULL,actor_id TEXT NOT NULL,PRIMARY KEY(entity_id,actor_id),
  FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS entity_retrieval_scope_history(
  entity_id TEXT NOT NULL,revision INTEGER NOT NULL,has_valid_from INTEGER NOT NULL,valid_from INTEGER NOT NULL,
  has_valid_to INTEGER NOT NULL,valid_to INTEGER NOT NULL,visibility TEXT NOT NULL,
  PRIMARY KEY(entity_id,revision),FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS entity_retrieval_grant_history(
  entity_id TEXT NOT NULL,revision INTEGER NOT NULL,actor_id TEXT NOT NULL,
  PRIMARY KEY(entity_id,revision,actor_id),FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
INSERT OR IGNORE INTO entity_retrieval_scope_history(entity_id,revision,has_valid_from,valid_from,has_valid_to,valid_to,visibility)
SELECT entity_id,revision,has_valid_from,valid_from,has_valid_to,valid_to,visibility FROM entity_retrieval_scope;
INSERT OR IGNORE INTO entity_retrieval_grant_history(entity_id,revision,actor_id)
SELECT g.entity_id,s.revision,g.actor_id FROM entity_retrieval_grant g JOIN entity_retrieval_scope s ON s.entity_id=g.entity_id;
CREATE TABLE IF NOT EXISTS entity_retrieval_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,entity_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS extraction_budget(
  job_id TEXT PRIMARY KEY,estimated_input_tokens INTEGER NOT NULL,output_token_limit INTEGER NOT NULL,
  max_requests INTEGER NOT NULL,consumed_requests INTEGER NOT NULL,sample_steps INTEGER NOT NULL,
  price_known INTEGER NOT NULL,estimated_cost_microunits INTEGER NOT NULL,currency TEXT NOT NULL,
  FOREIGN KEY(job_id) REFERENCES extraction_job(id)
);
INSERT OR IGNORE INTO extraction_budget(job_id,estimated_input_tokens,output_token_limit,max_requests,consumed_requests,sample_steps,price_known,estimated_cost_microunits,currency)
SELECT j.id,COALESCE((SELECT SUM((end_codepoint-start_codepoint)*3/2+1) FROM extraction_step s WHERE s.job_id=j.id),0),
  1200,j.total_steps+MAX(1,j.total_steps/10),COALESCE((SELECT SUM(attempt) FROM extraction_step s WHERE s.job_id=j.id),0),
  MIN(3,j.total_steps),0,0,'' FROM extraction_job j;
CREATE TABLE IF NOT EXISTS world_version(
  id TEXT PRIMARY KEY,world_id TEXT NOT NULL,parent_id TEXT NOT NULL,status TEXT NOT NULL,
  content_hash TEXT NOT NULL,published_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS world_version_member(
  version_id TEXT NOT NULL,entity_id TEXT NOT NULL,entity_revision INTEGER NOT NULL,
  PRIMARY KEY(version_id,entity_id),FOREIGN KEY(version_id) REFERENCES world_version(id),
  FOREIGN KEY(entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS world_version_member_scope(
  version_id TEXT NOT NULL,entity_id TEXT NOT NULL,scope_revision INTEGER NOT NULL,
  PRIMARY KEY(version_id,entity_id),FOREIGN KEY(version_id) REFERENCES world_version(id)
);
CREATE TABLE IF NOT EXISTS world_publish_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,version_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS historical_snapshot(
  id TEXT PRIMARY KEY,world_version_id TEXT NOT NULL,story_time INTEGER NOT NULL,content_hash TEXT NOT NULL,created_at TEXT NOT NULL,
  FOREIGN KEY(world_version_id) REFERENCES world_version(id)
);
CREATE TABLE IF NOT EXISTS historical_snapshot_member(
  snapshot_id TEXT NOT NULL,entity_id TEXT NOT NULL,entity_revision INTEGER NOT NULL,
  PRIMARY KEY(snapshot_id,entity_id),FOREIGN KEY(snapshot_id) REFERENCES historical_snapshot(id)
);
CREATE TABLE IF NOT EXISTS historical_snapshot_unresolved(
  snapshot_id TEXT NOT NULL,entity_id TEXT NOT NULL,PRIMARY KEY(snapshot_id,entity_id),
  FOREIGN KEY(snapshot_id) REFERENCES historical_snapshot(id)
);
CREATE TABLE IF NOT EXISTS historical_snapshot_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,snapshot_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS timeline_event(
  id TEXT PRIMARY KEY,world_id TEXT NOT NULL,name TEXT NOT NULL,has_story_time INTEGER NOT NULL,story_time INTEGER NOT NULL,
  narrative_order INTEGER NOT NULL,relative_time TEXT NOT NULL,truth_status TEXT NOT NULL,revision INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS timeline_event_edge(
  event_id TEXT NOT NULL,edge_kind TEXT NOT NULL,target_event_id TEXT NOT NULL,
  PRIMARY KEY(event_id,edge_kind,target_event_id),FOREIGN KEY(event_id) REFERENCES timeline_event(id)
);
CREATE TABLE IF NOT EXISTS directed_relation(
  id TEXT PRIMARY KEY,world_id TEXT NOT NULL,from_entity_id TEXT NOT NULL,to_entity_id TEXT NOT NULL,dimension TEXT NOT NULL,
  strength INTEGER NOT NULL,has_valid_from INTEGER NOT NULL,valid_from INTEGER NOT NULL,has_valid_to INTEGER NOT NULL,valid_to INTEGER NOT NULL,
  visibility TEXT NOT NULL,evidence_status TEXT NOT NULL,revision INTEGER NOT NULL,
  FOREIGN KEY(from_entity_id) REFERENCES world_entity(id),FOREIGN KEY(to_entity_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS directed_relation_grant(
  relation_id TEXT NOT NULL,actor_id TEXT NOT NULL,PRIMARY KEY(relation_id,actor_id),FOREIGN KEY(relation_id) REFERENCES directed_relation(id)
);
CREATE TABLE IF NOT EXISTS location_placement(
  location_id TEXT PRIMARY KEY,parent_location_id TEXT NOT NULL,has_image_point INTEGER NOT NULL,image_x INTEGER NOT NULL,image_y INTEGER NOT NULL,
  background_asset_ref TEXT NOT NULL,evidence_status TEXT NOT NULL,revision INTEGER NOT NULL,
  FOREIGN KEY(location_id) REFERENCES world_entity(id)
);
CREATE TABLE IF NOT EXISTS travel_route(
  id TEXT PRIMARY KEY,from_location_id TEXT NOT NULL,to_location_id TEXT NOT NULL,has_travel_minutes INTEGER NOT NULL,
  travel_minutes INTEGER NOT NULL,bidirectional INTEGER NOT NULL,evidence_status TEXT NOT NULL,revision INTEGER NOT NULL,
  FOREIGN KEY(from_location_id) REFERENCES location_placement(location_id),FOREIGN KEY(to_location_id) REFERENCES location_placement(location_id)
);
CREATE TABLE IF NOT EXISTS world_graph_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,record_type TEXT NOT NULL,record_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS character_instance(
  id TEXT PRIMARY KEY,blueprint_id TEXT NOT NULL,blueprint_version INTEGER NOT NULL,world_version_id TEXT NOT NULL,
  snapshot_id TEXT NOT NULL,name TEXT NOT NULL,adaptation_json TEXT NOT NULL,knowledge_policy TEXT NOT NULL,
  memory_json TEXT NOT NULL,status TEXT NOT NULL,revision INTEGER NOT NULL,
  FOREIGN KEY(blueprint_id) REFERENCES character_blueprint(id),FOREIGN KEY(world_version_id) REFERENCES world_version(id),
  FOREIGN KEY(snapshot_id) REFERENCES historical_snapshot(id)
);
CREATE TABLE IF NOT EXISTS character_instance_conflict(
  instance_id TEXT NOT NULL,message TEXT NOT NULL,PRIMARY KEY(instance_id,message),FOREIGN KEY(instance_id) REFERENCES character_instance(id)
);
CREATE TABLE IF NOT EXISTS character_instance_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,instance_id TEXT NOT NULL,revision INTEGER NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS branch_root_binding(
  branch_id TEXT PRIMARY KEY,world_version_id TEXT NOT NULL,snapshot_id TEXT NOT NULL,history_mode TEXT NOT NULL,root_hash TEXT NOT NULL,
  FOREIGN KEY(branch_id) REFERENCES branch(id),FOREIGN KEY(world_version_id) REFERENCES world_version(id),FOREIGN KEY(snapshot_id) REFERENCES historical_snapshot(id)
);
CREATE TABLE IF NOT EXISTS branch_character_instance(
  branch_id TEXT NOT NULL,instance_id TEXT NOT NULL,PRIMARY KEY(branch_id,instance_id),
  FOREIGN KEY(branch_id) REFERENCES branch_root_binding(branch_id),FOREIGN KEY(instance_id) REFERENCES character_instance(id)
);
CREATE TABLE IF NOT EXISTS branch_character_instance_revision(
  branch_id TEXT NOT NULL,instance_id TEXT NOT NULL,instance_revision INTEGER NOT NULL,
  PRIMARY KEY(branch_id,instance_id),FOREIGN KEY(branch_id) REFERENCES branch_root_binding(branch_id),
  FOREIGN KEY(instance_id) REFERENCES character_instance(id)
);
CREATE TABLE IF NOT EXISTS branch_binding_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,branch_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS simulation_session(
  id TEXT PRIMARY KEY,branch_id TEXT NOT NULL,status TEXT NOT NULL,max_turns INTEGER NOT NULL,continuous INTEGER NOT NULL,
  no_progress_limit INTEGER NOT NULL,max_calls INTEGER NOT NULL,used_calls INTEGER NOT NULL,reserved_calls INTEGER NOT NULL,
  unknown_calls INTEGER NOT NULL,pause_requested INTEGER NOT NULL,cancel_requested INTEGER NOT NULL,revision INTEGER NOT NULL,
  created_at TEXT NOT NULL,updated_at TEXT NOT NULL,FOREIGN KEY(branch_id) REFERENCES branch(id)
);
CREATE TABLE IF NOT EXISTS simulation_actor_binding(
  session_id TEXT NOT NULL,ordinal INTEGER NOT NULL,actor_id TEXT NOT NULL,provider_connection_id TEXT NOT NULL,model_id TEXT NOT NULL,
  PRIMARY KEY(session_id,actor_id),FOREIGN KEY(session_id) REFERENCES simulation_session(id)
);
CREATE TABLE IF NOT EXISTS simulation_turn(
  id TEXT PRIMARY KEY,session_id TEXT NOT NULL,ordinal INTEGER NOT NULL,input_commit_id TEXT NOT NULL,committed_commit_id TEXT NOT NULL,
  actor_id TEXT NOT NULL,status TEXT NOT NULL,speech TEXT NOT NULL,operation TEXT NOT NULL,target_id TEXT NOT NULL,
  holder_consented INTEGER NOT NULL,ends_scene INTEGER NOT NULL,public_reason TEXT NOT NULL,draft_narration TEXT NOT NULL,
  final_narration TEXT NOT NULL,error_message TEXT NOT NULL,revision INTEGER NOT NULL,UNIQUE(session_id,ordinal),
  FOREIGN KEY(session_id) REFERENCES simulation_session(id)
);
CREATE TABLE IF NOT EXISTS simulation_provider_call(
  id TEXT PRIMARY KEY,turn_id TEXT NOT NULL UNIQUE,provider_connection_id TEXT NOT NULL,model_id TEXT NOT NULL,
  request_hash TEXT NOT NULL,status TEXT NOT NULL,input_tokens INTEGER NOT NULL,output_tokens INTEGER NOT NULL,
  failure_kind TEXT NOT NULL,created_at TEXT NOT NULL,updated_at TEXT NOT NULL,FOREIGN KEY(turn_id) REFERENCES simulation_turn(id)
);
CREATE TABLE IF NOT EXISTS simulation_command_log(
  command_id TEXT PRIMARY KEY,payload_hash TEXT NOT NULL,result_type TEXT NOT NULL,result_id TEXT NOT NULL,created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS simulation_director_intervention(
  id TEXT PRIMARY KEY,session_id TEXT NOT NULL,input_commit_id TEXT NOT NULL,committed_commit_id TEXT NOT NULL,
  actor_id TEXT NOT NULL,speech TEXT NOT NULL,operation TEXT NOT NULL,target_id TEXT NOT NULL,created_at TEXT NOT NULL,
  FOREIGN KEY(session_id) REFERENCES simulation_session(id)
);
PRAGMA user_version=22;
)SQL";
    char* message = nullptr;
    if (sqlite3_exec(database_, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message == nullptr ? "migration failed" : message;
        sqlite3_free(message);
        throw std::runtime_error(detail);
    }
}

Result<CommitView> WorkspaceRepository::ensureDemo() {
    try {
        auto active = activeBranchId();
        if (active.ok()) return loadHead(*active.value);

        Transaction transaction(database_);
        CommitView root;
        root.branch_id = "branch-main";
        root.commit_id = "commit-root";
        root.parent_commit_id = "";
        root.state = xuyan::domain::makeGreyHarborInitialState();
        root.state_hash = xuyan::domain::stateHash(root.state);
        {
            Statement branch(database_,
                "INSERT INTO branch(id,name,parent_id,fork_commit_id,head_commit_id,created_at) VALUES(?,?,?,?,?,?)");
            bindText(branch.get(), 1, root.branch_id);
            bindText(branch.get(), 2, "原始路线");
            sqlite3_bind_null(branch.get(), 3);
            bindText(branch.get(), 4, root.commit_id);
            bindText(branch.get(), 5, root.commit_id);
            bindText(branch.get(), 6, utcNow());
            if (sqlite3_step(branch.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        insertSnapshot(database_, root);
        {
            Statement meta(database_, "INSERT INTO metadata(key,value) VALUES('active_branch_id',?)");
            bindText(meta.get(), 1, root.branch_id);
            if (sqlite3_step(meta.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        transaction.commit();
        return Result<CommitView>::success(std::move(root));
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<std::string> WorkspaceRepository::activeBranchId() {
    try {
        Statement query(database_, "SELECT value FROM metadata WHERE key='active_branch_id'");
        if (sqlite3_step(query.get()) != SQLITE_ROW) {
            return Result<std::string>::failure(
                Error{ErrorCode::missing_context, "工作区尚未初始化", false, "初始化演示世界"});
        }
        return Result<std::string>::success(columnText(query.get(), 0));
    } catch (const std::exception& exception) {
        return Result<std::string>::failure(storageError(exception));
    }
}

Result<CommitView> WorkspaceRepository::loadHead(const std::string& branch_id) {
    try {
        Statement query(database_, "SELECT head_commit_id FROM branch WHERE id=?");
        bindText(query.get(), 1, branch_id);
        if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到分支：" + branch_id);
        return Result<CommitView>::success(readCommit(database_, columnText(query.get(), 0)));
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<CommitView> WorkspaceRepository::loadCommit(const std::string& commit_id) {
    try {
        return Result<CommitView>::success(readCommit(database_, commit_id));
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<std::vector<BranchInfo>> WorkspaceRepository::listBranches() {
    try {
        Statement query(database_, "SELECT id,name,COALESCE(parent_id,''),fork_commit_id,head_commit_id FROM branch ORDER BY created_at");
        std::vector<BranchInfo> branches;
        while (sqlite3_step(query.get()) == SQLITE_ROW) {
            branches.push_back({columnText(query.get(), 0), columnText(query.get(), 1), columnText(query.get(), 2),
                                columnText(query.get(), 3), columnText(query.get(), 4)});
        }
        return Result<std::vector<BranchInfo>>::success(std::move(branches));
    } catch (const std::exception& exception) {
        return Result<std::vector<BranchInfo>>::failure(storageError(exception));
    }
}

Result<std::optional<CommitView>> WorkspaceRepository::replayCommand(const std::string& command_id) {
    try {
        const auto existing = commandResult(database_, command_id);
        if (!existing.has_value()) {
            return Result<std::optional<CommitView>>::success(std::nullopt);
        }
        return Result<std::optional<CommitView>>::success(readCommit(database_, existing->second));
    } catch (const std::exception& exception) {
        return Result<std::optional<CommitView>>::failure(storageError(exception));
    }
}

Result<CommitView> WorkspaceRepository::commitStep(const std::string& command_id, const std::string& payload_hash,
                                                    const CommitView& expected, const ScenarioState& next_state) {
    try {
        Transaction transaction(database_);
        if (const auto existing = commandResult(database_, command_id); existing.has_value()) {
            if (existing->first != payload_hash) {
                return Result<CommitView>::failure(
                    Error{ErrorCode::command_conflict, "相同 command_id 携带了不同负载", false, "生成新的命令ID"});
            }
            auto view = readCommit(database_, existing->second);
            transaction.commit();
            return Result<CommitView>::success(std::move(view));
        }
        Statement head(database_, "SELECT head_commit_id FROM branch WHERE id=?");
        bindText(head.get(), 1, expected.branch_id);
        if (sqlite3_step(head.get()) != SQLITE_ROW || columnText(head.get(), 0) != expected.commit_id) {
            return Result<CommitView>::failure(
                Error{ErrorCode::revision_conflict, "分支头已经改变", true, "刷新分支后重新校验行动"});
        }
        CommitView next;
        next.branch_id = expected.branch_id;
        next.commit_id = randomId("commit");
        next.parent_commit_id = expected.commit_id;
        next.state = next_state;
        next.state_hash = xuyan::domain::stateHash(next.state);
        insertSnapshot(database_, next);
        Statement update(database_, "UPDATE branch SET head_commit_id=? WHERE id=? AND head_commit_id=?");
        bindText(update.get(), 1, next.commit_id);
        bindText(update.get(), 2, next.branch_id);
        bindText(update.get(), 3, expected.commit_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) {
            throw std::runtime_error("更新分支头失败");
        }
        recordCommand(database_, command_id, payload_hash, next.commit_id);
        transaction.commit();
        return Result<CommitView>::success(std::move(next));
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<CommitView> WorkspaceRepository::setPaused(const std::string& command_id, const CommitView& expected, bool paused) {
    auto state = expected.state;
    state.paused = paused;
    ++state.revision;
    return commitStep(command_id, std::string{"pause:"} + (paused ? "1:" : "0:") + expected.commit_id,
                      expected, state);
}

Result<CommitView> WorkspaceRepository::forkBranch(const std::string& command_id, const std::string& source_commit_id,
                                                   const std::string& branch_name) {
    try {
        Transaction transaction(database_);
        const auto payload_hash = "fork:" + source_commit_id + ':' + branch_name;
        if (const auto existing = commandResult(database_, command_id); existing.has_value()) {
            if (existing->first != payload_hash) {
                return Result<CommitView>::failure(
                    Error{ErrorCode::command_conflict, "相同 command_id 被用于不同分支操作", false, "生成新的命令ID"});
            }
            auto view = readCommit(database_, existing->second);
            transaction.commit();
            return Result<CommitView>::success(std::move(view));
        }
        const auto source = readCommit(database_, source_commit_id);
        CommitView root = source;
        root.branch_id = randomId("branch");
        root.commit_id = randomId("commit");
        root.parent_commit_id = source_commit_id;
        root.state.paused = false;
        ++root.state.revision;
        root.state_hash = xuyan::domain::stateHash(root.state);
        Statement insert(database_,
            "INSERT INTO branch(id,name,parent_id,fork_commit_id,head_commit_id,created_at) VALUES(?,?,?,?,?,?)");
        bindText(insert.get(), 1, root.branch_id);
        bindText(insert.get(), 2, branch_name);
        bindText(insert.get(), 3, source.branch_id);
        bindText(insert.get(), 4, source_commit_id);
        bindText(insert.get(), 5, root.commit_id);
        bindText(insert.get(), 6, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        insertSnapshot(database_, root);
        Statement meta(database_, "UPDATE metadata SET value=? WHERE key='active_branch_id'");
        bindText(meta.get(), 1, root.branch_id);
        if (sqlite3_step(meta.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        recordCommand(database_, command_id, payload_hash, root.commit_id);
        transaction.commit();
        return Result<CommitView>::success(std::move(root));
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<CommitView> WorkspaceRepository::switchBranch(const std::string& branch_id) {
    try {
        Transaction transaction(database_);
        auto head = loadHead(branch_id);
        if (!head.ok()) return head;
        Statement meta(database_, "UPDATE metadata SET value=? WHERE key='active_branch_id'");
        bindText(meta.get(), 1, branch_id);
        if (sqlite3_step(meta.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return head;
    } catch (const std::exception& exception) {
        return Result<CommitView>::failure(storageError(exception));
    }
}

Result<std::string> WorkspaceRepository::backupTo(const std::filesystem::path& destination) {
    sqlite3* target = nullptr;
    try {
        if (!destination.parent_path().empty()) {
            std::filesystem::create_directories(destination.parent_path());
        }
        const auto utf8 = destination.u8string();
        if (sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &target) != SQLITE_OK) {
            throw std::runtime_error(target == nullptr ? "无法创建备份数据库" : sqlite3_errmsg(target));
        }
        sqlite3_backup* backup = sqlite3_backup_init(target, "main", database_, "main");
        if (backup == nullptr) throw std::runtime_error(sqlite3_errmsg(target));
        const int step = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if ((step != SQLITE_DONE && step != SQLITE_OK) || finish != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(target));
        }
        if (sqlite3_close(target) != SQLITE_OK) throw std::runtime_error("无法关闭备份数据库");
        target = nullptr;
        return Result<std::string>::success(destination.string());
    } catch (const std::exception& exception) {
        if (target != nullptr) sqlite3_close(target);
        return Result<std::string>::failure(storageError(exception));
    }
}

Result<WorldEntity> WorkspaceRepository::createEntity(const std::string& command_id, WorldEntity entity) {
    auto checked = xuyan::domain::validateEntity(std::move(entity));
    if (!checked.ok()) return checked;
    entity = std::move(*checked.value);
    const auto payload = entityPayload(entity, 0, "create");
    try {
        Transaction transaction(database_);
        if (const auto existing = entityCommandResult(database_, command_id); existing.has_value()) {
            if (std::get<0>(*existing) != payload) {
                return Result<WorldEntity>::failure(
                    Error{ErrorCode::command_conflict, "相同 command_id 携带了不同条目负载", false, "生成新的命令ID"});
            }
            auto replay = readEntityRevision(database_, std::get<1>(*existing), std::get<2>(*existing));
            transaction.commit();
            return Result<WorldEntity>::success(std::move(replay));
        }
        if (entity.id.empty()) entity.id = randomId("entity");
        entity.revision = 1;
        entity.deleted = false;
        Statement insert(database_,
            "INSERT INTO world_entity(id,world_id,head_revision,deleted,created_at) VALUES(?,?,1,0,?)");
        bindText(insert.get(), 1, entity.id);
        bindText(insert.get(), 2, entity.world_id);
        bindText(insert.get(), 3, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        insertEntityRevision(database_, entity);
        recordEntityCommand(database_, command_id, payload, entity);
        transaction.commit();
        return Result<WorldEntity>::success(std::move(entity));
    } catch (const std::exception& exception) {
        return Result<WorldEntity>::failure(storageError(exception));
    }
}

Result<WorldEntity> WorkspaceRepository::saveEntity(const std::string& command_id, WorldEntity entity,
                                                     int expected_revision) {
    auto checked = xuyan::domain::validateEntity(std::move(entity));
    if (!checked.ok()) return checked;
    entity = std::move(*checked.value);
    const auto payload = entityPayload(entity, expected_revision, "save");
    try {
        Transaction transaction(database_);
        if (const auto existing = entityCommandResult(database_, command_id); existing.has_value()) {
            if (std::get<0>(*existing) != payload) {
                return Result<WorldEntity>::failure(
                    Error{ErrorCode::command_conflict, "相同 command_id 携带了不同条目负载", false, "生成新的命令ID"});
            }
            auto replay = readEntityRevision(database_, std::get<1>(*existing), std::get<2>(*existing));
            transaction.commit();
            return Result<WorldEntity>::success(std::move(replay));
        }
        Statement head(database_, "SELECT head_revision FROM world_entity WHERE id=?");
        bindText(head.get(), 1, entity.id);
        if (sqlite3_step(head.get()) != SQLITE_ROW) {
            return Result<WorldEntity>::failure(
                Error{ErrorCode::missing_context, "要保存的条目不存在", false, "刷新条目列表"});
        }
        const auto current_revision = sqlite3_column_int(head.get(), 0);
        if (current_revision != expected_revision) {
            return Result<WorldEntity>::failure(
                Error{ErrorCode::revision_conflict,
                      "条目已被其他编辑修改；当前修订为 " + std::to_string(current_revision), false,
                      "重新加载并比较修订后再保存"});
        }
        entity.revision = current_revision + 1;
        insertEntityRevision(database_, entity);
        Statement update(database_,
            "UPDATE world_entity SET head_revision=?,deleted=? WHERE id=? AND head_revision=?");
        sqlite3_bind_int(update.get(), 1, entity.revision);
        sqlite3_bind_int(update.get(), 2, entity.deleted ? 1 : 0);
        bindText(update.get(), 3, entity.id);
        sqlite3_bind_int(update.get(), 4, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) {
            throw std::runtime_error("更新条目修订头失败");
        }
        recordEntityCommand(database_, command_id, payload, entity);
        transaction.commit();
        return Result<WorldEntity>::success(std::move(entity));
    } catch (const std::exception& exception) {
        return Result<WorldEntity>::failure(storageError(exception));
    }
}

Result<WorldEntity> WorkspaceRepository::loadEntity(const std::string& entity_id) {
    try {
        return Result<WorldEntity>::success(readEntityRevision(database_, entity_id));
    } catch (const std::exception& exception) {
        return Result<WorldEntity>::failure(storageError(exception));
    }
}

Result<EntityPage> WorkspaceRepository::searchEntities(const std::string& query, const std::string& kind,
                                                       int offset, int limit) {
    if (offset < 0 || limit < 1 || limit > 200) {
        return Result<EntityPage>::failure(
            Error{ErrorCode::validation_failed, "分页参数无效", false, "使用非负 offset 及 1—200 的 limit"});
    }
    try {
        const auto pattern = '%' + query + '%';
        const bool filter_kind = !kind.empty();
        const auto condition = std::string{
            " FROM world_entity e JOIN entity_revision r ON r.entity_id=e.id AND r.revision=e.head_revision "
            "WHERE e.deleted=0 AND (?='' OR r.name LIKE ? OR r.aliases LIKE ? OR r.tags LIKE ? OR r.description LIKE ?) "}
            + (filter_kind ? "AND r.kind=? " : "");
        Statement count(database_, ("SELECT COUNT(*)" + condition).c_str());
        bindText(count.get(), 1, query);
        for (int index = 2; index <= 5; ++index) bindText(count.get(), index, pattern);
        if (filter_kind) bindText(count.get(), 6, kind);
        if (sqlite3_step(count.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(database_));
        EntityPage page;
        page.offset = offset;
        page.total = sqlite3_column_int(count.get(), 0);

        const auto sql = "SELECT e.id,e.world_id,r.kind,r.name,r.aliases,r.tags,r.description,r.attributes_json,"
            "r.review_status,r.revision,r.deleted" + condition + "ORDER BY r.name COLLATE NOCASE,e.id LIMIT ? OFFSET ?";
        Statement list(database_, sql.c_str());
        bindText(list.get(), 1, query);
        for (int index = 2; index <= 5; ++index) bindText(list.get(), index, pattern);
        int parameter = 6;
        if (filter_kind) bindText(list.get(), parameter++, kind);
        sqlite3_bind_int(list.get(), parameter++, limit);
        sqlite3_bind_int(list.get(), parameter, offset);
        while (sqlite3_step(list.get()) == SQLITE_ROW) {
            WorldEntity entity;
            entity.id = columnText(list.get(), 0);
            entity.world_id = columnText(list.get(), 1);
            entity.kind = columnText(list.get(), 2);
            entity.name = columnText(list.get(), 3);
            entity.aliases = splitValues(columnText(list.get(), 4));
            entity.tags = splitValues(columnText(list.get(), 5));
            entity.description = columnText(list.get(), 6);
            entity.attributes_json = columnText(list.get(), 7);
            entity.review_status = columnText(list.get(), 8);
            entity.revision = sqlite3_column_int(list.get(), 9);
            entity.deleted = sqlite3_column_int(list.get(), 10) != 0;
            page.items.push_back(std::move(entity));
        }
        page.has_more = page.offset + static_cast<int>(page.items.size()) < page.total;
        return Result<EntityPage>::success(std::move(page));
    } catch (const std::exception& exception) {
        return Result<EntityPage>::failure(storageError(exception));
    }
}

Result<WorldEntity> WorkspaceRepository::deleteEntity(const std::string& command_id, const std::string& entity_id,
                                                       int expected_revision) {
    auto entity = loadEntity(entity_id);
    if (!entity.ok()) return entity;
    entity.value->deleted = true;
    return saveEntity(command_id, std::move(*entity.value), expected_revision);
}

Result<xuyan::domain::EntityMergeResult> WorkspaceRepository::mergeEntities(
    const std::string& command_id, const std::string& source_id, int source_expected_revision,
    const std::string& target_id, int target_expected_revision) {
    using MergeResult = xuyan::domain::EntityMergeResult;
    if (source_id.empty() || target_id.empty() || source_id == target_id) return Result<MergeResult>::failure(
        {ErrorCode::validation_failed, "合并源和目标必须是不同的有效条目", false, "重新选择两个条目"});
    const auto payload = "merge|" + source_id + '|' + std::to_string(source_expected_revision) + '|'
        + target_id + '|' + std::to_string(target_expected_revision);
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,merge_id FROM entity_merge_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<MergeResult>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他合并", false, "生成新的命令标识"});
                const auto existing_merge = columnText(replay.get(), 1);
                Statement info(database_, "SELECT source_id,target_id,active FROM entity_merge WHERE id=?");
                bindText(info.get(), 1, existing_merge);
                if (sqlite3_step(info.get()) != SQLITE_ROW) throw std::runtime_error("合并命令结果缺失");
                MergeResult result{existing_merge, readEntityRevision(database_, columnText(info.get(), 0)),
                                   readEntityRevision(database_, columnText(info.get(), 1)), sqlite3_column_int(info.get(), 2) != 0};
                transaction.commit(); return Result<MergeResult>::success(std::move(result));
            }
        }
        auto source = readEntityRevision(database_, source_id);
        auto target = readEntityRevision(database_, target_id);
        if (source.deleted || target.deleted || source.revision != source_expected_revision
            || target.revision != target_expected_revision) return Result<MergeResult>::failure(
            {ErrorCode::revision_conflict, "合并条目已被修改或删除", false, "刷新两个条目后重试"});
        if (source.world_id != target.world_id || source.kind != target.kind) return Result<MergeResult>::failure(
            {ErrorCode::validation_failed, "只能合并同一世界且类型相同的条目", false, "选择同类重复条目"});

        const auto source_previous = source.revision; const auto target_previous = target.revision;
        if (source.name != target.name) target.aliases.push_back(source.name);
        target.aliases.insert(target.aliases.end(), source.aliases.begin(), source.aliases.end());
        target.revision += 1;
        auto valid_target = xuyan::domain::validateEntity(std::move(target));
        if (!valid_target.ok()) return Result<MergeResult>::failure(*valid_target.error);
        target = std::move(*valid_target.value);
        source.deleted = true; source.revision += 1;
        insertEntityRevision(database_, target); updateEntityHead(database_, target);
        insertEntityRevision(database_, source); updateEntityHead(database_, source);

        const auto merge_id = randomId("merge");
        std::vector<std::string> evidence_ids;
        Statement evidence(database_, "SELECT id FROM evidence_reference WHERE entity_id=?");
        bindText(evidence.get(), 1, source_id);
        while (sqlite3_step(evidence.get()) == SQLITE_ROW) evidence_ids.push_back(columnText(evidence.get(), 0));
        for (const auto& evidence_id : evidence_ids) {
            Statement rewrite(database_, "INSERT INTO evidence_entity_rewrite(merge_id,evidence_id,from_entity_id,to_entity_id) VALUES(?,?,?,?)");
            bindText(rewrite.get(), 1, merge_id); bindText(rewrite.get(), 2, evidence_id);
            bindText(rewrite.get(), 3, source_id); bindText(rewrite.get(), 4, target_id);
            if (sqlite3_step(rewrite.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            Statement update(database_, "UPDATE evidence_reference SET entity_id=? WHERE id=? AND entity_id=?");
            bindText(update.get(), 1, target_id); bindText(update.get(), 2, evidence_id); bindText(update.get(), 3, source_id);
            if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1)
                throw std::runtime_error("证据引用合并失败");
        }
        Statement history(database_, "INSERT INTO entity_merge(id,source_id,target_id,source_previous_revision,target_previous_revision,source_merged_revision,target_merged_revision,active,created_at) VALUES(?,?,?,?,?,?,?,?,?)");
        bindText(history.get(), 1, merge_id); bindText(history.get(), 2, source_id); bindText(history.get(), 3, target_id);
        sqlite3_bind_int(history.get(), 4, source_previous); sqlite3_bind_int(history.get(), 5, target_previous);
        sqlite3_bind_int(history.get(), 6, source.revision); sqlite3_bind_int(history.get(), 7, target.revision);
        sqlite3_bind_int(history.get(), 8, 1); bindText(history.get(), 9, utcNow());
        if (sqlite3_step(history.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement log(database_, "INSERT INTO entity_merge_command_log(command_id,payload_hash,merge_id,created_at) VALUES(?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, merge_id); bindText(log.get(), 4, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<MergeResult>::success({merge_id, std::move(source), std::move(target), true});
    } catch (const std::exception& exception) { return Result<MergeResult>::failure(storageError(exception)); }
}

Result<xuyan::domain::EntityMergeResult> WorkspaceRepository::splitEntityMerge(
    const std::string& command_id, const std::string& merge_id,
    int source_expected_revision, int target_expected_revision) {
    using MergeResult = xuyan::domain::EntityMergeResult;
    const auto payload = "split|" + merge_id + '|' + std::to_string(source_expected_revision) + '|'
        + std::to_string(target_expected_revision);
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash FROM entity_merge_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<MergeResult>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他拆分", false, "生成新的命令标识"});
                Statement replay_info(database_, "SELECT source_id,target_id,active FROM entity_merge WHERE id=?");
                bindText(replay_info.get(), 1, merge_id);
                if (sqlite3_step(replay_info.get()) != SQLITE_ROW) throw std::runtime_error("拆分命令结果缺失");
                MergeResult result{merge_id, readEntityRevision(database_, columnText(replay_info.get(), 0)),
                                   readEntityRevision(database_, columnText(replay_info.get(), 1)), sqlite3_column_int(replay_info.get(), 2) != 0};
                transaction.commit(); return Result<MergeResult>::success(std::move(result));
            }
        }
        Statement info(database_, "SELECT source_id,target_id,source_previous_revision,target_previous_revision,active,source_merged_revision,target_merged_revision FROM entity_merge WHERE id=?");
        bindText(info.get(), 1, merge_id);
        if (sqlite3_step(info.get()) != SQLITE_ROW) return Result<MergeResult>::failure(
            {ErrorCode::missing_context, "找不到合并记录", false, "刷新合并历史"});
        const auto source_id = columnText(info.get(), 0); const auto target_id = columnText(info.get(), 1);
        const auto source_previous = sqlite3_column_int(info.get(), 2); const auto target_previous = sqlite3_column_int(info.get(), 3);
        if (sqlite3_column_int(info.get(), 4) == 0) return Result<MergeResult>::failure(
            {ErrorCode::revision_conflict, "该合并已经拆分", false, "刷新条目"});
        if (source_expected_revision < 0) source_expected_revision = sqlite3_column_int(info.get(), 5);
        if (target_expected_revision < 0) target_expected_revision = sqlite3_column_int(info.get(), 6);
        const auto source_current = readEntityRevision(database_, source_id);
        const auto target_current = readEntityRevision(database_, target_id);
        if (source_current.revision != source_expected_revision || target_current.revision != target_expected_revision)
            return Result<MergeResult>::failure(
                {ErrorCode::revision_conflict, "合并后条目已有新修订，不能自动拆分", false, "人工比较修订后处理"});
        auto source = readEntityRevision(database_, source_id, source_previous);
        auto target = readEntityRevision(database_, target_id, target_previous);
        source.revision = source_current.revision + 1; source.deleted = false;
        target.revision = target_current.revision + 1; target.deleted = false;
        insertEntityRevision(database_, source); updateEntityHead(database_, source);
        insertEntityRevision(database_, target); updateEntityHead(database_, target);

        Statement rewrites(database_, "SELECT evidence_id,from_entity_id,to_entity_id FROM evidence_entity_rewrite WHERE merge_id=?");
        bindText(rewrites.get(), 1, merge_id);
        std::vector<std::tuple<std::string,std::string,std::string>> mappings;
        while (sqlite3_step(rewrites.get()) == SQLITE_ROW)
            mappings.emplace_back(columnText(rewrites.get(), 0), columnText(rewrites.get(), 1), columnText(rewrites.get(), 2));
        for (const auto& [evidence_id, from_id, to_id] : mappings) {
            Statement update(database_, "UPDATE evidence_reference SET entity_id=? WHERE id=? AND entity_id=?");
            bindText(update.get(), 1, from_id); bindText(update.get(), 2, evidence_id); bindText(update.get(), 3, to_id);
            if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1)
                throw std::runtime_error("证据引用拆分失败");
        }
        Statement history(database_, "UPDATE entity_merge SET active=0,source_split_revision=?,target_split_revision=?,split_at=? WHERE id=? AND active=1");
        sqlite3_bind_int(history.get(), 1, source.revision); sqlite3_bind_int(history.get(), 2, target.revision);
        bindText(history.get(), 3, utcNow()); bindText(history.get(), 4, merge_id);
        if (sqlite3_step(history.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("合并历史拆分失败");
        Statement log(database_, "INSERT INTO entity_merge_command_log(command_id,payload_hash,merge_id,created_at) VALUES(?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, merge_id); bindText(log.get(), 4, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<MergeResult>::success({merge_id, std::move(source), std::move(target), false});
    } catch (const std::exception& exception) { return Result<MergeResult>::failure(storageError(exception)); }
}

Result<SourceDocument> WorkspaceRepository::saveSource(const std::string& command_id,
                                                       const SourceDocument& document) {
    const auto payload = document.sha256 + '|' + document.name + '|' + document.edition;
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,source_id FROM source_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) {
                    return Result<SourceDocument>::failure(
                        Error{ErrorCode::command_conflict, "相同 command_id 携带了不同来源文件", false, "生成新的命令ID"});
                }
                auto result = readSource(database_, columnText(replay.get(), 1));
                transaction.commit();
                return Result<SourceDocument>::success(std::move(result));
            }
        }
        {
            Statement existing(database_, "SELECT id FROM source_document WHERE id=?");
            bindText(existing.get(), 1, document.id);
            if (sqlite3_step(existing.get()) == SQLITE_ROW) {
                Statement record(database_,
                    "INSERT INTO source_command_log(command_id,payload_hash,source_id,created_at) VALUES(?,?,?,?)");
                bindText(record.get(), 1, command_id);
                bindText(record.get(), 2, payload);
                bindText(record.get(), 3, document.id);
                bindText(record.get(), 4, utcNow());
                if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
                auto result = readSource(database_, document.id);
                transaction.commit();
                return Result<SourceDocument>::success(std::move(result));
            }
        }
        Statement insert(database_,
            "INSERT INTO source_document(id,world_id,name,sha256,original_asset_ref,normalized_asset_ref,edition,created_at) "
            "VALUES(?,?,?,?,?,?,?,?)");
        bindText(insert.get(), 1, document.id);
        bindText(insert.get(), 2, document.world_id);
        bindText(insert.get(), 3, document.name);
        bindText(insert.get(), 4, document.sha256);
        bindText(insert.get(), 5, document.original_asset_ref);
        bindText(insert.get(), 6, document.normalized_asset_ref);
        bindText(insert.get(), 7, document.edition);
        bindText(insert.get(), 8, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        {
            Statement encoding(database_, "INSERT INTO source_encoding(source_id,encoding) VALUES(?,?)");
            bindText(encoding.get(), 1, document.id); bindText(encoding.get(), 2, document.detected_encoding);
            if (sqlite3_step(encoding.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        {
            Statement head(database_, "INSERT INTO source_chapter_head(source_id,revision) VALUES(?,1)");
            bindText(head.get(), 1, document.id);
            if (sqlite3_step(head.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        for (const auto& chapter : document.chapters) {
            Statement chapter_insert(database_,
                "INSERT INTO source_chapter(id,document_id,title,ordinal,start_byte,end_byte,start_codepoint,end_codepoint) "
                "VALUES(?,?,?,?,?,?,?,?)");
            bindText(chapter_insert.get(), 1, chapter.id);
            bindText(chapter_insert.get(), 2, document.id);
            bindText(chapter_insert.get(), 3, chapter.title);
            sqlite3_bind_int(chapter_insert.get(), 4, chapter.ordinal);
            sqlite3_bind_int64(chapter_insert.get(), 5, static_cast<sqlite3_int64>(chapter.start_byte));
            sqlite3_bind_int64(chapter_insert.get(), 6, static_cast<sqlite3_int64>(chapter.end_byte));
            sqlite3_bind_int64(chapter_insert.get(), 7, static_cast<sqlite3_int64>(chapter.start_codepoint));
            sqlite3_bind_int64(chapter_insert.get(), 8, static_cast<sqlite3_int64>(chapter.end_codepoint));
            if (sqlite3_step(chapter_insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement record(database_,
            "INSERT INTO source_command_log(command_id,payload_hash,source_id,created_at) VALUES(?,?,?,?)");
        bindText(record.get(), 1, command_id);
        bindText(record.get(), 2, payload);
        bindText(record.get(), 3, document.id);
        bindText(record.get(), 4, utcNow());
        if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<SourceDocument>::success(document);
    } catch (const std::exception& exception) {
        return Result<SourceDocument>::failure(storageError(exception));
    }
}

Result<std::vector<SourceDocument>> WorkspaceRepository::listSources() {
    try {
        Statement query(database_, "SELECT id FROM source_document ORDER BY created_at,id");
        std::vector<SourceDocument> documents;
        while (sqlite3_step(query.get()) == SQLITE_ROW) {
            documents.push_back(readSource(database_, columnText(query.get(), 0)));
        }
        return Result<std::vector<SourceDocument>>::success(std::move(documents));
    } catch (const std::exception& exception) {
        return Result<std::vector<SourceDocument>>::failure(storageError(exception));
    }
}

Result<SourceDocument> WorkspaceRepository::loadSource(const std::string& source_id) {
    try {
        return Result<SourceDocument>::success(readSource(database_, source_id));
    } catch (const std::exception& exception) {
        return Result<SourceDocument>::failure(storageError(exception));
    }
}

Result<SourceDocument> WorkspaceRepository::replaceSourceChapters(
    const std::string& command_id, const std::string& source_id, int expected_revision,
    std::vector<SourceChapter> chapters) {
    std::ostringstream payload_builder;
    payload_builder << source_id << '|' << expected_revision;
    for (const auto& chapter : chapters) payload_builder << '|' << chapter.id << '|' << chapter.title << '|'
        << chapter.ordinal << '|' << chapter.start_byte << '|' << chapter.end_byte << '|'
        << chapter.start_codepoint << '|' << chapter.end_codepoint;
    const auto payload = payload_builder.str();
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,source_id FROM source_chapter_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<SourceDocument>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他章节布局", false, "生成新的命令标识"});
                auto result = readSource(database_, columnText(replay.get(), 1)); transaction.commit();
                return Result<SourceDocument>::success(std::move(result));
            }
        }
        Statement head(database_, "SELECT revision FROM source_chapter_head WHERE source_id=?");
        bindText(head.get(), 1, source_id);
        if (sqlite3_step(head.get()) != SQLITE_ROW) return Result<SourceDocument>::failure(
            {ErrorCode::missing_context, "找不到来源章节修订", false, "刷新来源列表"});
        if (sqlite3_column_int(head.get(), 0) != expected_revision) return Result<SourceDocument>::failure(
            {ErrorCode::revision_conflict, "章节布局已被其他编辑修改", false, "刷新章节后重试"});
        Statement remove(database_, "DELETE FROM source_chapter WHERE document_id=?");
        bindText(remove.get(), 1, source_id);
        if (sqlite3_step(remove.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& chapter : chapters) {
            Statement insert(database_, "INSERT INTO source_chapter(id,document_id,title,ordinal,start_byte,end_byte,start_codepoint,end_codepoint) VALUES(?,?,?,?,?,?,?,?)");
            bindText(insert.get(), 1, chapter.id); bindText(insert.get(), 2, source_id); bindText(insert.get(), 3, chapter.title);
            sqlite3_bind_int(insert.get(), 4, chapter.ordinal);
            sqlite3_bind_int64(insert.get(), 5, static_cast<sqlite3_int64>(chapter.start_byte));
            sqlite3_bind_int64(insert.get(), 6, static_cast<sqlite3_int64>(chapter.end_byte));
            sqlite3_bind_int64(insert.get(), 7, static_cast<sqlite3_int64>(chapter.start_codepoint));
            sqlite3_bind_int64(insert.get(), 8, static_cast<sqlite3_int64>(chapter.end_codepoint));
            if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        const auto revision = expected_revision + 1;
        Statement update(database_, "UPDATE source_chapter_head SET revision=? WHERE source_id=? AND revision=?");
        sqlite3_bind_int(update.get(), 1, revision); bindText(update.get(), 2, source_id); sqlite3_bind_int(update.get(), 3, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("章节修订并发更新失败");
        Statement log(database_, "INSERT INTO source_chapter_command_log(command_id,payload_hash,source_id,revision,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, source_id);
        sqlite3_bind_int(log.get(), 4, revision); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSource(database_, source_id); transaction.commit();
        return Result<SourceDocument>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<SourceDocument>::failure(storageError(exception)); }
}

Result<CharacterBlueprint> WorkspaceRepository::createBlueprint(const std::string& command_id,
                                                                 CharacterBlueprint blueprint) {
    auto checked = xuyan::domain::validateBlueprint(std::move(blueprint));
    if (!checked.ok()) return checked;
    blueprint = std::move(*checked.value);
    const auto payload = blueprintPayload(blueprint, 0, "create");
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_,
                "SELECT payload_hash,blueprint_id,version FROM blueprint_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) {
                    return Result<CharacterBlueprint>::failure(
                        Error{ErrorCode::command_conflict, "相同 command_id 携带了不同人物卡", false, "生成新的命令ID"});
                }
                auto existing = readBlueprint(database_, columnText(replay.get(), 1), sqlite3_column_int(replay.get(), 2));
                transaction.commit();
                return Result<CharacterBlueprint>::success(std::move(existing));
            }
        }
        if (blueprint.id.empty()) blueprint.id = randomId("blueprint");
        blueprint.version = 1;
        blueprint.deleted = false;
        Statement insert(database_,
            "INSERT INTO character_blueprint(id,head_version,deleted,created_at) VALUES(?,1,0,?)");
        bindText(insert.get(), 1, blueprint.id);
        bindText(insert.get(), 2, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        insertBlueprintVersion(database_, blueprint);
        Statement record(database_,
            "INSERT INTO blueprint_command_log(command_id,payload_hash,blueprint_id,version,created_at) VALUES(?,?,?,?,?)");
        bindText(record.get(), 1, command_id);
        bindText(record.get(), 2, payload);
        bindText(record.get(), 3, blueprint.id);
        sqlite3_bind_int(record.get(), 4, blueprint.version);
        bindText(record.get(), 5, utcNow());
        if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<CharacterBlueprint>::success(std::move(blueprint));
    } catch (const std::exception& exception) {
        return Result<CharacterBlueprint>::failure(storageError(exception));
    }
}

Result<CharacterBlueprint> WorkspaceRepository::saveBlueprint(const std::string& command_id,
                                                               CharacterBlueprint blueprint,
                                                               int expected_version) {
    auto checked = xuyan::domain::validateBlueprint(std::move(blueprint));
    if (!checked.ok()) return checked;
    blueprint = std::move(*checked.value);
    const auto payload = blueprintPayload(blueprint, expected_version, "save");
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_,
                "SELECT payload_hash,blueprint_id,version FROM blueprint_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) {
                    return Result<CharacterBlueprint>::failure(
                        Error{ErrorCode::command_conflict, "相同 command_id 携带了不同人物卡修订", false, "生成新的命令ID"});
                }
                auto existing = readBlueprint(database_, columnText(replay.get(), 1), sqlite3_column_int(replay.get(), 2));
                transaction.commit();
                return Result<CharacterBlueprint>::success(std::move(existing));
            }
        }
        Statement head(database_, "SELECT head_version FROM character_blueprint WHERE id=?");
        bindText(head.get(), 1, blueprint.id);
        if (sqlite3_step(head.get()) != SQLITE_ROW) {
            return Result<CharacterBlueprint>::failure(
                Error{ErrorCode::missing_context, "人物卡不存在", false, "刷新人物卡列表"});
        }
        const auto current = sqlite3_column_int(head.get(), 0);
        if (current != expected_version) {
            return Result<CharacterBlueprint>::failure(
                Error{ErrorCode::revision_conflict,
                      "人物卡已产生新版本；当前版本为 " + std::to_string(current), false,
                      "比较版本后重新保存"});
        }
        blueprint.version = current + 1;
        insertBlueprintVersion(database_, blueprint);
        Statement update(database_,
            "UPDATE character_blueprint SET head_version=?,deleted=? WHERE id=? AND head_version=?");
        sqlite3_bind_int(update.get(), 1, blueprint.version);
        sqlite3_bind_int(update.get(), 2, blueprint.deleted ? 1 : 0);
        bindText(update.get(), 3, blueprint.id);
        sqlite3_bind_int(update.get(), 4, expected_version);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) {
            throw std::runtime_error("更新人物卡版本头失败");
        }
        Statement record(database_,
            "INSERT INTO blueprint_command_log(command_id,payload_hash,blueprint_id,version,created_at) VALUES(?,?,?,?,?)");
        bindText(record.get(), 1, command_id);
        bindText(record.get(), 2, payload);
        bindText(record.get(), 3, blueprint.id);
        sqlite3_bind_int(record.get(), 4, blueprint.version);
        bindText(record.get(), 5, utcNow());
        if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<CharacterBlueprint>::success(std::move(blueprint));
    } catch (const std::exception& exception) {
        return Result<CharacterBlueprint>::failure(storageError(exception));
    }
}

Result<CharacterBlueprint> WorkspaceRepository::loadBlueprint(const std::string& blueprint_id, int version) {
    try {
        return Result<CharacterBlueprint>::success(readBlueprint(database_, blueprint_id, version));
    } catch (const std::exception& exception) {
        return Result<CharacterBlueprint>::failure(storageError(exception));
    }
}

Result<std::vector<CharacterBlueprint>> WorkspaceRepository::listBlueprints() {
    try {
        Statement query(database_, "SELECT id FROM character_blueprint WHERE deleted=0 ORDER BY created_at,id");
        std::vector<CharacterBlueprint> result;
        while (sqlite3_step(query.get()) == SQLITE_ROW) result.push_back(readBlueprint(database_, columnText(query.get(), 0)));
        return Result<std::vector<CharacterBlueprint>>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<std::vector<CharacterBlueprint>>::failure(storageError(exception));
    }
}

Result<int> WorkspaceRepository::importEntities(const std::string& command_id, const std::string& package_hash,
                                                std::vector<WorldEntity> entities) {
    std::set<std::string> ids;
    for (auto& entity : entities) {
        auto checked = xuyan::domain::validateEntity(std::move(entity));
        if (!checked.ok()) return Result<int>::failure(*checked.error);
        entity = std::move(*checked.value);
        if (entity.id.empty() || entity.revision < 1 || !ids.insert(entity.id).second) {
            return Result<int>::failure(
                Error{ErrorCode::validation_failed, "导入包包含空、重复 ID 或无效修订", false, "修复包内容"});
        }
    }
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT package_hash,imported_count FROM package_import_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != package_hash) {
                    return Result<int>::failure(
                        Error{ErrorCode::command_conflict, "相同 command_id 指向不同导入包", false, "生成新的命令ID"});
                }
                const auto count = sqlite3_column_int(replay.get(), 1);
                transaction.commit();
                return Result<int>::success(count);
            }
        }
        for (const auto& entity : entities) {
            Statement exists(database_, "SELECT 1 FROM world_entity WHERE id=?");
            bindText(exists.get(), 1, entity.id);
            if (sqlite3_step(exists.get()) == SQLITE_ROW) {
                return Result<int>::failure(
                    Error{ErrorCode::revision_conflict, "目标工作区已存在实体：" + entity.id, false,
                          "导入到空工作区或先解决 ID 冲突"});
            }
        }
        for (const auto& entity : entities) {
            Statement insert(database_,
                "INSERT INTO world_entity(id,world_id,head_revision,deleted,created_at) VALUES(?,?,?,?,?)");
            bindText(insert.get(), 1, entity.id);
            bindText(insert.get(), 2, entity.world_id);
            sqlite3_bind_int(insert.get(), 3, entity.revision);
            sqlite3_bind_int(insert.get(), 4, entity.deleted ? 1 : 0);
            bindText(insert.get(), 5, utcNow());
            if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            insertEntityRevision(database_, entity);
        }
        Statement record(database_,
            "INSERT INTO package_import_log(command_id,package_hash,imported_count,created_at) VALUES(?,?,?,?)");
        bindText(record.get(), 1, command_id);
        bindText(record.get(), 2, package_hash);
        sqlite3_bind_int(record.get(), 3, static_cast<int>(entities.size()));
        bindText(record.get(), 4, utcNow());
        if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<int>::success(static_cast<int>(entities.size()));
    } catch (const std::exception& exception) {
        return Result<int>::failure(storageError(exception));
    }
}

Result<int> WorkspaceRepository::importBlueprintVersions(const std::string& command_id,
                                                         const std::string& package_hash,
                                                         std::vector<CharacterBlueprint> versions) {
    if (versions.empty()) return Result<int>::failure(
        Error{ErrorCode::validation_failed, "人物包不包含任何版本", false, "修复人物包"});
    const auto blueprint_id = versions.front().id;
    if (blueprint_id.empty()) return Result<int>::failure(
        Error{ErrorCode::validation_failed, "人物包缺少稳定 ID", false, "修复人物包"});
    std::sort(versions.begin(), versions.end(), [](const auto& left, const auto& right) { return left.version < right.version; });
    for (std::size_t index = 0; index < versions.size(); ++index) {
        auto checked = xuyan::domain::validateBlueprint(std::move(versions[index]));
        if (!checked.ok()) return Result<int>::failure(*checked.error);
        versions[index] = std::move(*checked.value);
        if (versions[index].id != blueprint_id || versions[index].version != static_cast<int>(index + 1)) {
            return Result<int>::failure(
                Error{ErrorCode::validation_failed, "人物卡版本必须属于同一 ID 且从 1 连续递增", false, "修复人物包版本链"});
        }
    }
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT package_hash,imported_count FROM package_import_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != package_hash) return Result<int>::failure(
                    Error{ErrorCode::command_conflict, "相同 command_id 指向不同人物包", false, "生成新的命令ID"});
                const auto count = sqlite3_column_int(replay.get(), 1);
                transaction.commit();
                return Result<int>::success(count);
            }
        }
        Statement exists(database_, "SELECT 1 FROM character_blueprint WHERE id=?");
        bindText(exists.get(), 1, blueprint_id);
        if (sqlite3_step(exists.get()) == SQLITE_ROW) return Result<int>::failure(
            Error{ErrorCode::revision_conflict, "目标工作区已存在人物卡：" + blueprint_id, false,
                  "导入到空工作区或先解决人物卡冲突"});
        const auto& head = versions.back();
        Statement insert(database_,
            "INSERT INTO character_blueprint(id,head_version,deleted,created_at) VALUES(?,?,?,?)");
        bindText(insert.get(), 1, blueprint_id);
        sqlite3_bind_int(insert.get(), 2, head.version);
        sqlite3_bind_int(insert.get(), 3, head.deleted ? 1 : 0);
        bindText(insert.get(), 4, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& version : versions) insertBlueprintVersion(database_, version);
        Statement record(database_,
            "INSERT INTO package_import_log(command_id,package_hash,imported_count,created_at) VALUES(?,?,?,?)");
        bindText(record.get(), 1, command_id);
        bindText(record.get(), 2, package_hash);
        sqlite3_bind_int(record.get(), 3, static_cast<int>(versions.size()));
        bindText(record.get(), 4, utcNow());
        if (sqlite3_step(record.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<int>::success(static_cast<int>(versions.size()));
    } catch (const std::exception& exception) {
        return Result<int>::failure(storageError(exception));
    }
}

Result<ProviderConnection> WorkspaceRepository::saveProviderConnection(
    const std::string& command_id, ProviderConnection connection, int expected_revision) {
    try {
        const auto payload = providerPayload(connection, expected_revision, "save");
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,connection_id FROM provider_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<ProviderConnection>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于不同的连接内容", false, "生成新的命令标识后重试"});
                auto result = readProvider(database_, columnText(replay.get(), 1)); transaction.commit();
                return Result<ProviderConnection>::success(std::move(result));
            }
        }
        if (expected_revision == 0) {
            Statement existing(database_, "SELECT revision FROM provider_connection WHERE id=?");
            bindText(existing.get(), 1, connection.id);
            if (sqlite3_step(existing.get()) == SQLITE_ROW) return Result<ProviderConnection>::failure(
                {ErrorCode::revision_conflict, "模型连接已存在", false, "刷新连接列表后重试"});
            connection.revision = 1;
            Statement insert(database_, "INSERT INTO provider_connection(id,name,kind,endpoint,default_model,credential_ref,data_policy,enabled,deleted,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
            bindText(insert.get(), 1, connection.id); bindText(insert.get(), 2, connection.name);
            bindText(insert.get(), 3, connection.kind); bindText(insert.get(), 4, connection.endpoint);
            bindText(insert.get(), 5, connection.default_model); bindText(insert.get(), 6, connection.credential_ref);
            bindText(insert.get(), 7, connection.data_policy); sqlite3_bind_int(insert.get(), 8, connection.enabled);
            sqlite3_bind_int(insert.get(), 9, connection.deleted); sqlite3_bind_int(insert.get(), 10, connection.revision);
            bindText(insert.get(), 11, utcNow()); bindText(insert.get(), 12, utcNow());
            if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        } else {
            const auto current = readProvider(database_, connection.id);
            if (current.revision != expected_revision || current.deleted) return Result<ProviderConnection>::failure(
                {ErrorCode::revision_conflict, "模型连接已被其他编辑修改或删除", false, "刷新连接列表后重试"});
            connection.revision = expected_revision + 1;
            Statement update(database_, "UPDATE provider_connection SET name=?,kind=?,endpoint=?,default_model=?,credential_ref=?,data_policy=?,enabled=?,deleted=?,revision=?,updated_at=? WHERE id=? AND revision=?");
            bindText(update.get(), 1, connection.name); bindText(update.get(), 2, connection.kind);
            bindText(update.get(), 3, connection.endpoint); bindText(update.get(), 4, connection.default_model);
            bindText(update.get(), 5, connection.credential_ref); bindText(update.get(), 6, connection.data_policy);
            sqlite3_bind_int(update.get(), 7, connection.enabled); sqlite3_bind_int(update.get(), 8, connection.deleted);
            sqlite3_bind_int(update.get(), 9, connection.revision); bindText(update.get(), 10, utcNow());
            bindText(update.get(), 11, connection.id); sqlite3_bind_int(update.get(), 12, expected_revision);
            if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1)
                throw std::runtime_error("模型连接并发更新失败");
        }
        Statement log(database_, "INSERT INTO provider_command_log(command_id,payload_hash,connection_id,revision,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, connection.id);
        sqlite3_bind_int(log.get(), 4, connection.revision); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<ProviderConnection>::success(std::move(connection));
    } catch (const std::exception& exception) { return Result<ProviderConnection>::failure(storageError(exception)); }
}

Result<ProviderConnection> WorkspaceRepository::loadProviderConnection(const std::string& connection_id) {
    try { return Result<ProviderConnection>::success(readProvider(database_, connection_id)); }
    catch (const std::exception& exception) { return Result<ProviderConnection>::failure(storageError(exception)); }
}

Result<std::vector<ProviderConnection>> WorkspaceRepository::listProviderConnections() {
    try {
        Statement query(database_, "SELECT id FROM provider_connection WHERE deleted=0 ORDER BY name COLLATE NOCASE,id");
        std::vector<ProviderConnection> result;
        while (sqlite3_step(query.get()) == SQLITE_ROW) result.push_back(readProvider(database_, columnText(query.get(), 0)));
        return Result<std::vector<ProviderConnection>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<ProviderConnection>>::failure(storageError(exception)); }
}

Result<ProviderConnection> WorkspaceRepository::deleteProviderConnection(
    const std::string& command_id, const std::string& connection_id, int expected_revision) {
    auto current = loadProviderConnection(connection_id);
    if (!current.ok()) return current;
    current.value->deleted = true; current.value->enabled = false;
    return saveProviderConnection(command_id, std::move(*current.value), expected_revision);
}

Result<EvidenceReference> WorkspaceRepository::createEvidence(
    const std::string& command_id, EvidenceReference evidence) {
    auto valid = xuyan::domain::validateEvidence(std::move(evidence));
    if (!valid.ok()) return valid;
    evidence = std::move(*valid.value);
    try {
        std::ostringstream payload_builder;
        payload_builder << evidence.entity_id << '|' << evidence.field_path << '|' << evidence.source_id << '|'
                        << evidence.start_codepoint << '|' << evidence.end_codepoint << '|'
                        << evidence.quote_hash << '|' << evidence.provenance_type;
        const auto payload = payload_builder.str();
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,evidence_id FROM evidence_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<EvidenceReference>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于不同证据", false, "生成新的命令标识"});
                Statement query(database_, "SELECT id,entity_id,field_path,source_id,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,revision FROM evidence_reference WHERE id=?");
                bindText(query.get(), 1, columnText(replay.get(), 1));
                if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("证据命令结果缺失");
                auto result = readEvidence(query.get()); transaction.commit();
                return Result<EvidenceReference>::success(std::move(result));
            }
        }
        evidence.revision = 1;
        Statement insert(database_, "INSERT INTO evidence_reference(id,entity_id,field_path,source_id,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,revision,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        bindText(insert.get(), 1, evidence.id); bindText(insert.get(), 2, evidence.entity_id);
        bindText(insert.get(), 3, evidence.field_path); bindText(insert.get(), 4, evidence.source_id);
        sqlite3_bind_int64(insert.get(), 5, static_cast<sqlite3_int64>(evidence.start_codepoint));
        sqlite3_bind_int64(insert.get(), 6, static_cast<sqlite3_int64>(evidence.end_codepoint));
        bindText(insert.get(), 7, evidence.quote); bindText(insert.get(), 8, evidence.quote_hash);
        bindText(insert.get(), 9, evidence.provenance_type); sqlite3_bind_int(insert.get(), 10, evidence.revision);
        bindText(insert.get(), 11, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement log(database_, "INSERT INTO evidence_command_log(command_id,payload_hash,evidence_id,created_at) VALUES(?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, evidence.id);
        bindText(log.get(), 4, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<EvidenceReference>::success(std::move(evidence));
    } catch (const std::exception& exception) { return Result<EvidenceReference>::failure(storageError(exception)); }
}

Result<std::vector<EvidenceReference>> WorkspaceRepository::listEvidenceForSource(const std::string& source_id) {
    try {
        Statement query(database_, "SELECT id,entity_id,field_path,source_id,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,revision FROM evidence_reference WHERE source_id=? ORDER BY start_codepoint,id");
        bindText(query.get(), 1, source_id);
        std::vector<EvidenceReference> result;
        while (sqlite3_step(query.get()) == SQLITE_ROW) result.push_back(readEvidence(query.get()));
        return Result<std::vector<EvidenceReference>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<EvidenceReference>>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::createExtractionJob(
    const std::string& command_id, xuyan::domain::ExtractionJob job) {
    auto valid = xuyan::domain::validateExtractionJob(std::move(job));
    if (!valid.ok()) return valid;
    job = std::move(*valid.value);
    std::ostringstream payload_builder;
    payload_builder << "create|" << job.source_id << '|' << job.schema_version << '|' << job.prompt_version;
    for (const auto& step : job.steps) payload_builder << '|' << step.start_codepoint << ':' << step.end_codepoint << ':' << step.chunk_hash;
    payload_builder << "|budget:" << job.budget.estimated_input_tokens << ':' << job.budget.output_token_limit_per_request
                    << ':' << job.budget.max_requests << ':' << job.budget.sample_steps << ':' << job.budget.price_known
                    << ':' << job.budget.estimated_cost_microunits << ':' << job.budget.currency;
    const auto payload = payload_builder.str();
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,job_id FROM extraction_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionJob>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他提取任务", false, "生成新的命令标识"});
                auto result = readExtractionJob(database_, columnText(replay.get(), 1)); transaction.commit();
                return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
            }
        }
        std::ostringstream entity_index_builder;
        {
            Statement entities(database_, "SELECT id,head_revision,deleted FROM world_entity ORDER BY id");
            while (sqlite3_step(entities.get()) == SQLITE_ROW)
                entity_index_builder << columnText(entities.get(), 0) << ':' << sqlite3_column_int(entities.get(), 1)
                                     << ':' << sqlite3_column_int(entities.get(), 2) << '|';
        }
        const auto entity_index_hash = xuyan::domain::sha256(entity_index_builder.str());
        const auto parameters_hash = xuyan::domain::sha256(job.schema_version + '|' + job.prompt_version + '|'
            + job.provider_connection_id + '|' + job.model_id);
        job.revision = 1; job.status = "queued"; job.completed_steps = 0; job.cancel_requested = false;
        Statement insert(database_, "INSERT INTO extraction_job(id,source_id,status,schema_version,prompt_version,provider_connection_id,model_id,total_steps,completed_steps,cancel_requested,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
        bindText(insert.get(), 1, job.id); bindText(insert.get(), 2, job.source_id); bindText(insert.get(), 3, job.status);
        bindText(insert.get(), 4, job.schema_version); bindText(insert.get(), 5, job.prompt_version);
        bindText(insert.get(), 6, job.provider_connection_id); bindText(insert.get(), 7, job.model_id);
        sqlite3_bind_int(insert.get(), 8, job.total_steps); sqlite3_bind_int(insert.get(), 9, 0);
        sqlite3_bind_int(insert.get(), 10, 0); sqlite3_bind_int(insert.get(), 11, job.revision);
        bindText(insert.get(), 12, utcNow()); bindText(insert.get(), 13, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        {
            Statement budget(database_, "INSERT INTO extraction_budget(job_id,estimated_input_tokens,output_token_limit,max_requests,consumed_requests,sample_steps,price_known,estimated_cost_microunits,currency) VALUES(?,?,?,?,?,?,?,?,?)");
            bindText(budget.get(), 1, job.id);
            sqlite3_bind_int64(budget.get(), 2, static_cast<sqlite3_int64>(job.budget.estimated_input_tokens));
            sqlite3_bind_int(budget.get(), 3, job.budget.output_token_limit_per_request);
            sqlite3_bind_int(budget.get(), 4, job.budget.max_requests); sqlite3_bind_int(budget.get(), 5, 0);
            sqlite3_bind_int(budget.get(), 6, job.budget.sample_steps); sqlite3_bind_int(budget.get(), 7, job.budget.price_known ? 1 : 0);
            sqlite3_bind_int64(budget.get(), 8, job.budget.estimated_cost_microunits); bindText(budget.get(), 9, job.budget.currency);
            if (sqlite3_step(budget.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        {
            Statement context(database_, "INSERT INTO extraction_job_cache_context(job_id,entity_index_hash,parameters_hash) VALUES(?,?,?)");
            bindText(context.get(), 1, job.id); bindText(context.get(), 2, entity_index_hash);
            bindText(context.get(), 3, parameters_hash);
            if (sqlite3_step(context.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        int reused_steps = 0;
        for (auto& step : job.steps) {
            std::string cached_job_id; int cached_ordinal = 0;
            std::size_t cached_start = 0; std::size_t cached_end = 0;
            std::vector<xuyan::domain::ExtractionCandidate> cached_candidates;
            {
                Statement cached(database_,
                    "SELECT es.job_id,es.ordinal,es.start_codepoint,es.end_codepoint FROM extraction_step es "
                    "JOIN extraction_job ej ON ej.id=es.job_id JOIN extraction_job_cache_context ctx ON ctx.job_id=ej.id "
                    "WHERE es.chunk_hash=? AND es.status='completed' "
                    "AND ej.status='completed' AND ej.schema_version=? AND ej.prompt_version=? "
                    "AND ctx.entity_index_hash=? AND ctx.parameters_hash=? "
                    "ORDER BY es.updated_at DESC LIMIT 1");
                bindText(cached.get(), 1, step.chunk_hash); bindText(cached.get(), 2, job.schema_version);
                bindText(cached.get(), 3, job.prompt_version); bindText(cached.get(), 4, entity_index_hash);
                bindText(cached.get(), 5, parameters_hash);
                if (sqlite3_step(cached.get()) == SQLITE_ROW) {
                    cached_job_id = columnText(cached.get(), 0); cached_ordinal = sqlite3_column_int(cached.get(), 1);
                    cached_start = static_cast<std::size_t>(sqlite3_column_int64(cached.get(), 2));
                    cached_end = static_cast<std::size_t>(sqlite3_column_int64(cached.get(), 3));
                }
            }
            if (!cached_job_id.empty()) {
                Statement candidates(database_, "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE job_id=? AND step_ordinal=? ORDER BY id");
                bindText(candidates.get(), 1, cached_job_id); sqlite3_bind_int(candidates.get(), 2, cached_ordinal);
                while (sqlite3_step(candidates.get()) == SQLITE_ROW) cached_candidates.push_back(readCandidate(candidates.get()));
            }
            step.status = cached_job_id.empty() ? "ready" : "completed"; step.attempt = 0;
            Statement add(database_, "INSERT INTO extraction_step(id,job_id,ordinal,start_codepoint,end_codepoint,chunk_hash,status,attempt,output_json,error_message,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            bindText(add.get(), 1, step.id); bindText(add.get(), 2, job.id); sqlite3_bind_int(add.get(), 3, step.ordinal);
            sqlite3_bind_int64(add.get(), 4, static_cast<sqlite3_int64>(step.start_codepoint));
            sqlite3_bind_int64(add.get(), 5, static_cast<sqlite3_int64>(step.end_codepoint));
            bindText(add.get(), 6, step.chunk_hash); bindText(add.get(), 7, step.status); sqlite3_bind_int(add.get(), 8, 0);
            bindText(add.get(), 9, ""); bindText(add.get(), 10, ""); bindText(add.get(), 11, utcNow());
            if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            if (!cached_job_id.empty()) {
                ++reused_steps;
                Statement hit(database_, "INSERT INTO extraction_step_cache_hit(job_id,step_ordinal,source_job_id,source_step_ordinal,chunk_hash,created_at) VALUES(?,?,?,?,?,?)");
                bindText(hit.get(), 1, job.id); sqlite3_bind_int(hit.get(), 2, step.ordinal);
                bindText(hit.get(), 3, cached_job_id); sqlite3_bind_int(hit.get(), 4, cached_ordinal);
                bindText(hit.get(), 5, step.chunk_hash); bindText(hit.get(), 6, utcNow());
                if (sqlite3_step(hit.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
                for (auto candidate : cached_candidates) {
                    if (candidate.start_codepoint < cached_start || candidate.end_codepoint > cached_end)
                        throw std::runtime_error("缓存候选范围超出原步骤");
                    const auto old_id = candidate.id;
                    candidate.id = "candidate-" + xuyan::domain::sha256(job.id + '|' + std::to_string(step.ordinal) + '|' + old_id).substr(0, 24);
                    candidate.job_id = job.id; candidate.step_ordinal = step.ordinal; candidate.source_id = job.source_id;
                    candidate.start_codepoint = step.start_codepoint + (candidate.start_codepoint - cached_start);
                    candidate.end_codepoint = step.start_codepoint + (candidate.end_codepoint - cached_start);
                    candidate.review_status = "candidate"; candidate.revision = 1;
                    Statement insert_candidate(database_, "INSERT INTO extraction_candidate(id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
                    bindText(insert_candidate.get(), 1, candidate.id); bindText(insert_candidate.get(), 2, candidate.job_id);
                    sqlite3_bind_int(insert_candidate.get(), 3, candidate.step_ordinal); bindText(insert_candidate.get(), 4, candidate.source_id);
                    bindText(insert_candidate.get(), 5, candidate.candidate_type); bindText(insert_candidate.get(), 6, candidate.name);
                    bindText(insert_candidate.get(), 7, candidate.fields_json);
                    sqlite3_bind_int64(insert_candidate.get(), 8, static_cast<sqlite3_int64>(candidate.start_codepoint));
                    sqlite3_bind_int64(insert_candidate.get(), 9, static_cast<sqlite3_int64>(candidate.end_codepoint));
                    bindText(insert_candidate.get(), 10, candidate.quote); bindText(insert_candidate.get(), 11, candidate.quote_hash);
                    bindText(insert_candidate.get(), 12, candidate.provenance_type); bindText(insert_candidate.get(), 13, candidate.review_status);
                    bindText(insert_candidate.get(), 14, candidate.schema_version); bindText(insert_candidate.get(), 15, candidate.prompt_version);
                    sqlite3_bind_int(insert_candidate.get(), 16, 1); bindText(insert_candidate.get(), 17, utcNow()); bindText(insert_candidate.get(), 18, utcNow());
                    if (sqlite3_step(insert_candidate.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
                    Statement history(database_, "INSERT INTO candidate_review_history(candidate_id,revision,name,fields_json,provenance_type,review_status,updated_at) VALUES(?,?,?,?,?,?,?)");
                    bindText(history.get(), 1, candidate.id); sqlite3_bind_int(history.get(), 2, 1);
                    bindText(history.get(), 3, candidate.name); bindText(history.get(), 4, candidate.fields_json);
                    bindText(history.get(), 5, candidate.provenance_type); bindText(history.get(), 6, candidate.review_status);
                    bindText(history.get(), 7, utcNow());
                    if (sqlite3_step(history.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
                }
            }
        }
        job.completed_steps = reused_steps;
        job.status = reused_steps == job.total_steps ? "completed" : "queued";
        if (reused_steps > 0) {
            Statement reuse(database_, "UPDATE extraction_job SET status=?,completed_steps=?,updated_at=? WHERE id=?");
            bindText(reuse.get(), 1, job.status); sqlite3_bind_int(reuse.get(), 2, reused_steps);
            bindText(reuse.get(), 3, utcNow()); bindText(reuse.get(), 4, job.id);
            if (sqlite3_step(reuse.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1)
                throw std::runtime_error("无法提交提取缓存命中");
        }
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job.id);
        sqlite3_bind_int(log.get(), 4, 0); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::ExtractionJob>::success(std::move(job));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::loadExtractionJob(const std::string& job_id) {
    try { return Result<xuyan::domain::ExtractionJob>::success(readExtractionJob(database_, job_id)); }
    catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::ExtractionJob>> WorkspaceRepository::listExtractionJobs() {
    try {
        Statement query(database_, "SELECT id FROM extraction_job ORDER BY updated_at DESC,id");
        std::vector<std::string> ids;
        while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        std::vector<xuyan::domain::ExtractionJob> jobs;
        for (const auto& id : ids) jobs.push_back(readExtractionJob(database_, id));
        return Result<std::vector<xuyan::domain::ExtractionJob>>::success(std::move(jobs));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::ExtractionJob>>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionStep> WorkspaceRepository::claimExtractionStep(
    const std::string& command_id, const std::string& job_id, int expected_revision) {
    const auto payload = "claim|" + job_id + '|' + std::to_string(expected_revision);
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash,step_ordinal FROM extraction_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionStep>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他步骤领取", false, "生成新的命令标识"});
                const auto ordinal = sqlite3_column_int(replay.get(), 1);
                auto job = readExtractionJob(database_, job_id);
                const auto found = std::find_if(job.steps.begin(), job.steps.end(), [ordinal](const auto& step) { return step.ordinal == ordinal; });
                if (found == job.steps.end()) throw std::runtime_error("领取步骤结果缺失");
                auto result = *found; transaction.commit(); return Result<xuyan::domain::ExtractionStep>::success(std::move(result));
            }
        }
        auto job = readExtractionJob(database_, job_id);
        if (job.revision != expected_revision || job.cancel_requested) return Result<xuyan::domain::ExtractionStep>::failure(
            {ErrorCode::revision_conflict, "任务已变化或正在取消", false, "刷新任务后重试"});
        const auto ready = std::find_if(job.steps.begin(), job.steps.end(), [](const auto& step) { return step.status == "ready"; });
        if (ready == job.steps.end()) return Result<xuyan::domain::ExtractionStep>::failure(
            {ErrorCode::missing_context, "没有可领取的提取步骤", false, "检查失败、未知或已完成步骤"});
        if (job.budget.consumed_requests >= job.budget.max_requests) return Result<xuyan::domain::ExtractionStep>::failure(
            {ErrorCode::validation_failed, "提取任务已达到硬调用预算", false, "提高预算或保留未完成步骤供人工处理"});
        auto step = *ready; ++step.attempt; step.status = "running";
        Statement update_step(database_, "UPDATE extraction_step SET status='running',attempt=?,updated_at=? WHERE job_id=? AND ordinal=? AND status='ready' AND attempt=?");
        sqlite3_bind_int(update_step.get(), 1, step.attempt); bindText(update_step.get(), 2, utcNow()); bindText(update_step.get(), 3, job_id);
        sqlite3_bind_int(update_step.get(), 4, step.ordinal); sqlite3_bind_int(update_step.get(), 5, step.attempt - 1);
        if (sqlite3_step(update_step.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("步骤领取竞争失败");
        Statement update_job(database_, "UPDATE extraction_job SET status='running',revision=revision+1,updated_at=? WHERE id=? AND revision=?");
        bindText(update_job.get(), 1, utcNow()); bindText(update_job.get(), 2, job_id); sqlite3_bind_int(update_job.get(), 3, expected_revision);
        if (sqlite3_step(update_job.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("任务领取竞争失败");
        Statement consume(database_, "UPDATE extraction_budget SET consumed_requests=consumed_requests+1 WHERE job_id=? AND consumed_requests<max_requests");
        bindText(consume.get(), 1, job_id);
        if (sqlite3_step(consume.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("调用预算并发预留失败");
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job_id);
        sqlite3_bind_int(log.get(), 4, step.ordinal); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::ExtractionStep>::success(std::move(step));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionStep>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::finishExtractionStep(
    const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
    const std::string& terminal_status, const std::string& output_json, const std::string& error_message) {
    if ((terminal_status != "completed" && terminal_status != "failed" && terminal_status != "unknown")
        || output_json.size() > 8 * 1024 * 1024 || error_message.size() > 4096) return Result<xuyan::domain::ExtractionJob>::failure(
        {ErrorCode::validation_failed, "步骤终态或结果大小无效", false, "使用 completed、failed 或 unknown"});
    const auto payload = "finish|" + job_id + '|' + std::to_string(ordinal) + '|' + std::to_string(expected_attempt)
        + '|' + terminal_status + '|' + xuyan::domain::sha256(output_json) + '|' + error_message;
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash FROM extraction_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionJob>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他步骤结果", false, "生成新的命令标识"});
                auto result = readExtractionJob(database_, job_id); transaction.commit();
                return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
            }
        }
        Statement finish(database_, "UPDATE extraction_step SET status=?,output_json=?,error_message=?,updated_at=? WHERE job_id=? AND ordinal=? AND status='running' AND attempt=?");
        bindText(finish.get(), 1, terminal_status); bindText(finish.get(), 2, output_json); bindText(finish.get(), 3, error_message);
        bindText(finish.get(), 4, utcNow()); bindText(finish.get(), 5, job_id); sqlite3_bind_int(finish.get(), 6, ordinal);
        sqlite3_bind_int(finish.get(), 7, expected_attempt);
        if (sqlite3_step(finish.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) return Result<xuyan::domain::ExtractionJob>::failure(
            {ErrorCode::revision_conflict, "步骤不再处于对应运行尝试", false, "刷新任务后重试"});
        Statement counts(database_, "SELECT COUNT(*),SUM(status='completed'),SUM(status='failed'),SUM(status='unknown'),SUM(status='running') FROM extraction_step WHERE job_id=?");
        bindText(counts.get(), 1, job_id);
        if (sqlite3_step(counts.get()) != SQLITE_ROW) throw std::runtime_error("无法汇总任务步骤");
        const auto total = sqlite3_column_int(counts.get(), 0); const auto completed = sqlite3_column_int(counts.get(), 1);
        const auto failed = sqlite3_column_int(counts.get(), 2); const auto unknown = sqlite3_column_int(counts.get(), 3);
        const auto running = sqlite3_column_int(counts.get(), 4);
        auto current = readExtractionJob(database_, job_id);
        std::string status = failed > 0 || unknown > 0 ? "needs_attention"
            : completed == total ? "completed" : running > 0 ? "running" : current.cancel_requested ? "cancelled" : "queued";
        Statement update(database_, "UPDATE extraction_job SET status=?,completed_steps=?,revision=revision+1,updated_at=? WHERE id=?");
        bindText(update.get(), 1, status); sqlite3_bind_int(update.get(), 2, completed); bindText(update.get(), 3, utcNow()); bindText(update.get(), 4, job_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("无法更新任务进度");
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job_id);
        sqlite3_bind_int(log.get(), 4, ordinal); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readExtractionJob(database_, job_id); transaction.commit();
        return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::cancelExtractionJob(
    const std::string& command_id, const std::string& job_id, int expected_revision) {
    const auto payload = "cancel|" + job_id + '|' + std::to_string(expected_revision);
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash FROM extraction_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::command_conflict, "命令标识已用于其他取消", false, "生成新的命令标识"});
            auto result = readExtractionJob(database_, job_id); transaction.commit(); return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
        }
        auto current = readExtractionJob(database_, job_id);
        if (current.revision != expected_revision) return Result<xuyan::domain::ExtractionJob>::failure(
            {ErrorCode::revision_conflict, "任务修订已变化", false, "刷新任务后重试"});
        Statement steps(database_, "UPDATE extraction_step SET status='cancelled',updated_at=? WHERE job_id=? AND status='ready'");
        bindText(steps.get(), 1, utcNow()); bindText(steps.get(), 2, job_id);
        if (sqlite3_step(steps.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        const bool running = std::any_of(current.steps.begin(), current.steps.end(), [](const auto& step) { return step.status == "running"; });
        Statement update(database_, "UPDATE extraction_job SET status=?,cancel_requested=1,revision=revision+1,updated_at=? WHERE id=? AND revision=?");
        bindText(update.get(), 1, running ? "cancelling" : "cancelled"); bindText(update.get(), 2, utcNow()); bindText(update.get(), 3, job_id);
        sqlite3_bind_int(update.get(), 4, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("取消任务竞争失败");
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job_id);
        sqlite3_bind_int(log.get(), 4, 0); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readExtractionJob(database_, job_id); transaction.commit(); return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::retryExtractionStep(
    const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt) {
    const auto payload = "retry|" + job_id + '|' + std::to_string(ordinal) + '|' + std::to_string(expected_attempt);
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash FROM extraction_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::command_conflict, "命令标识已用于其他重试", false, "生成新的命令标识"});
            auto result = readExtractionJob(database_, job_id); transaction.commit(); return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
        }
        Statement step(database_, "UPDATE extraction_step SET status='ready',output_json='',error_message='',updated_at=? WHERE job_id=? AND ordinal=? AND attempt=? AND status IN ('failed','unknown')");
        bindText(step.get(), 1, utcNow()); bindText(step.get(), 2, job_id); sqlite3_bind_int(step.get(), 3, ordinal); sqlite3_bind_int(step.get(), 4, expected_attempt);
        if (sqlite3_step(step.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) return Result<xuyan::domain::ExtractionJob>::failure(
            {ErrorCode::revision_conflict, "步骤状态或尝试次数已变化", false, "刷新任务后重试"});
        Statement update(database_, "UPDATE extraction_job SET status='queued',cancel_requested=0,revision=revision+1,updated_at=? WHERE id=?");
        bindText(update.get(), 1, utcNow()); bindText(update.get(), 2, job_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("无法恢复任务队列");
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job_id);
        sqlite3_bind_int(log.get(), 4, ordinal); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readExtractionJob(database_, job_id); transaction.commit(); return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<int> WorkspaceRepository::recoverInterruptedExtractionSteps() {
    try {
        Transaction transaction(database_);
        Statement jobs_query(database_, "SELECT DISTINCT job_id FROM extraction_step WHERE status='running'");
        std::vector<std::string> jobs;
        while (sqlite3_step(jobs_query.get()) == SQLITE_ROW) jobs.push_back(columnText(jobs_query.get(), 0));
        Statement steps(database_, "UPDATE extraction_step SET status='unknown',error_message='进程中断，请确认外部请求是否已计费后再重试',updated_at=? WHERE status='running'");
        bindText(steps.get(), 1, utcNow());
        if (sqlite3_step(steps.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        const auto count = sqlite3_changes(database_);
        for (const auto& job_id : jobs) {
            Statement update(database_, "UPDATE extraction_job SET status='needs_attention',revision=revision+1,updated_at=? WHERE id=?");
            bindText(update.get(), 1, utcNow()); bindText(update.get(), 2, job_id);
            if (sqlite3_step(update.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        transaction.commit(); return Result<int>::success(count);
    } catch (const std::exception& exception) { return Result<int>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionJob> WorkspaceRepository::commitExtractionCandidates(
    const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
    const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates) {
    for (auto& candidate : candidates) {
        auto valid = xuyan::domain::validateExtractionCandidate(std::move(candidate));
        if (!valid.ok()) return Result<xuyan::domain::ExtractionJob>::failure(*valid.error);
        candidate = std::move(*valid.value);
        if (candidate.job_id != job_id || candidate.step_ordinal != step_ordinal)
            return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::validation_failed, "候选不属于当前任务步骤", false, "拒绝该输出"});
    }
    const auto payload = "candidates|" + job_id + '|' + std::to_string(step_ordinal) + '|'
        + std::to_string(expected_attempt) + '|' + xuyan::domain::sha256(output_json);
    try {
        Transaction transaction(database_);
        {
            Statement replay(database_, "SELECT payload_hash FROM extraction_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionJob>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他候选提交", false, "生成新的命令标识"});
                auto result = readExtractionJob(database_, job_id); transaction.commit();
                return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
            }
        }
        Statement state(database_, "SELECT j.source_id,s.status,s.attempt FROM extraction_step s JOIN extraction_job j ON j.id=s.job_id WHERE s.job_id=? AND s.ordinal=?");
        bindText(state.get(), 1, job_id); sqlite3_bind_int(state.get(), 2, step_ordinal);
        if (sqlite3_step(state.get()) != SQLITE_ROW || columnText(state.get(), 1) != "running"
            || sqlite3_column_int(state.get(), 2) != expected_attempt) return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::revision_conflict, "候选步骤尝试已变化", false, "刷新任务后重试"});
        const auto source_id = columnText(state.get(), 0);
        for (auto& candidate : candidates) {
            if (candidate.source_id != source_id) return Result<xuyan::domain::ExtractionJob>::failure(
                {ErrorCode::validation_failed, "候选来源与任务不一致", false, "拒绝该输出"});
            candidate.revision = 1;
            Statement insert(database_, "INSERT INTO extraction_candidate(id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            bindText(insert.get(), 1, candidate.id); bindText(insert.get(), 2, candidate.job_id);
            sqlite3_bind_int(insert.get(), 3, candidate.step_ordinal); bindText(insert.get(), 4, candidate.source_id);
            bindText(insert.get(), 5, candidate.candidate_type); bindText(insert.get(), 6, candidate.name);
            bindText(insert.get(), 7, candidate.fields_json);
            sqlite3_bind_int64(insert.get(), 8, static_cast<sqlite3_int64>(candidate.start_codepoint));
            sqlite3_bind_int64(insert.get(), 9, static_cast<sqlite3_int64>(candidate.end_codepoint));
            bindText(insert.get(), 10, candidate.quote); bindText(insert.get(), 11, candidate.quote_hash);
            bindText(insert.get(), 12, candidate.provenance_type); bindText(insert.get(), 13, candidate.review_status);
            bindText(insert.get(), 14, candidate.schema_version); bindText(insert.get(), 15, candidate.prompt_version);
            sqlite3_bind_int(insert.get(), 16, candidate.revision); bindText(insert.get(), 17, utcNow()); bindText(insert.get(), 18, utcNow());
            if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            Statement history(database_, "INSERT INTO candidate_review_history(candidate_id,revision,name,fields_json,provenance_type,review_status,updated_at) VALUES(?,?,?,?,?,?,?)");
            bindText(history.get(), 1, candidate.id); sqlite3_bind_int(history.get(), 2, candidate.revision);
            bindText(history.get(), 3, candidate.name); bindText(history.get(), 4, candidate.fields_json);
            bindText(history.get(), 5, candidate.provenance_type); bindText(history.get(), 6, candidate.review_status);
            bindText(history.get(), 7, utcNow());
            if (sqlite3_step(history.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement step(database_, "UPDATE extraction_step SET status='completed',output_json=?,error_message='',updated_at=? WHERE job_id=? AND ordinal=? AND status='running' AND attempt=?");
        bindText(step.get(), 1, output_json); bindText(step.get(), 2, utcNow()); bindText(step.get(), 3, job_id);
        sqlite3_bind_int(step.get(), 4, step_ordinal); sqlite3_bind_int(step.get(), 5, expected_attempt);
        if (sqlite3_step(step.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("候选步骤提交竞争失败");
        Statement counts(database_, "SELECT COUNT(*),SUM(status='completed') FROM extraction_step WHERE job_id=?");
        bindText(counts.get(), 1, job_id);
        if (sqlite3_step(counts.get()) != SQLITE_ROW) throw std::runtime_error("无法汇总候选任务进度");
        const auto total = sqlite3_column_int(counts.get(), 0); const auto completed = sqlite3_column_int(counts.get(), 1);
        Statement update(database_, "UPDATE extraction_job SET status=?,completed_steps=?,revision=revision+1,updated_at=? WHERE id=?");
        bindText(update.get(), 1, completed == total ? "completed" : "queued"); sqlite3_bind_int(update.get(), 2, completed);
        bindText(update.get(), 3, utcNow()); bindText(update.get(), 4, job_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("无法更新候选任务进度");
        Statement log(database_, "INSERT INTO extraction_command_log(command_id,payload_hash,job_id,step_ordinal,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, job_id);
        sqlite3_bind_int(log.get(), 4, step_ordinal); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readExtractionJob(database_, job_id); transaction.commit();
        return Result<xuyan::domain::ExtractionJob>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionJob>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::ExtractionCandidate>> WorkspaceRepository::listExtractionCandidates(
    const std::string& review_status) {
    try {
        const char* sql = review_status.empty()
            ? "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate ORDER BY created_at,id"
            : "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE review_status=? ORDER BY created_at,id";
        Statement query(database_, sql);
        if (!review_status.empty()) bindText(query.get(), 1, review_status);
        std::vector<xuyan::domain::ExtractionCandidate> result;
        while (sqlite3_step(query.get()) == SQLITE_ROW) result.push_back(readCandidate(query.get()));
        return Result<std::vector<xuyan::domain::ExtractionCandidate>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::ExtractionCandidate>>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::ExtractionCandidate>> WorkspaceRepository::listExtractionCandidatesForJob(
    const std::string& job_id, int limit) {
    if (job_id.empty() || limit < 1 || limit > 1000) return Result<std::vector<xuyan::domain::ExtractionCandidate>>::failure(
        {ErrorCode::validation_failed, "候选抽样任务或数量无效", false, "选择任务并限制为 1—1000 项"});
    try {
        Statement query(database_, "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE job_id=? ORDER BY step_ordinal,id LIMIT ?");
        bindText(query.get(), 1, job_id); sqlite3_bind_int(query.get(), 2, limit);
        std::vector<xuyan::domain::ExtractionCandidate> result;
        while (sqlite3_step(query.get()) == SQLITE_ROW) result.push_back(readCandidate(query.get()));
        return Result<std::vector<xuyan::domain::ExtractionCandidate>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::ExtractionCandidate>>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionCandidate> WorkspaceRepository::loadExtractionCandidate(const std::string& candidate_id) {
    try {
        Statement query(database_, "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE id=?");
        bindText(query.get(), 1, candidate_id);
        if (sqlite3_step(query.get()) != SQLITE_ROW) return Result<xuyan::domain::ExtractionCandidate>::failure(
            {ErrorCode::missing_context, "找不到提取候选", false, "刷新校对列表"});
        return Result<xuyan::domain::ExtractionCandidate>::success(readCandidate(query.get()));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionCandidate>::failure(storageError(exception)); }
}

Result<xuyan::domain::ExtractionCandidate> WorkspaceRepository::reviewExtractionCandidate(
    const std::string& command_id, xuyan::domain::ExtractionCandidate candidate, int expected_revision,
    std::optional<WorldEntity> accepted_entity) {
    if (candidate.review_status != "candidate" && candidate.review_status != "accepted"
        && candidate.review_status != "rejected" && candidate.review_status != "conflicted")
        return Result<xuyan::domain::ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "候选审核状态无效", false, "选择候选、接受、拒绝或冲突"});
    auto valid = xuyan::domain::validateExtractionCandidate(candidate);
    if (!valid.ok()) return valid;
    candidate = std::move(*valid.value);
    if ((candidate.review_status == "accepted") != accepted_entity.has_value())
        return Result<xuyan::domain::ExtractionCandidate>::failure(
            {ErrorCode::validation_failed, "接受候选必须携带目标世界条目", false, "重新提交审核"});
    if (accepted_entity) {
        auto checked = xuyan::domain::validateEntity(std::move(*accepted_entity));
        if (!checked.ok()) return Result<xuyan::domain::ExtractionCandidate>::failure(*checked.error);
        accepted_entity = std::move(*checked.value);
    }
    const auto payload = candidate.id + '|' + std::to_string(expected_revision) + '|' + candidate.review_status + '|'
        + candidate.name + '|' + candidate.fields_json + '|' + candidate.provenance_type;
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,candidate_id FROM candidate_review_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::ExtractionCandidate>::failure(
                {ErrorCode::command_conflict, "命令标识已用于其他候选审核", false, "生成新的命令标识"});
            Statement query(database_, "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE id=?");
            bindText(query.get(), 1, columnText(replay.get(), 1));
            if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("候选审核结果缺失");
            auto result = readCandidate(query.get()); transaction.commit();
            return Result<xuyan::domain::ExtractionCandidate>::success(std::move(result));
        }
        Statement current_query(database_, "SELECT id,job_id,step_ordinal,source_id,candidate_type,name,fields_json,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,review_status,schema_version,prompt_version,revision FROM extraction_candidate WHERE id=?");
        bindText(current_query.get(), 1, candidate.id);
        if (sqlite3_step(current_query.get()) != SQLITE_ROW) return Result<xuyan::domain::ExtractionCandidate>::failure(
            {ErrorCode::missing_context, "找不到提取候选", false, "刷新校对列表"});
        const auto current = readCandidate(current_query.get());
        if (current.revision != expected_revision || current.review_status == "accepted" || current.review_status == "rejected")
            return Result<xuyan::domain::ExtractionCandidate>::failure(
                {ErrorCode::revision_conflict, "候选已被其他审核修改或终结", false, "刷新校对列表"});
        candidate.revision = expected_revision + 1;
        Statement update(database_, "UPDATE extraction_candidate SET name=?,fields_json=?,provenance_type=?,review_status=?,revision=?,updated_at=? WHERE id=? AND revision=?");
        bindText(update.get(), 1, candidate.name); bindText(update.get(), 2, candidate.fields_json);
        bindText(update.get(), 3, candidate.provenance_type); bindText(update.get(), 4, candidate.review_status);
        sqlite3_bind_int(update.get(), 5, candidate.revision); bindText(update.get(), 6, utcNow());
        bindText(update.get(), 7, candidate.id); sqlite3_bind_int(update.get(), 8, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("候选审核并发更新失败");
        Statement history(database_, "INSERT INTO candidate_review_history(candidate_id,revision,name,fields_json,provenance_type,review_status,updated_at) VALUES(?,?,?,?,?,?,?)");
        bindText(history.get(), 1, candidate.id); sqlite3_bind_int(history.get(), 2, candidate.revision);
        bindText(history.get(), 3, candidate.name); bindText(history.get(), 4, candidate.fields_json);
        bindText(history.get(), 5, candidate.provenance_type); bindText(history.get(), 6, candidate.review_status);
        bindText(history.get(), 7, utcNow());
        if (sqlite3_step(history.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));

        if (accepted_entity) {
            accepted_entity->revision = 1;
            Statement entity(database_, "INSERT INTO world_entity(id,world_id,head_revision,deleted,created_at) VALUES(?,?,?,?,?)");
            bindText(entity.get(), 1, accepted_entity->id); bindText(entity.get(), 2, accepted_entity->world_id);
            sqlite3_bind_int(entity.get(), 3, 1); sqlite3_bind_int(entity.get(), 4, 0); bindText(entity.get(), 5, utcNow());
            if (sqlite3_step(entity.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            insertEntityRevision(database_, *accepted_entity);
            Statement acceptance(database_, "INSERT INTO candidate_acceptance(candidate_id,entity_id,accepted_at) VALUES(?,?,?)");
            bindText(acceptance.get(), 1, candidate.id); bindText(acceptance.get(), 2, accepted_entity->id); bindText(acceptance.get(), 3, utcNow());
            if (sqlite3_step(acceptance.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            EvidenceReference evidence;
            evidence.id = "evidence-from-" + candidate.id; evidence.entity_id = accepted_entity->id;
            evidence.field_path = "description"; evidence.source_id = candidate.source_id;
            evidence.start_codepoint = candidate.start_codepoint; evidence.end_codepoint = candidate.end_codepoint;
            evidence.quote = candidate.quote; evidence.quote_hash = candidate.quote_hash;
            evidence.provenance_type = candidate.provenance_type; evidence.revision = 1;
            Statement add_evidence(database_, "INSERT INTO evidence_reference(id,entity_id,field_path,source_id,start_codepoint,end_codepoint,quote,quote_hash,provenance_type,revision,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            bindText(add_evidence.get(), 1, evidence.id); bindText(add_evidence.get(), 2, evidence.entity_id);
            bindText(add_evidence.get(), 3, evidence.field_path); bindText(add_evidence.get(), 4, evidence.source_id);
            sqlite3_bind_int64(add_evidence.get(), 5, static_cast<sqlite3_int64>(evidence.start_codepoint));
            sqlite3_bind_int64(add_evidence.get(), 6, static_cast<sqlite3_int64>(evidence.end_codepoint));
            bindText(add_evidence.get(), 7, evidence.quote); bindText(add_evidence.get(), 8, evidence.quote_hash);
            bindText(add_evidence.get(), 9, evidence.provenance_type); sqlite3_bind_int(add_evidence.get(), 10, 1);
            bindText(add_evidence.get(), 11, utcNow());
            if (sqlite3_step(add_evidence.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement log(database_, "INSERT INTO candidate_review_command_log(command_id,payload_hash,candidate_id,revision,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, candidate.id);
        sqlite3_bind_int(log.get(), 4, candidate.revision); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::ExtractionCandidate>::success(std::move(candidate));
    } catch (const std::exception& exception) { return Result<xuyan::domain::ExtractionCandidate>::failure(storageError(exception)); }
}

Result<xuyan::domain::EntityRetrievalScope> WorkspaceRepository::saveEntityRetrievalScope(
    const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision) {
    auto valid = xuyan::domain::validateEntityRetrievalScope(std::move(scope));
    if (!valid.ok()) return valid;
    scope = std::move(*valid.value);
    std::ostringstream payload_builder;
    payload_builder << scope.entity_id << '|' << (scope.valid_from ? std::to_string(*scope.valid_from) : "null")
                    << '|' << (scope.valid_to ? std::to_string(*scope.valid_to) : "null") << '|' << scope.visibility;
    for (const auto& actor : scope.actor_grants) payload_builder << '|' << actor;
    const auto payload = payload_builder.str();
    try {
        Transaction transaction(database_);
        auto read_scope = [this](const std::string& entity_id) {
            xuyan::domain::EntityRetrievalScope value; value.entity_id = entity_id;
            Statement query(database_, "SELECT has_valid_from,valid_from,has_valid_to,valid_to,visibility,revision FROM entity_retrieval_scope WHERE entity_id=?");
            bindText(query.get(), 1, entity_id);
            if (sqlite3_step(query.get()) != SQLITE_ROW) throw std::runtime_error("找不到条目检索范围");
            if (sqlite3_column_int(query.get(), 0)) value.valid_from = sqlite3_column_int64(query.get(), 1);
            if (sqlite3_column_int(query.get(), 2)) value.valid_to = sqlite3_column_int64(query.get(), 3);
            value.visibility = columnText(query.get(), 4); value.revision = sqlite3_column_int(query.get(), 5);
            Statement grants(database_, "SELECT actor_id FROM entity_retrieval_grant WHERE entity_id=? ORDER BY actor_id");
            bindText(grants.get(), 1, entity_id);
            while (sqlite3_step(grants.get()) == SQLITE_ROW) value.actor_grants.push_back(columnText(grants.get(), 0));
            return value;
        };
        {
            Statement replay(database_, "SELECT payload_hash,entity_id FROM entity_retrieval_command_log WHERE command_id=?");
            bindText(replay.get(), 1, command_id);
            if (sqlite3_step(replay.get()) == SQLITE_ROW) {
                if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::EntityRetrievalScope>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他检索范围", false, "生成新的命令标识"});
                auto result = read_scope(columnText(replay.get(), 1)); transaction.commit();
                return Result<xuyan::domain::EntityRetrievalScope>::success(std::move(result));
            }
        }
        Statement entity(database_, "SELECT 1 FROM world_entity WHERE id=? AND deleted=0"); bindText(entity.get(), 1, scope.entity_id);
        if (sqlite3_step(entity.get()) != SQLITE_ROW) return Result<xuyan::domain::EntityRetrievalScope>::failure(
            {ErrorCode::missing_context, "检索范围对应的世界条目不存在", false, "刷新世界资料"});
        int current_revision = 0;
        {
            Statement current(database_, "SELECT revision FROM entity_retrieval_scope WHERE entity_id=?"); bindText(current.get(), 1, scope.entity_id);
            if (sqlite3_step(current.get()) == SQLITE_ROW) current_revision = sqlite3_column_int(current.get(), 0);
        }
        if (current_revision != expected_revision) return Result<xuyan::domain::EntityRetrievalScope>::failure(
            {ErrorCode::revision_conflict, "检索范围修订已变化", false, "刷新后重试"});
        scope.revision = expected_revision + 1;
        Statement upsert(database_, "INSERT INTO entity_retrieval_scope(entity_id,has_valid_from,valid_from,has_valid_to,valid_to,visibility,revision) VALUES(?,?,?,?,?,?,?) ON CONFLICT(entity_id) DO UPDATE SET has_valid_from=excluded.has_valid_from,valid_from=excluded.valid_from,has_valid_to=excluded.has_valid_to,valid_to=excluded.valid_to,visibility=excluded.visibility,revision=excluded.revision");
        bindText(upsert.get(), 1, scope.entity_id); sqlite3_bind_int(upsert.get(), 2, scope.valid_from.has_value() ? 1 : 0);
        sqlite3_bind_int64(upsert.get(), 3, scope.valid_from.value_or(0)); sqlite3_bind_int(upsert.get(), 4, scope.valid_to.has_value() ? 1 : 0);
        sqlite3_bind_int64(upsert.get(), 5, scope.valid_to.value_or(0)); bindText(upsert.get(), 6, scope.visibility);
        sqlite3_bind_int(upsert.get(), 7, scope.revision);
        if (sqlite3_step(upsert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement history_scope(database_, "INSERT INTO entity_retrieval_scope_history(entity_id,revision,has_valid_from,valid_from,has_valid_to,valid_to,visibility) VALUES(?,?,?,?,?,?,?)");
        bindText(history_scope.get(), 1, scope.entity_id); sqlite3_bind_int(history_scope.get(), 2, scope.revision);
        sqlite3_bind_int(history_scope.get(), 3, scope.valid_from.has_value() ? 1 : 0);
        sqlite3_bind_int64(history_scope.get(), 4, scope.valid_from.value_or(0));
        sqlite3_bind_int(history_scope.get(), 5, scope.valid_to.has_value() ? 1 : 0);
        sqlite3_bind_int64(history_scope.get(), 6, scope.valid_to.value_or(0)); bindText(history_scope.get(), 7, scope.visibility);
        if (sqlite3_step(history_scope.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement clear(database_, "DELETE FROM entity_retrieval_grant WHERE entity_id=?"); bindText(clear.get(), 1, scope.entity_id);
        if (sqlite3_step(clear.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& actor : scope.actor_grants) {
            Statement grant(database_, "INSERT INTO entity_retrieval_grant(entity_id,actor_id) VALUES(?,?)");
            bindText(grant.get(), 1, scope.entity_id); bindText(grant.get(), 2, actor);
            if (sqlite3_step(grant.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            Statement history_grant(database_, "INSERT INTO entity_retrieval_grant_history(entity_id,revision,actor_id) VALUES(?,?,?)");
            bindText(history_grant.get(), 1, scope.entity_id); sqlite3_bind_int(history_grant.get(), 2, scope.revision);
            bindText(history_grant.get(), 3, actor);
            if (sqlite3_step(history_grant.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement log(database_, "INSERT INTO entity_retrieval_command_log(command_id,payload_hash,entity_id,revision,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, scope.entity_id);
        sqlite3_bind_int(log.get(), 4, scope.revision); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::EntityRetrievalScope>::success(std::move(scope));
    } catch (const std::exception& exception) { return Result<xuyan::domain::EntityRetrievalScope>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::RetrievalHit>> WorkspaceRepository::retrieveEntities(
    xuyan::domain::RetrievalRequest request) {
    auto valid = xuyan::domain::validateRetrievalRequest(std::move(request));
    if (!valid.ok()) return Result<std::vector<xuyan::domain::RetrievalHit>>::failure(*valid.error);
    request = std::move(*valid.value);
    try {
        Statement query(database_,
            "SELECT e.id FROM world_entity e LEFT JOIN entity_retrieval_scope s ON s.entity_id=e.id "
            "WHERE e.world_id=? AND e.deleted=0 "
            "AND (?=0 OR s.entity_id IS NULL OR s.has_valid_from=0 OR s.valid_from<=?) "
            "AND (?=0 OR s.entity_id IS NULL OR s.has_valid_to=0 OR s.valid_to>=?) "
            "AND (?=1 OR s.entity_id IS NULL OR s.visibility='public' OR (s.visibility='restricted' AND EXISTS(" 
            "SELECT 1 FROM entity_retrieval_grant g WHERE g.entity_id=e.id AND g.actor_id=?))) "
            "ORDER BY e.id LIMIT 5000");
        bindText(query.get(), 1, request.world_id);
        sqlite3_bind_int(query.get(), 2, request.story_time.has_value() ? 1 : 0);
        sqlite3_bind_int64(query.get(), 3, request.story_time.value_or(0));
        sqlite3_bind_int(query.get(), 4, request.story_time.has_value() ? 1 : 0);
        sqlite3_bind_int64(query.get(), 5, request.story_time.value_or(0));
        sqlite3_bind_int(query.get(), 6, request.author_view ? 1 : 0); bindText(query.get(), 7, request.actor_id);
        std::vector<std::string> ids;
        while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        auto fold = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : static_cast<char>(character);
            });
            return value;
        };
        const auto needle = fold(request.query);
        std::vector<xuyan::domain::RetrievalHit> hits;
        for (const auto& id : ids) {
            auto loaded = loadEntity(id);
            if (!loaded.ok()) throw std::runtime_error(loaded.error->message);
            const auto& entity_value = *loaded.value;
            int score = needle.empty() ? 1 : 0;
            auto contains = [&](const std::string& value) { return fold(value).find(needle) != std::string::npos; };
            if (!needle.empty()) {
                if (contains(entity_value.name)) score += 100;
                for (const auto& alias : entity_value.aliases) if (contains(alias)) score += 90;
                for (const auto& tag : entity_value.tags) if (contains(tag)) score += 50;
                if (contains(entity_value.description)) score += 30;
                if (contains(entity_value.attributes_json)) score += 10;
            }
            if (score > 0) hits.push_back({entity_value, score});
        }
        std::sort(hits.begin(), hits.end(), [](const auto& left, const auto& right) {
            return left.lexical_score != right.lexical_score ? left.lexical_score > right.lexical_score
                : left.entity.name < right.entity.name;
        });
        if (hits.size() > static_cast<std::size_t>(request.limit)) hits.resize(static_cast<std::size_t>(request.limit));
        return Result<std::vector<xuyan::domain::RetrievalHit>>::success(std::move(hits));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::RetrievalHit>>::failure(storageError(exception)); }
}

Result<xuyan::domain::WorldVersion> WorkspaceRepository::publishWorldVersion(
    const std::string& command_id, const std::string& world_id, const std::string& parent_id) {
    if (command_id.empty() || world_id.empty()) return Result<xuyan::domain::WorldVersion>::failure(
        {ErrorCode::validation_failed, "发布世界版本缺少命令或世界 ID", false, "选择世界后重试"});
    try {
        Transaction transaction(database_);
        if (!parent_id.empty()) {
            Statement parent(database_, "SELECT 1 FROM world_version WHERE id=? AND world_id=?");
            bindText(parent.get(), 1, parent_id); bindText(parent.get(), 2, world_id);
            if (sqlite3_step(parent.get()) != SQLITE_ROW) return Result<xuyan::domain::WorldVersion>::failure(
                {ErrorCode::missing_context, "父世界版本不存在或属于其他世界", false, "刷新版本列表"});
        }
        xuyan::domain::WorldVersion version;
        version.id = "world-version-" + xuyan::domain::sha256(command_id).substr(0, 24);
        version.world_id = world_id; version.parent_id = parent_id; version.published_at = utcNow();
        Statement members(database_, "SELECT e.id,e.head_revision,COALESCE(s.revision,0) FROM world_entity e LEFT JOIN entity_retrieval_scope s ON s.entity_id=e.id WHERE e.world_id=? AND e.deleted=0 ORDER BY e.id");
        bindText(members.get(), 1, world_id);
        std::ostringstream content;
        while (sqlite3_step(members.get()) == SQLITE_ROW) {
            xuyan::domain::WorldVersionMember member{columnText(members.get(), 0), sqlite3_column_int(members.get(), 1), sqlite3_column_int(members.get(), 2)};
            version.members.push_back(member); content << member.entity_id << ':' << member.entity_revision << ':' << member.retrieval_scope_revision << '|';
        }
        version.content_hash = xuyan::domain::sha256(world_id + '|' + content.str());
        auto checked = xuyan::domain::validateWorldVersion(version);
        if (!checked.ok()) return checked;
        version = std::move(*checked.value);
        const auto payload = world_id + '|' + parent_id + '|' + version.content_hash;
        Statement replay(database_, "SELECT payload_hash,version_id FROM world_publish_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::WorldVersion>::failure(
                {ErrorCode::command_conflict, "命令标识已用于其他世界版本", false, "生成新的命令标识"});
            auto result = readWorldVersion(database_, columnText(replay.get(), 1)); transaction.commit();
            return Result<xuyan::domain::WorldVersion>::success(std::move(result));
        }
        Statement insert(database_, "INSERT INTO world_version(id,world_id,parent_id,status,content_hash,published_at) VALUES(?,?,?,?,?,?)");
        bindText(insert.get(), 1, version.id); bindText(insert.get(), 2, version.world_id); bindText(insert.get(), 3, version.parent_id);
        bindText(insert.get(), 4, version.status); bindText(insert.get(), 5, version.content_hash); bindText(insert.get(), 6, version.published_at);
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& member : version.members) {
            Statement add(database_, "INSERT INTO world_version_member(version_id,entity_id,entity_revision) VALUES(?,?,?)");
            bindText(add.get(), 1, version.id); bindText(add.get(), 2, member.entity_id); sqlite3_bind_int(add.get(), 3, member.entity_revision);
            if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            Statement add_scope(database_, "INSERT INTO world_version_member_scope(version_id,entity_id,scope_revision) VALUES(?,?,?)");
            bindText(add_scope.get(), 1, version.id); bindText(add_scope.get(), 2, member.entity_id);
            sqlite3_bind_int(add_scope.get(), 3, member.retrieval_scope_revision);
            if (sqlite3_step(add_scope.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement log(database_, "INSERT INTO world_publish_command_log(command_id,payload_hash,version_id,created_at) VALUES(?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, version.id); bindText(log.get(), 4, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::WorldVersion>::success(std::move(version));
    } catch (const std::exception& exception) { return Result<xuyan::domain::WorldVersion>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::WorldVersion>> WorkspaceRepository::listWorldVersions(const std::string& world_id) {
    try {
        Statement query(database_, "SELECT id FROM world_version WHERE world_id=? ORDER BY published_at,id"); bindText(query.get(), 1, world_id);
        std::vector<std::string> ids; while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        std::vector<xuyan::domain::WorldVersion> result; result.reserve(ids.size());
        for (const auto& id : ids) result.push_back(readWorldVersion(database_, id));
        return Result<std::vector<xuyan::domain::WorldVersion>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::WorldVersion>>::failure(storageError(exception)); }
}

Result<xuyan::domain::WorldVersion> WorkspaceRepository::loadWorldVersion(const std::string& version_id) {
    try { return Result<xuyan::domain::WorldVersion>::success(readWorldVersion(database_, version_id)); }
    catch (const std::exception& exception) { return Result<xuyan::domain::WorldVersion>::failure(storageError(exception)); }
}

Result<xuyan::domain::HistoricalSnapshot> WorkspaceRepository::createHistoricalSnapshot(
    const std::string& command_id, const std::string& world_version_id, std::int64_t story_time) {
    if (command_id.empty() || world_version_id.empty()) return Result<xuyan::domain::HistoricalSnapshot>::failure(
        {ErrorCode::validation_failed, "历史快照缺少命令或世界版本", false, "选择已发布世界版本"});
    try {
        Transaction transaction(database_);
        const auto version = readWorldVersion(database_, world_version_id);
        xuyan::domain::HistoricalSnapshot snapshot;
        snapshot.id = "snapshot-" + xuyan::domain::sha256(command_id).substr(0, 24);
        snapshot.world_version_id = world_version_id; snapshot.story_time = story_time;
        std::ostringstream content;
        for (const auto& member : version.members) {
            const auto entity = readEntityRevision(database_, member.entity_id, member.entity_revision);
            const auto marker = entity.attributes_json.find("\"story_time_unknown\"");
            const auto is_unknown = marker != std::string::npos
                && entity.attributes_json.find("true", marker) != std::string::npos;
            if (is_unknown) { snapshot.unresolved_entity_ids.push_back(member.entity_id); continue; }
            bool active = true;
            Statement scope(database_, "SELECT has_valid_from,valid_from,has_valid_to,valid_to FROM entity_retrieval_scope_history WHERE entity_id=? AND revision=?");
            bindText(scope.get(), 1, member.entity_id);
            sqlite3_bind_int(scope.get(), 2, member.retrieval_scope_revision);
            if (sqlite3_step(scope.get()) == SQLITE_ROW) {
                if (sqlite3_column_int(scope.get(), 0) && sqlite3_column_int64(scope.get(), 1) > story_time) active = false;
                if (sqlite3_column_int(scope.get(), 2) && sqlite3_column_int64(scope.get(), 3) < story_time) active = false;
            }
            if (active) snapshot.included_members.push_back(member);
        }
        std::sort(snapshot.included_members.begin(), snapshot.included_members.end(), [](const auto& left, const auto& right) {
            return left.entity_id < right.entity_id;
        });
        std::sort(snapshot.unresolved_entity_ids.begin(), snapshot.unresolved_entity_ids.end());
        content << world_version_id << '|' << story_time << '|';
        for (const auto& member : snapshot.included_members) content << member.entity_id << ':' << member.entity_revision << ':' << member.retrieval_scope_revision << '|';
        content << "unknown|"; for (const auto& id : snapshot.unresolved_entity_ids) content << id << '|';
        snapshot.content_hash = xuyan::domain::sha256(content.str());
        auto checked = xuyan::domain::validateHistoricalSnapshot(snapshot);
        if (!checked.ok()) return checked;
        snapshot = std::move(*checked.value);
        const auto payload = world_version_id + '|' + std::to_string(story_time) + '|' + snapshot.content_hash;
        Statement replay(database_, "SELECT payload_hash,snapshot_id FROM historical_snapshot_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::HistoricalSnapshot>::failure(
                {ErrorCode::command_conflict, "命令标识已用于其他历史快照", false, "生成新的命令标识"});
            const auto id = columnText(replay.get(), 1);
            Statement existing(database_, "SELECT content_hash FROM historical_snapshot WHERE id=?"); bindText(existing.get(), 1, id);
            if (sqlite3_step(existing.get()) != SQLITE_ROW) throw std::runtime_error("历史快照重放结果缺失");
            snapshot.id = id; snapshot.content_hash = columnText(existing.get(), 0); transaction.commit();
            return Result<xuyan::domain::HistoricalSnapshot>::success(std::move(snapshot));
        }
        Statement insert(database_, "INSERT INTO historical_snapshot(id,world_version_id,story_time,content_hash,created_at) VALUES(?,?,?,?,?)");
        bindText(insert.get(), 1, snapshot.id); bindText(insert.get(), 2, world_version_id);
        sqlite3_bind_int64(insert.get(), 3, story_time); bindText(insert.get(), 4, snapshot.content_hash); bindText(insert.get(), 5, utcNow());
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& member : snapshot.included_members) {
            Statement add(database_, "INSERT INTO historical_snapshot_member(snapshot_id,entity_id,entity_revision) VALUES(?,?,?)");
            bindText(add.get(), 1, snapshot.id); bindText(add.get(), 2, member.entity_id); sqlite3_bind_int(add.get(), 3, member.entity_revision);
            if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        for (const auto& id : snapshot.unresolved_entity_ids) {
            Statement add(database_, "INSERT INTO historical_snapshot_unresolved(snapshot_id,entity_id) VALUES(?,?)");
            bindText(add.get(), 1, snapshot.id); bindText(add.get(), 2, id);
            if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement log(database_, "INSERT INTO historical_snapshot_command_log(command_id,payload_hash,snapshot_id,created_at) VALUES(?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, snapshot.id); bindText(log.get(), 4, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::HistoricalSnapshot>::success(std::move(snapshot));
    } catch (const std::exception& exception) { return Result<xuyan::domain::HistoricalSnapshot>::failure(storageError(exception)); }
}

Result<xuyan::domain::TimelineEvent> WorkspaceRepository::saveTimelineEvent(
    const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision) {
    auto valid = xuyan::domain::validateTimelineEvent(std::move(event)); if (!valid.ok()) return valid; event = std::move(*valid.value);
    std::ostringstream payload_builder; payload_builder << event.id << '|' << event.world_id << '|' << event.name << '|'
        << (event.story_time ? std::to_string(*event.story_time) : "null") << '|' << event.narrative_order << '|'
        << event.relative_time << '|' << event.truth_status;
    for (const auto& id : event.prerequisites) payload_builder << "|p:" << id;
    for (const auto& id : event.causes) payload_builder << "|c:" << id;
    for (const auto& id : event.results) payload_builder << "|r:" << id;
    const auto payload = payload_builder.str();
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,record_type,record_id FROM world_graph_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "timeline")
                return Result<xuyan::domain::TimelineEvent>::failure({ErrorCode::command_conflict, "命令标识已用于其他世界视图记录", false, "生成新的命令标识"});
            auto result = readTimelineEvent(database_, columnText(replay.get(), 2)); transaction.commit();
            return Result<xuyan::domain::TimelineEvent>::success(std::move(result));
        }
        int revision = 0; { Statement current(database_, "SELECT revision FROM timeline_event WHERE id=?"); bindText(current.get(), 1, event.id); if (sqlite3_step(current.get()) == SQLITE_ROW) revision = sqlite3_column_int(current.get(), 0); }
        if (revision != expected_revision) return Result<xuyan::domain::TimelineEvent>::failure({ErrorCode::revision_conflict, "时间事件修订已变化", false, "刷新后重试"});
        event.revision = expected_revision + 1;
        Statement upsert(database_, "INSERT INTO timeline_event(id,world_id,name,has_story_time,story_time,narrative_order,relative_time,truth_status,revision) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET world_id=excluded.world_id,name=excluded.name,has_story_time=excluded.has_story_time,story_time=excluded.story_time,narrative_order=excluded.narrative_order,relative_time=excluded.relative_time,truth_status=excluded.truth_status,revision=excluded.revision");
        bindText(upsert.get(), 1, event.id); bindText(upsert.get(), 2, event.world_id); bindText(upsert.get(), 3, event.name);
        sqlite3_bind_int(upsert.get(), 4, event.story_time.has_value() ? 1 : 0); sqlite3_bind_int64(upsert.get(), 5, event.story_time.value_or(0));
        sqlite3_bind_int(upsert.get(), 6, event.narrative_order); bindText(upsert.get(), 7, event.relative_time);
        bindText(upsert.get(), 8, event.truth_status); sqlite3_bind_int(upsert.get(), 9, event.revision);
        if (sqlite3_step(upsert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement clear(database_, "DELETE FROM timeline_event_edge WHERE event_id=?"); bindText(clear.get(), 1, event.id);
        if (sqlite3_step(clear.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto insert_edges = [&](const std::vector<std::string>& ids, const char* kind) {
            for (const auto& id : ids) { Statement add(database_, "INSERT INTO timeline_event_edge(event_id,edge_kind,target_event_id) VALUES(?,?,?)");
                bindText(add.get(), 1, event.id); bindText(add.get(), 2, kind); bindText(add.get(), 3, id);
                if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); }
        };
        insert_edges(event.prerequisites, "prerequisite"); insert_edges(event.causes, "cause"); insert_edges(event.results, "result");
        Statement log(database_, "INSERT INTO world_graph_command_log(command_id,payload_hash,record_type,record_id,revision,created_at) VALUES(?,?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "timeline"); bindText(log.get(), 4, event.id);
        sqlite3_bind_int(log.get(), 5, event.revision); bindText(log.get(), 6, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::TimelineEvent>::success(std::move(event));
    } catch (const std::exception& exception) { return Result<xuyan::domain::TimelineEvent>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::TimelineEvent>> WorkspaceRepository::listTimelineEvents(
    const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time) {
    try {
        const char* sql_story = "SELECT id FROM timeline_event WHERE world_id=? AND (?=0 OR has_story_time=0 OR story_time<=?) ORDER BY has_story_time DESC,story_time,id";
        const char* sql_narrative = "SELECT id FROM timeline_event WHERE world_id=? AND (?=0 OR has_story_time=0 OR story_time<=?) ORDER BY narrative_order,id";
        Statement query(database_, narrative_order ? sql_narrative : sql_story); bindText(query.get(), 1, world_id);
        sqlite3_bind_int(query.get(), 2, maximum_story_time.has_value() ? 1 : 0); sqlite3_bind_int64(query.get(), 3, maximum_story_time.value_or(0));
        std::vector<std::string> ids; while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        std::vector<xuyan::domain::TimelineEvent> result; for (const auto& id : ids) result.push_back(readTimelineEvent(database_, id));
        return Result<std::vector<xuyan::domain::TimelineEvent>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::TimelineEvent>>::failure(storageError(exception)); }
}

Result<xuyan::domain::DirectedRelation> WorkspaceRepository::saveDirectedRelation(
    const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision) {
    auto valid = xuyan::domain::validateDirectedRelation(std::move(relation)); if (!valid.ok()) return valid; relation = std::move(*valid.value);
    std::ostringstream builder; builder << relation.id << '|' << relation.world_id << '|' << relation.from_entity_id << '|' << relation.to_entity_id
        << '|' << relation.dimension << '|' << relation.strength << '|' << (relation.valid_from ? std::to_string(*relation.valid_from) : "null")
        << '|' << (relation.valid_to ? std::to_string(*relation.valid_to) : "null") << '|' << relation.visibility << '|' << relation.evidence_status;
    for (const auto& actor : relation.actor_grants) builder << '|' << actor;
    const auto payload = builder.str();
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,record_type,record_id FROM world_graph_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "relation") return Result<xuyan::domain::DirectedRelation>::failure({ErrorCode::command_conflict, "命令标识已用于其他世界视图记录", false, "生成新的命令标识"});
            auto result = readDirectedRelation(database_, columnText(replay.get(), 2)); transaction.commit(); return Result<xuyan::domain::DirectedRelation>::success(std::move(result));
        }
        Statement endpoints(database_, "SELECT COUNT(*) FROM world_entity WHERE id IN (?,?) AND world_id=? AND deleted=0"); bindText(endpoints.get(), 1, relation.from_entity_id); bindText(endpoints.get(), 2, relation.to_entity_id); bindText(endpoints.get(), 3, relation.world_id);
        if (sqlite3_step(endpoints.get()) != SQLITE_ROW || sqlite3_column_int(endpoints.get(), 0) != 2) return Result<xuyan::domain::DirectedRelation>::failure({ErrorCode::missing_context, "关系端点不存在于当前世界", false, "刷新世界条目"});
        int revision = 0; { Statement current(database_, "SELECT revision FROM directed_relation WHERE id=?"); bindText(current.get(), 1, relation.id); if (sqlite3_step(current.get()) == SQLITE_ROW) revision = sqlite3_column_int(current.get(), 0); }
        if (revision != expected_revision) return Result<xuyan::domain::DirectedRelation>::failure({ErrorCode::revision_conflict, "关系修订已变化", false, "刷新后重试"});
        relation.revision = expected_revision + 1;
        Statement upsert(database_, "INSERT INTO directed_relation(id,world_id,from_entity_id,to_entity_id,dimension,strength,has_valid_from,valid_from,has_valid_to,valid_to,visibility,evidence_status,revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET world_id=excluded.world_id,from_entity_id=excluded.from_entity_id,to_entity_id=excluded.to_entity_id,dimension=excluded.dimension,strength=excluded.strength,has_valid_from=excluded.has_valid_from,valid_from=excluded.valid_from,has_valid_to=excluded.has_valid_to,valid_to=excluded.valid_to,visibility=excluded.visibility,evidence_status=excluded.evidence_status,revision=excluded.revision");
        bindText(upsert.get(), 1, relation.id); bindText(upsert.get(), 2, relation.world_id); bindText(upsert.get(), 3, relation.from_entity_id); bindText(upsert.get(), 4, relation.to_entity_id); bindText(upsert.get(), 5, relation.dimension); sqlite3_bind_int(upsert.get(), 6, relation.strength);
        sqlite3_bind_int(upsert.get(), 7, relation.valid_from.has_value() ? 1 : 0); sqlite3_bind_int64(upsert.get(), 8, relation.valid_from.value_or(0)); sqlite3_bind_int(upsert.get(), 9, relation.valid_to.has_value() ? 1 : 0); sqlite3_bind_int64(upsert.get(), 10, relation.valid_to.value_or(0)); bindText(upsert.get(), 11, relation.visibility); bindText(upsert.get(), 12, relation.evidence_status); sqlite3_bind_int(upsert.get(), 13, relation.revision);
        if (sqlite3_step(upsert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement clear(database_, "DELETE FROM directed_relation_grant WHERE relation_id=?"); bindText(clear.get(), 1, relation.id); if (sqlite3_step(clear.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& actor : relation.actor_grants) { Statement add(database_, "INSERT INTO directed_relation_grant(relation_id,actor_id) VALUES(?,?)"); bindText(add.get(), 1, relation.id); bindText(add.get(), 2, actor); if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); }
        Statement log(database_, "INSERT INTO world_graph_command_log(command_id,payload_hash,record_type,record_id,revision,created_at) VALUES(?,?,?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "relation"); bindText(log.get(), 4, relation.id); sqlite3_bind_int(log.get(), 5, relation.revision); bindText(log.get(), 6, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit(); return Result<xuyan::domain::DirectedRelation>::success(std::move(relation));
    } catch (const std::exception& exception) { return Result<xuyan::domain::DirectedRelation>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::DirectedRelation>> WorkspaceRepository::listDirectedRelations(
    const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
    const std::string& actor_id, bool author_view) {
    try {
        Statement query(database_, "SELECT r.id FROM directed_relation r WHERE r.world_id=? AND (?='' OR r.from_entity_id=? OR r.to_entity_id=?) AND (?=0 OR r.has_valid_from=0 OR r.valid_from<=?) AND (?=0 OR r.has_valid_to=0 OR r.valid_to>=?) AND (?=1 OR r.visibility='public' OR (r.visibility='restricted' AND EXISTS(SELECT 1 FROM directed_relation_grant g WHERE g.relation_id=r.id AND g.actor_id=?))) ORDER BY r.from_entity_id,r.to_entity_id,r.dimension,r.id");
        bindText(query.get(), 1, world_id); bindText(query.get(), 2, entity_id); bindText(query.get(), 3, entity_id); bindText(query.get(), 4, entity_id);
        sqlite3_bind_int(query.get(), 5, story_time.has_value() ? 1 : 0); sqlite3_bind_int64(query.get(), 6, story_time.value_or(0)); sqlite3_bind_int(query.get(), 7, story_time.has_value() ? 1 : 0); sqlite3_bind_int64(query.get(), 8, story_time.value_or(0)); sqlite3_bind_int(query.get(), 9, author_view ? 1 : 0); bindText(query.get(), 10, actor_id);
        std::vector<std::string> ids; while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        std::vector<xuyan::domain::DirectedRelation> result; for (const auto& id : ids) result.push_back(readDirectedRelation(database_, id));
        return Result<std::vector<xuyan::domain::DirectedRelation>>::success(std::move(result));
    } catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::DirectedRelation>>::failure(storageError(exception)); }
}

Result<xuyan::domain::LocationPlacement> WorkspaceRepository::saveLocationPlacement(
    const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision) {
    auto valid = xuyan::domain::validateLocationPlacement(std::move(placement)); if (!valid.ok()) return valid; placement = std::move(*valid.value);
    const auto payload = placement.location_id + '|' + placement.parent_location_id + '|' + (placement.image_x ? std::to_string(*placement.image_x) : "null") + '|' + (placement.image_y ? std::to_string(*placement.image_y) : "null") + '|' + placement.background_asset_ref + '|' + placement.evidence_status;
    try {
        Transaction transaction(database_); Statement replay(database_, "SELECT payload_hash,record_type,record_id FROM world_graph_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) { if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "location") return Result<xuyan::domain::LocationPlacement>::failure({ErrorCode::command_conflict, "命令标识已用于其他世界视图记录", false, "生成新的命令标识"}); auto result = readLocationPlacement(database_, columnText(replay.get(), 2)); transaction.commit(); return Result<xuyan::domain::LocationPlacement>::success(std::move(result)); }
        Statement entity(database_, "SELECT r.kind FROM world_entity e JOIN entity_revision r ON r.entity_id=e.id AND r.revision=e.head_revision WHERE e.id=? AND e.deleted=0"); bindText(entity.get(), 1, placement.location_id);
        if (sqlite3_step(entity.get()) != SQLITE_ROW || columnText(entity.get(), 0) != "location") return Result<xuyan::domain::LocationPlacement>::failure({ErrorCode::missing_context, "地图标注必须引用有效地点条目", false, "选择地点条目"});
        if (!placement.parent_location_id.empty()) { Statement parent(database_, "SELECT 1 FROM location_placement WHERE location_id=?"); bindText(parent.get(), 1, placement.parent_location_id); if (sqlite3_step(parent.get()) != SQLITE_ROW) return Result<xuyan::domain::LocationPlacement>::failure({ErrorCode::missing_context, "父地点尚未加入地图", false, "先保存父地点"}); std::string cursor = placement.parent_location_id; for (int depth = 0; depth < 128 && !cursor.empty(); ++depth) { if (cursor == placement.location_id) return Result<xuyan::domain::LocationPlacement>::failure({ErrorCode::rule_conflict, "地点层级不能形成环", false, "选择其他父地点"}); Statement ancestor(database_, "SELECT parent_location_id FROM location_placement WHERE location_id=?"); bindText(ancestor.get(), 1, cursor); cursor = sqlite3_step(ancestor.get()) == SQLITE_ROW ? columnText(ancestor.get(), 0) : std::string{}; } }
        int revision = 0; { Statement current(database_, "SELECT revision FROM location_placement WHERE location_id=?"); bindText(current.get(), 1, placement.location_id); if (sqlite3_step(current.get()) == SQLITE_ROW) revision = sqlite3_column_int(current.get(), 0); } if (revision != expected_revision) return Result<xuyan::domain::LocationPlacement>::failure({ErrorCode::revision_conflict, "地点标注修订已变化", false, "刷新后重试"}); placement.revision = expected_revision + 1;
        Statement upsert(database_, "INSERT INTO location_placement(location_id,parent_location_id,has_image_point,image_x,image_y,background_asset_ref,evidence_status,revision) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(location_id) DO UPDATE SET parent_location_id=excluded.parent_location_id,has_image_point=excluded.has_image_point,image_x=excluded.image_x,image_y=excluded.image_y,background_asset_ref=excluded.background_asset_ref,evidence_status=excluded.evidence_status,revision=excluded.revision"); bindText(upsert.get(), 1, placement.location_id); bindText(upsert.get(), 2, placement.parent_location_id); sqlite3_bind_int(upsert.get(), 3, placement.image_x.has_value() ? 1 : 0); sqlite3_bind_int(upsert.get(), 4, placement.image_x.value_or(0)); sqlite3_bind_int(upsert.get(), 5, placement.image_y.value_or(0)); bindText(upsert.get(), 6, placement.background_asset_ref); bindText(upsert.get(), 7, placement.evidence_status); sqlite3_bind_int(upsert.get(), 8, placement.revision); if (sqlite3_step(upsert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement log(database_, "INSERT INTO world_graph_command_log(command_id,payload_hash,record_type,record_id,revision,created_at) VALUES(?,?,?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "location"); bindText(log.get(), 4, placement.location_id); sqlite3_bind_int(log.get(), 5, placement.revision); bindText(log.get(), 6, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); transaction.commit(); return Result<xuyan::domain::LocationPlacement>::success(std::move(placement));
    } catch (const std::exception& exception) { return Result<xuyan::domain::LocationPlacement>::failure(storageError(exception)); }
}

Result<xuyan::domain::TravelRoute> WorkspaceRepository::saveTravelRoute(
    const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision) {
    auto valid = xuyan::domain::validateTravelRoute(std::move(route)); if (!valid.ok()) return valid; route = std::move(*valid.value);
    const auto payload = route.id + '|' + route.from_location_id + '|' + route.to_location_id + '|' + (route.travel_minutes ? std::to_string(*route.travel_minutes) : "null") + '|' + (route.bidirectional ? "1" : "0") + '|' + route.evidence_status;
    try {
        Transaction transaction(database_); Statement replay(database_, "SELECT payload_hash,record_type,record_id FROM world_graph_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id); if (sqlite3_step(replay.get()) == SQLITE_ROW) { if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "route") return Result<xuyan::domain::TravelRoute>::failure({ErrorCode::command_conflict, "命令标识已用于其他世界视图记录", false, "生成新的命令标识"}); auto result = readTravelRoute(database_, columnText(replay.get(), 2)); transaction.commit(); return Result<xuyan::domain::TravelRoute>::success(std::move(result)); }
        Statement endpoints(database_, "SELECT COUNT(*) FROM location_placement WHERE location_id IN (?,?)"); bindText(endpoints.get(), 1, route.from_location_id); bindText(endpoints.get(), 2, route.to_location_id); if (sqlite3_step(endpoints.get()) != SQLITE_ROW || sqlite3_column_int(endpoints.get(), 0) != 2) return Result<xuyan::domain::TravelRoute>::failure({ErrorCode::missing_context, "路线端点尚未加入地图", false, "先保存地点标注"});
        int revision = 0; { Statement current(database_, "SELECT revision FROM travel_route WHERE id=?"); bindText(current.get(), 1, route.id); if (sqlite3_step(current.get()) == SQLITE_ROW) revision = sqlite3_column_int(current.get(), 0); } if (revision != expected_revision) return Result<xuyan::domain::TravelRoute>::failure({ErrorCode::revision_conflict, "路线修订已变化", false, "刷新后重试"}); route.revision = expected_revision + 1;
        Statement upsert(database_, "INSERT INTO travel_route(id,from_location_id,to_location_id,has_travel_minutes,travel_minutes,bidirectional,evidence_status,revision) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET from_location_id=excluded.from_location_id,to_location_id=excluded.to_location_id,has_travel_minutes=excluded.has_travel_minutes,travel_minutes=excluded.travel_minutes,bidirectional=excluded.bidirectional,evidence_status=excluded.evidence_status,revision=excluded.revision"); bindText(upsert.get(), 1, route.id); bindText(upsert.get(), 2, route.from_location_id); bindText(upsert.get(), 3, route.to_location_id); sqlite3_bind_int(upsert.get(), 4, route.travel_minutes.has_value() ? 1 : 0); sqlite3_bind_int(upsert.get(), 5, route.travel_minutes.value_or(0)); sqlite3_bind_int(upsert.get(), 6, route.bidirectional ? 1 : 0); bindText(upsert.get(), 7, route.evidence_status); sqlite3_bind_int(upsert.get(), 8, route.revision); if (sqlite3_step(upsert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement log(database_, "INSERT INTO world_graph_command_log(command_id,payload_hash,record_type,record_id,revision,created_at) VALUES(?,?,?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "route"); bindText(log.get(), 4, route.id); sqlite3_bind_int(log.get(), 5, route.revision); bindText(log.get(), 6, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); transaction.commit(); return Result<xuyan::domain::TravelRoute>::success(std::move(route));
    } catch (const std::exception& exception) { return Result<xuyan::domain::TravelRoute>::failure(storageError(exception)); }
}

Result<xuyan::domain::MapView> WorkspaceRepository::loadMapView(const std::string& world_id) {
    try {
        xuyan::domain::MapView view; Statement locations(database_, "SELECT p.location_id FROM location_placement p JOIN world_entity e ON e.id=p.location_id WHERE e.world_id=? AND e.deleted=0 ORDER BY p.location_id"); bindText(locations.get(), 1, world_id); std::vector<std::string> ids; while (sqlite3_step(locations.get()) == SQLITE_ROW) ids.push_back(columnText(locations.get(), 0)); for (const auto& id : ids) view.locations.push_back(readLocationPlacement(database_, id));
        Statement routes(database_, "SELECT r.id FROM travel_route r JOIN world_entity a ON a.id=r.from_location_id JOIN world_entity b ON b.id=r.to_location_id WHERE a.world_id=? AND b.world_id=? ORDER BY r.id"); bindText(routes.get(), 1, world_id); bindText(routes.get(), 2, world_id); std::vector<std::string> route_ids; while (sqlite3_step(routes.get()) == SQLITE_ROW) route_ids.push_back(columnText(routes.get(), 0)); for (const auto& id : route_ids) view.routes.push_back(readTravelRoute(database_, id));
        return Result<xuyan::domain::MapView>::success(std::move(view));
    } catch (const std::exception& exception) { return Result<xuyan::domain::MapView>::failure(storageError(exception)); }
}

Result<xuyan::domain::CharacterInstance> WorkspaceRepository::createCharacterInstance(
    const std::string& command_id, xuyan::domain::CharacterInstance instance) {
    auto valid = xuyan::domain::validateCharacterInstance(std::move(instance)); if (!valid.ok()) return valid; instance = std::move(*valid.value);
    std::ostringstream builder; builder << instance.id << '|' << instance.blueprint_id << '|' << instance.blueprint_version << '|'
        << instance.world_version_id << '|' << instance.snapshot_id << '|' << instance.adaptation_json << '|' << instance.knowledge_policy;
    for (const auto& conflict : instance.conflicts) builder << '|' << conflict;
    const auto payload = builder.str();
    try {
        Transaction transaction(database_); Statement replay(database_, "SELECT payload_hash,instance_id FROM character_instance_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) { if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::CharacterInstance>::failure({ErrorCode::command_conflict, "命令标识已用于其他人物实例", false, "生成新的命令标识"}); auto result = readCharacterInstance(database_, columnText(replay.get(), 1)); transaction.commit(); return Result<xuyan::domain::CharacterInstance>::success(std::move(result)); }
        Statement blueprint(database_, "SELECT name FROM character_blueprint_version WHERE blueprint_id=? AND version=?"); bindText(blueprint.get(), 1, instance.blueprint_id); sqlite3_bind_int(blueprint.get(), 2, instance.blueprint_version);
        if (sqlite3_step(blueprint.get()) != SQLITE_ROW) return Result<xuyan::domain::CharacterInstance>::failure({ErrorCode::missing_context, "人物卡版本不存在", false, "刷新人物卡"});
        instance.name = columnText(blueprint.get(), 0);
        const auto snapshot = readHistoricalSnapshot(database_, instance.snapshot_id);
        if (snapshot.world_version_id != instance.world_version_id) return Result<xuyan::domain::CharacterInstance>::failure({ErrorCode::validation_failed, "人物实例的快照不属于所选世界版本", false, "重新选择入场点"});
        instance.revision = 1;
        Statement insert(database_, "INSERT INTO character_instance(id,blueprint_id,blueprint_version,world_version_id,snapshot_id,name,adaptation_json,knowledge_policy,memory_json,status,revision) VALUES(?,?,?,?,?,?,?,?,?,?,?)"); bindText(insert.get(), 1, instance.id); bindText(insert.get(), 2, instance.blueprint_id); sqlite3_bind_int(insert.get(), 3, instance.blueprint_version); bindText(insert.get(), 4, instance.world_version_id); bindText(insert.get(), 5, instance.snapshot_id); bindText(insert.get(), 6, instance.name); bindText(insert.get(), 7, instance.adaptation_json); bindText(insert.get(), 8, instance.knowledge_policy); bindText(insert.get(), 9, instance.memory_json); bindText(insert.get(), 10, instance.status); sqlite3_bind_int(insert.get(), 11, 1); if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& conflict : instance.conflicts) { Statement add(database_, "INSERT INTO character_instance_conflict(instance_id,message) VALUES(?,?)"); bindText(add.get(), 1, instance.id); bindText(add.get(), 2, conflict); if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); }
        Statement log(database_, "INSERT INTO character_instance_command_log(command_id,payload_hash,instance_id,revision,created_at) VALUES(?,?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, instance.id); sqlite3_bind_int(log.get(), 4, 1); bindText(log.get(), 5, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); transaction.commit(); return Result<xuyan::domain::CharacterInstance>::success(std::move(instance));
    } catch (const std::exception& exception) { return Result<xuyan::domain::CharacterInstance>::failure(storageError(exception)); }
}

Result<xuyan::domain::CharacterInstance> WorkspaceRepository::loadCharacterInstance(const std::string& instance_id) {
    try { return Result<xuyan::domain::CharacterInstance>::success(readCharacterInstance(database_, instance_id)); }
    catch (const std::exception& exception) { return Result<xuyan::domain::CharacterInstance>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::CharacterInstance>> WorkspaceRepository::listCharacterInstances(const std::string& world_version_id) {
    try { Statement query(database_, "SELECT id FROM character_instance WHERE world_version_id=? ORDER BY name,id"); bindText(query.get(), 1, world_version_id); std::vector<std::string> ids; while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0)); std::vector<xuyan::domain::CharacterInstance> result; for (const auto& id : ids) result.push_back(readCharacterInstance(database_, id)); return Result<std::vector<xuyan::domain::CharacterInstance>>::success(std::move(result)); }
    catch (const std::exception& exception) { return Result<std::vector<xuyan::domain::CharacterInstance>>::failure(storageError(exception)); }
}

Result<xuyan::domain::CharacterInstance> WorkspaceRepository::saveCharacterInstanceMemory(
    const std::string& command_id, const std::string& instance_id, int expected_revision, const std::string& memory_json) {
    const auto payload = "memory|" + instance_id + '|' + std::to_string(expected_revision) + '|' + memory_json;
    try {
        Transaction transaction(database_); Statement replay(database_, "SELECT payload_hash,instance_id FROM character_instance_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id); if (sqlite3_step(replay.get()) == SQLITE_ROW) { if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::CharacterInstance>::failure({ErrorCode::command_conflict, "命令标识已用于其他人物实例操作", false, "生成新的命令标识"}); auto result = readCharacterInstance(database_, columnText(replay.get(), 1)); transaction.commit(); return Result<xuyan::domain::CharacterInstance>::success(std::move(result)); }
        auto current = readCharacterInstance(database_, instance_id); if (current.revision != expected_revision) return Result<xuyan::domain::CharacterInstance>::failure({ErrorCode::revision_conflict, "人物实例记忆修订已变化", false, "刷新后重试"});
        Statement update(database_, "UPDATE character_instance SET memory_json=?,revision=revision+1 WHERE id=? AND revision=?"); bindText(update.get(), 1, memory_json); bindText(update.get(), 2, instance_id); sqlite3_bind_int(update.get(), 3, expected_revision); if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("人物实例记忆并发更新失败"); current.memory_json = memory_json; current.revision = expected_revision + 1;
        Statement log(database_, "INSERT INTO character_instance_command_log(command_id,payload_hash,instance_id,revision,created_at) VALUES(?,?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, instance_id); sqlite3_bind_int(log.get(), 4, current.revision); bindText(log.get(), 5, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); transaction.commit(); return Result<xuyan::domain::CharacterInstance>::success(std::move(current));
    } catch (const std::exception& exception) { return Result<xuyan::domain::CharacterInstance>::failure(storageError(exception)); }
}

Result<xuyan::domain::BranchRootBinding> WorkspaceRepository::bindBranchRoot(
    const std::string& command_id, xuyan::domain::BranchRootBinding binding) {
    auto valid = xuyan::domain::validateBranchRootBinding(std::move(binding)); if (!valid.ok()) return valid; binding = std::move(*valid.value);
    std::string payload = binding.branch_id + '|' + binding.world_version_id + '|' + binding.snapshot_id + '|' + binding.history_mode + '|' + binding.root_hash;
    for (const auto& id : binding.character_instance_ids) payload += '|' + id;
    try {
        Transaction transaction(database_); Statement replay(database_, "SELECT payload_hash,branch_id FROM branch_binding_command_log WHERE command_id=?"); bindText(replay.get(), 1, command_id); if (sqlite3_step(replay.get()) == SQLITE_ROW) { if (columnText(replay.get(), 0) != payload) return Result<xuyan::domain::BranchRootBinding>::failure({ErrorCode::command_conflict, "命令标识已用于其他分支根", false, "生成新的命令标识"}); auto result = readBranchRootBinding(database_, columnText(replay.get(), 1)); transaction.commit(); return Result<xuyan::domain::BranchRootBinding>::success(std::move(result)); }
        Statement branch(database_, "SELECT 1 FROM branch WHERE id=?"); bindText(branch.get(), 1, binding.branch_id); if (sqlite3_step(branch.get()) != SQLITE_ROW) return Result<xuyan::domain::BranchRootBinding>::failure({ErrorCode::missing_context, "分支不存在", false, "刷新分支列表"});
        const auto snapshot = readHistoricalSnapshot(database_, binding.snapshot_id); if (snapshot.world_version_id != binding.world_version_id) return Result<xuyan::domain::BranchRootBinding>::failure({ErrorCode::validation_failed, "分支根快照不属于所选世界版本", false, "重新选择快照"});
        Statement existing(database_, "SELECT 1 FROM branch_root_binding WHERE branch_id=?"); bindText(existing.get(), 1, binding.branch_id); if (sqlite3_step(existing.get()) == SQLITE_ROW) return Result<xuyan::domain::BranchRootBinding>::failure({ErrorCode::revision_conflict, "分支根已固定，不能原地改绑", false, "创建新分支"});
        std::vector<xuyan::domain::CharacterInstance> instances;
        for (const auto& id : binding.character_instance_ids) { const auto instance = readCharacterInstance(database_, id); if (instance.world_version_id != binding.world_version_id || instance.snapshot_id != binding.snapshot_id || instance.status != "ready") return Result<xuyan::domain::BranchRootBinding>::failure({ErrorCode::validation_failed, "人物实例未就绪或不属于同一版本/快照", false, "解决入场冲突后重试"}); instances.push_back(instance); }
        std::ostringstream root; root << binding.branch_id << '|' << binding.world_version_id << '|' << binding.snapshot_id << '|' << binding.history_mode;
        for (const auto& instance : instances) root << '|' << instance.id << ':' << instance.revision << ':' << instance.blueprint_id << ':' << instance.blueprint_version;
        binding.root_hash = xuyan::domain::sha256(root.str());
        Statement insert(database_, "INSERT INTO branch_root_binding(branch_id,world_version_id,snapshot_id,history_mode,root_hash) VALUES(?,?,?,?,?)"); bindText(insert.get(), 1, binding.branch_id); bindText(insert.get(), 2, binding.world_version_id); bindText(insert.get(), 3, binding.snapshot_id); bindText(insert.get(), 4, binding.history_mode); bindText(insert.get(), 5, binding.root_hash); if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (const auto& instance : instances) { Statement add(database_, "INSERT INTO branch_character_instance(branch_id,instance_id) VALUES(?,?)"); bindText(add.get(), 1, binding.branch_id); bindText(add.get(), 2, instance.id); if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); Statement pin(database_, "INSERT INTO branch_character_instance_revision(branch_id,instance_id,instance_revision) VALUES(?,?,?)"); bindText(pin.get(), 1, binding.branch_id); bindText(pin.get(), 2, instance.id); sqlite3_bind_int(pin.get(), 3, instance.revision); if (sqlite3_step(pin.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); }
        Statement log(database_, "INSERT INTO branch_binding_command_log(command_id,payload_hash,branch_id,created_at) VALUES(?,?,?,?)"); bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, binding.branch_id); bindText(log.get(), 4, utcNow()); if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_)); transaction.commit(); return Result<xuyan::domain::BranchRootBinding>::success(std::move(binding));
    } catch (const std::exception& exception) { return Result<xuyan::domain::BranchRootBinding>::failure(storageError(exception)); }
}

Result<xuyan::domain::BranchRootBinding> WorkspaceRepository::loadBranchRootBinding(const std::string& branch_id) {
    try { return Result<xuyan::domain::BranchRootBinding>::success(readBranchRootBinding(database_, branch_id)); }
    catch (const std::exception& exception) {
        return Result<xuyan::domain::BranchRootBinding>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationSession> WorkspaceRepository::createSimulationSession(
    const std::string& command_id, xuyan::domain::SimulationSession session) {
    auto valid = xuyan::domain::validateSimulationSession(std::move(session));
    if (!valid.ok()) return valid;
    session = std::move(*valid.value);
    std::ostringstream payload_builder;
    payload_builder << session.id << '|' << session.branch_id << '|' << session.max_turns << '|'
                    << session.continuous << '|' << session.no_progress_limit << '|' << session.max_calls;
    for (const auto& actor : session.actors)
        payload_builder << '|' << actor.actor_id << ':' << actor.provider_connection_id << ':' << actor.model_id;
    const auto payload = payload_builder.str();
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "session")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationSession(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationSession>::success(std::move(result));
        }
        Statement branch(database_, "SELECT 1 FROM branch WHERE id=?");
        bindText(branch.get(), 1, session.branch_id);
        if (sqlite3_step(branch.get()) != SQLITE_ROW)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::missing_context, "推演分支不存在", false, "刷新分支后重试"});
        session.status = "ready";
        session.used_calls = session.reserved_calls = session.unknown_calls = 0;
        session.pause_requested = session.cancel_requested = false;
        session.revision = 1;
        const auto now = utcNow();
        Statement insert(database_, "INSERT INTO simulation_session(id,branch_id,status,max_turns,continuous,no_progress_limit,max_calls,used_calls,reserved_calls,unknown_calls,pause_requested,cancel_requested,revision,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        bindText(insert.get(), 1, session.id); bindText(insert.get(), 2, session.branch_id); bindText(insert.get(), 3, session.status);
        sqlite3_bind_int(insert.get(), 4, session.max_turns); sqlite3_bind_int(insert.get(), 5, session.continuous ? 1 : 0);
        sqlite3_bind_int(insert.get(), 6, session.no_progress_limit); sqlite3_bind_int(insert.get(), 7, session.max_calls);
        sqlite3_bind_int(insert.get(), 8, 0); sqlite3_bind_int(insert.get(), 9, 0); sqlite3_bind_int(insert.get(), 10, 0);
        sqlite3_bind_int(insert.get(), 11, 0); sqlite3_bind_int(insert.get(), 12, 0); sqlite3_bind_int(insert.get(), 13, 1);
        bindText(insert.get(), 14, now); bindText(insert.get(), 15, now);
        if (sqlite3_step(insert.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        for (std::size_t ordinal = 0; ordinal < session.actors.size(); ++ordinal) {
            const auto& actor = session.actors[ordinal];
            Statement add(database_, "INSERT INTO simulation_actor_binding(session_id,ordinal,actor_id,provider_connection_id,model_id) VALUES(?,?,?,?,?)");
            bindText(add.get(), 1, session.id); sqlite3_bind_int(add.get(), 2, static_cast<int>(ordinal));
            bindText(add.get(), 3, actor.actor_id); bindText(add.get(), 4, actor.provider_connection_id); bindText(add.get(), 5, actor.model_id);
            if (sqlite3_step(add.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        }
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "session");
        bindText(log.get(), 4, session.id); bindText(log.get(), 5, now);
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        transaction.commit();
        return Result<xuyan::domain::SimulationSession>::success(std::move(session));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationSession>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationSession> WorkspaceRepository::loadSimulationSession(const std::string& session_id) {
    try { return Result<xuyan::domain::SimulationSession>::success(readSimulationSession(database_, session_id)); }
    catch (const std::exception& exception) { return Result<xuyan::domain::SimulationSession>::failure(storageError(exception)); }
}

Result<std::vector<xuyan::domain::SimulationSession>> WorkspaceRepository::listSimulationSessions() {
    try {
        Statement query(database_, "SELECT id FROM simulation_session ORDER BY created_at DESC,id");
        std::vector<std::string> ids;
        while (sqlite3_step(query.get()) == SQLITE_ROW) ids.push_back(columnText(query.get(), 0));
        std::vector<xuyan::domain::SimulationSession> result;
        result.reserve(ids.size());
        for (const auto& id : ids) result.push_back(readSimulationSession(database_, id));
        return Result<std::vector<xuyan::domain::SimulationSession>>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<std::vector<xuyan::domain::SimulationSession>>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationTurn> WorkspaceRepository::reserveSimulationTurn(
    const std::string& command_id, const std::string& session_id, int expected_revision,
    const std::string& actor_id, const std::string& request_hash) {
    const auto payload = session_id + '|' + std::to_string(expected_revision) + '|' + actor_id + '|' + request_hash;
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "turn")
                return Result<xuyan::domain::SimulationTurn>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationTurn(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationTurn>::success(std::move(result));
        }
        auto session = readSimulationSession(database_, session_id);
        if (session.revision != expected_revision)
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::revision_conflict, "推演会话修订已变化", true, "刷新会话后重试"});
        if (session.status != "ready" && session.status != "running")
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::rule_conflict, "当前推演会话不能开始新回合", false, "恢复会话或新建推演"});
        if (session.pause_requested || session.cancel_requested)
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::rule_conflict, "推演正在暂停或取消", false, "等待当前回合结束"});
        if (static_cast<int>(session.turns.size()) >= session.max_turns)
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::rule_conflict, "推演已达到最大回合数", false, "新建会话并重新设置上限"});
        if (session.used_calls + session.reserved_calls + session.unknown_calls >= session.max_calls)
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::rule_conflict, "模型调用硬预算已耗尽", false, "核对未知调用后新建会话"});
        const auto actor = std::find_if(session.actors.begin(), session.actors.end(), [&](const auto& value) {
            return value.actor_id == actor_id;
        });
        if (actor == session.actors.end())
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::missing_context, "人物未绑定到该推演会话", false, "刷新人物模型绑定"});
        Statement head(database_, "SELECT head_commit_id FROM branch WHERE id=?");
        bindText(head.get(), 1, session.branch_id);
        if (sqlite3_step(head.get()) != SQLITE_ROW)
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::missing_context, "推演分支不存在", false, "刷新分支"});
        const auto input_commit_id = columnText(head.get(), 0);
        const auto ordinal = static_cast<int>(session.turns.size()) + 1;
        const auto turn_id = randomId("turn");
        const auto call_id = randomId("call");
        const auto now = utcNow();
        Statement add_turn(database_, "INSERT INTO simulation_turn(id,session_id,ordinal,input_commit_id,committed_commit_id,actor_id,status,speech,operation,target_id,holder_consented,ends_scene,public_reason,draft_narration,final_narration,error_message,revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        bindText(add_turn.get(), 1, turn_id); bindText(add_turn.get(), 2, session_id); sqlite3_bind_int(add_turn.get(), 3, ordinal);
        bindText(add_turn.get(), 4, input_commit_id); bindText(add_turn.get(), 5, ""); bindText(add_turn.get(), 6, actor_id);
        bindText(add_turn.get(), 7, "requesting"); bindText(add_turn.get(), 8, ""); bindText(add_turn.get(), 9, "speak");
        bindText(add_turn.get(), 10, ""); sqlite3_bind_int(add_turn.get(), 11, 0); sqlite3_bind_int(add_turn.get(), 12, 0);
        bindText(add_turn.get(), 13, ""); bindText(add_turn.get(), 14, ""); bindText(add_turn.get(), 15, ""); bindText(add_turn.get(), 16, "");
        sqlite3_bind_int(add_turn.get(), 17, 1);
        if (sqlite3_step(add_turn.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement add_call(database_, "INSERT INTO simulation_provider_call(id,turn_id,provider_connection_id,model_id,request_hash,status,input_tokens,output_tokens,failure_kind,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
        bindText(add_call.get(), 1, call_id); bindText(add_call.get(), 2, turn_id); bindText(add_call.get(), 3, actor->provider_connection_id);
        bindText(add_call.get(), 4, actor->model_id); bindText(add_call.get(), 5, request_hash); bindText(add_call.get(), 6, "sent");
        sqlite3_bind_int(add_call.get(), 7, 0); sqlite3_bind_int(add_call.get(), 8, 0); bindText(add_call.get(), 9, "");
        bindText(add_call.get(), 10, now); bindText(add_call.get(), 11, now);
        if (sqlite3_step(add_call.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        Statement update(database_, "UPDATE simulation_session SET reserved_calls=reserved_calls+1,status='running',revision=revision+1,updated_at=? WHERE id=? AND revision=?");
        bindText(update.get(), 1, now); bindText(update.get(), 2, session_id); sqlite3_bind_int(update.get(), 3, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("预留推演回合并发更新失败");
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "turn"); bindText(log.get(), 4, turn_id); bindText(log.get(), 5, now);
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSimulationTurn(database_, turn_id);
        transaction.commit();
        return Result<xuyan::domain::SimulationTurn>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationTurn>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationSession> WorkspaceRepository::commitSimulationTurn(
    const std::string& command_id, const std::string& turn_id, int expected_session_revision,
    xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state,
    const std::string& draft_narration, int input_tokens, int output_tokens) {
    auto valid = xuyan::domain::validateActorIntent(std::move(intent));
    if (!valid.ok()) return Result<xuyan::domain::SimulationSession>::failure(*valid.error);
    intent = std::move(*valid.value);
    const auto payload = turn_id + '|' + std::to_string(expected_session_revision) + '|' + intent.actor_id + '|'
        + intent.input_commit_id + '|' + intent.operation + '|' + intent.target_id + '|' + xuyan::domain::stateHash(next_state)
        + '|' + draft_narration + '|' + std::to_string(input_tokens) + '|' + std::to_string(output_tokens);
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "session")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationSession(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationSession>::success(std::move(result));
        }
        auto turn = readSimulationTurn(database_, turn_id);
        auto session = readSimulationSession(database_, turn.session_id);
        if (session.revision != expected_session_revision)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::revision_conflict, "推演会话修订已变化", true, "刷新会话后重试"});
        const bool pending_review = turn.status == "needs_review" && turn.call.status == "completed";
        const bool fresh_response = turn.status == "requesting" && turn.call.status == "sent";
        if (!fresh_response && !pending_review)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::rule_conflict, "推演回合不再等待模型结果", false, "刷新会话"});
        if (session.cancel_requested)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::rule_conflict, "推演会话已请求取消", false, "放弃本次模型结果"});
        if (intent.actor_id != turn.actor_id || intent.input_commit_id != turn.input_commit_id)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::validation_failed, "模型意图与预留人物或输入提交不匹配", false, "丢弃响应并重新请求"});
        const auto now = utcNow();
        if (fresh_response && session.pause_requested) {
            Statement save_turn(database_, "UPDATE simulation_turn SET status='needs_review',speech=?,operation=?,target_id=?,holder_consented=?,ends_scene=?,public_reason=?,draft_narration=?,error_message='暂停期间返回，尚未提交',revision=revision+1 WHERE id=? AND status='requesting'");
            bindText(save_turn.get(), 1, intent.speech); bindText(save_turn.get(), 2, intent.operation); bindText(save_turn.get(), 3, intent.target_id);
            sqlite3_bind_int(save_turn.get(), 4, intent.holder_consented ? 1 : 0); sqlite3_bind_int(save_turn.get(), 5, intent.ends_scene ? 1 : 0);
            bindText(save_turn.get(), 6, intent.public_reason); bindText(save_turn.get(), 7, draft_narration); bindText(save_turn.get(), 8, turn_id);
            if (sqlite3_step(save_turn.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("保存暂停期间返回的意图失败");
            Statement settle_call(database_, "UPDATE simulation_provider_call SET status='completed',input_tokens=?,output_tokens=?,updated_at=? WHERE turn_id=? AND status='sent'");
            sqlite3_bind_int(settle_call.get(), 1, std::max(0, input_tokens)); sqlite3_bind_int(settle_call.get(), 2, std::max(0, output_tokens));
            bindText(settle_call.get(), 3, now); bindText(settle_call.get(), 4, turn_id);
            if (sqlite3_step(settle_call.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("结算暂停期间模型调用失败");
            Statement pause_session(database_, "UPDATE simulation_session SET status='paused',used_calls=used_calls+1,reserved_calls=reserved_calls-1,revision=revision+1,updated_at=? WHERE id=? AND revision=? AND reserved_calls>0");
            bindText(pause_session.get(), 1, now); bindText(pause_session.get(), 2, session.id); sqlite3_bind_int(pause_session.get(), 3, expected_session_revision);
            if (sqlite3_step(pause_session.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("暂停推演会话结算失败");
            Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
            bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "session"); bindText(log.get(), 4, session.id); bindText(log.get(), 5, now);
            if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
            auto result = readSimulationSession(database_, session.id);
            transaction.commit();
            return Result<xuyan::domain::SimulationSession>::success(std::move(result));
        }
        Statement head(database_, "SELECT head_commit_id FROM branch WHERE id=?");
        bindText(head.get(), 1, session.branch_id);
        if (sqlite3_step(head.get()) != SQLITE_ROW || columnText(head.get(), 0) != turn.input_commit_id)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::revision_conflict, "分支头已变化，不能提交过期意图", true, "基于新分支头重新推演"});
        const auto previous = readCommit(database_, turn.input_commit_id);
        if (next_state.turn != previous.state.turn + 1 || next_state.elapsed_ticks < previous.state.elapsed_ticks
            || next_state.revision <= previous.state.revision)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::validation_failed, "下一状态的回合、时间或修订不单调", false, "重新执行领域规则"});
        CommitView next;
        next.branch_id = session.branch_id; next.commit_id = randomId("commit"); next.parent_commit_id = turn.input_commit_id;
        next.state = std::move(next_state); next.state_hash = xuyan::domain::stateHash(next.state);
        insertSnapshot(database_, next);
        Statement update_branch(database_, "UPDATE branch SET head_commit_id=? WHERE id=? AND head_commit_id=?");
        bindText(update_branch.get(), 1, next.commit_id); bindText(update_branch.get(), 2, session.branch_id); bindText(update_branch.get(), 3, turn.input_commit_id);
        if (sqlite3_step(update_branch.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("更新推演分支头失败");
        Statement update_turn(database_, "UPDATE simulation_turn SET committed_commit_id=?,status='narration_pending',speech=?,operation=?,target_id=?,holder_consented=?,ends_scene=?,public_reason=?,draft_narration=?,error_message='',revision=revision+1 WHERE id=? AND status=?");
        bindText(update_turn.get(), 1, next.commit_id); bindText(update_turn.get(), 2, intent.speech); bindText(update_turn.get(), 3, intent.operation);
        bindText(update_turn.get(), 4, intent.target_id); sqlite3_bind_int(update_turn.get(), 5, intent.holder_consented ? 1 : 0);
        sqlite3_bind_int(update_turn.get(), 6, intent.ends_scene ? 1 : 0); bindText(update_turn.get(), 7, intent.public_reason);
        bindText(update_turn.get(), 8, draft_narration); bindText(update_turn.get(), 9, turn_id); bindText(update_turn.get(), 10, pending_review ? "needs_review" : "requesting");
        if (sqlite3_step(update_turn.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("提交推演回合并发更新失败");
        if (fresh_response) {
            Statement update_call(database_, "UPDATE simulation_provider_call SET status='completed',input_tokens=?,output_tokens=?,updated_at=? WHERE turn_id=? AND status='sent'");
            sqlite3_bind_int(update_call.get(), 1, std::max(0, input_tokens)); sqlite3_bind_int(update_call.get(), 2, std::max(0, output_tokens));
            bindText(update_call.get(), 3, now); bindText(update_call.get(), 4, turn_id);
            if (sqlite3_step(update_call.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("完成模型调用并发更新失败");
        }
        int no_progress_count = 0;
        Statement progress(database_, "SELECT operation FROM simulation_turn WHERE session_id=? AND status IN ('narration_pending','completed') ORDER BY ordinal DESC LIMIT ?");
        bindText(progress.get(), 1, session.id); sqlite3_bind_int(progress.get(), 2, session.no_progress_limit);
        while (sqlite3_step(progress.get()) == SQLITE_ROW && columnText(progress.get(), 0) == "speak") ++no_progress_count;
        std::string next_status;
        if (next.state.completed || intent.ends_scene || turn.ordinal >= session.max_turns) next_status = "completed";
        else if (session.pause_requested) next_status = "paused";
        else if (no_progress_count >= session.no_progress_limit) next_status = "blocked";
        else next_status = session.continuous ? "running" : "ready";
        const auto* session_sql = fresh_response
            ? "UPDATE simulation_session SET status=?,used_calls=used_calls+1,reserved_calls=reserved_calls-1,revision=revision+1,updated_at=? WHERE id=? AND revision=? AND reserved_calls>0"
            : "UPDATE simulation_session SET status=?,revision=revision+1,updated_at=? WHERE id=? AND revision=?";
        Statement update_session(database_, session_sql);
        bindText(update_session.get(), 1, next_status); bindText(update_session.get(), 2, now); bindText(update_session.get(), 3, session.id);
        sqlite3_bind_int(update_session.get(), 4, expected_session_revision);
        if (sqlite3_step(update_session.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("推进推演会话并发更新失败");
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "session"); bindText(log.get(), 4, session.id); bindText(log.get(), 5, now);
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSimulationSession(database_, session.id);
        transaction.commit();
        return Result<xuyan::domain::SimulationSession>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationSession>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationTurn> WorkspaceRepository::finishSimulationNarration(
    const std::string& command_id, const std::string& turn_id, const std::string& final_narration) {
    const auto payload = turn_id + '|' + final_narration;
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "turn")
                return Result<xuyan::domain::SimulationTurn>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationTurn(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationTurn>::success(std::move(result));
        }
        auto turn = readSimulationTurn(database_, turn_id);
        if (turn.status != "narration_pending")
            return Result<xuyan::domain::SimulationTurn>::failure(
                {ErrorCode::rule_conflict, "该回合不在等待叙事阶段", false, "刷新回合"});
        Statement update(database_, "UPDATE simulation_turn SET final_narration=?,status='completed',revision=revision+1 WHERE id=? AND status='narration_pending'");
        bindText(update.get(), 1, final_narration); bindText(update.get(), 2, turn_id);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("完成叙事并发更新失败");
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "turn"); bindText(log.get(), 4, turn_id); bindText(log.get(), 5, utcNow());
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSimulationTurn(database_, turn_id);
        transaction.commit();
        return Result<xuyan::domain::SimulationTurn>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationTurn>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationSession> WorkspaceRepository::controlSimulationSession(
    const std::string& command_id, const std::string& session_id, int expected_revision, const std::string& action) {
    const auto payload = session_id + '|' + std::to_string(expected_revision) + '|' + action;
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "session")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationSession(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationSession>::success(std::move(result));
        }
        auto session = readSimulationSession(database_, session_id);
        if (session.revision != expected_revision)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::revision_conflict, "推演会话修订已变化", true, "刷新会话后重试"});
        std::string status = session.status;
        bool pause_requested = session.pause_requested;
        bool cancel_requested = session.cancel_requested;
        if (action == "pause") {
            if (status == "completed" || status == "cancelled")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::rule_conflict, "终态会话不能暂停", false, "新建推演"});
            pause_requested = true;
            if (session.reserved_calls == 0) status = "paused";
        } else if (action == "resume") {
            if (status != "paused" && status != "blocked")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::rule_conflict, "只有暂停或阻塞会话可以恢复", false, "刷新会话"});
            if (session.unknown_calls > 0)
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::rule_conflict, "存在结果未知的模型调用，不能自动恢复", false, "人工核对账单后新建会话"});
            status = "ready"; pause_requested = false; cancel_requested = false;
        } else if (action == "cancel") {
            cancel_requested = true; pause_requested = false; status = "cancelled";
        } else {
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::validation_failed, "未知的推演控制动作", false, "使用 pause、resume 或 cancel"});
        }
        const auto now = utcNow();
        Statement update(database_, "UPDATE simulation_session SET status=?,pause_requested=?,cancel_requested=?,revision=revision+1,updated_at=? WHERE id=? AND revision=?");
        bindText(update.get(), 1, status); sqlite3_bind_int(update.get(), 2, pause_requested ? 1 : 0); sqlite3_bind_int(update.get(), 3, cancel_requested ? 1 : 0);
        bindText(update.get(), 4, now); bindText(update.get(), 5, session_id); sqlite3_bind_int(update.get(), 6, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("控制推演会话并发更新失败");
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "session"); bindText(log.get(), 4, session_id); bindText(log.get(), 5, now);
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSimulationSession(database_, session_id);
        transaction.commit();
        return Result<xuyan::domain::SimulationSession>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationSession>::failure(storageError(exception));
    }
}

Result<xuyan::domain::SimulationSession> WorkspaceRepository::commitDirectorIntervention(
    const std::string& command_id, const std::string& session_id, int expected_revision,
    xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state) {
    auto valid = xuyan::domain::validateActorIntent(std::move(intent));
    if (!valid.ok()) return Result<xuyan::domain::SimulationSession>::failure(*valid.error);
    intent = std::move(*valid.value);
    const auto payload = session_id + '|' + intent.actor_id + '|' + intent.operation + '|'
        + intent.target_id + '|' + (intent.holder_consented ? "1" : "0") + '|' + intent.speech;
    try {
        Transaction transaction(database_);
        Statement replay(database_, "SELECT payload_hash,result_type,result_id FROM simulation_command_log WHERE command_id=?");
        bindText(replay.get(), 1, command_id);
        if (sqlite3_step(replay.get()) == SQLITE_ROW) {
            if (columnText(replay.get(), 0) != payload || columnText(replay.get(), 1) != "session")
                return Result<xuyan::domain::SimulationSession>::failure(
                    {ErrorCode::command_conflict, "命令标识已用于其他推演操作", false, "生成新的命令标识"});
            auto result = readSimulationSession(database_, columnText(replay.get(), 2));
            transaction.commit();
            return Result<xuyan::domain::SimulationSession>::success(std::move(result));
        }
        auto session = readSimulationSession(database_, session_id);
        if (session.revision != expected_revision)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::revision_conflict, "推演会话修订已变化", true, "刷新会话后重试"});
        if (session.status == "completed" || session.status == "cancelled" || session.reserved_calls != 0)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::rule_conflict, "终态或存在在途调用时不能导演介入", false, "先暂停并等待当前回合收束"});
        if (std::none_of(session.actors.begin(), session.actors.end(), [&](const auto& actor) { return actor.actor_id == intent.actor_id; }))
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::missing_context, "导演接管的人物不属于该会话", false, "选择已绑定人物"});
        Statement head(database_, "SELECT head_commit_id FROM branch WHERE id=?");
        bindText(head.get(), 1, session.branch_id);
        if (sqlite3_step(head.get()) != SQLITE_ROW || columnText(head.get(), 0) != intent.input_commit_id)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::revision_conflict, "分支头已变化，不能提交过期导演指令", true, "刷新后重新介入"});
        const auto previous = readCommit(database_, intent.input_commit_id);
        if (next_state.turn != previous.state.turn + 1 || next_state.elapsed_ticks < previous.state.elapsed_ticks
            || next_state.revision <= previous.state.revision)
            return Result<xuyan::domain::SimulationSession>::failure(
                {ErrorCode::validation_failed, "导演介入后的状态不单调", false, "重新执行领域规则"});
        CommitView next; next.branch_id = session.branch_id; next.commit_id = randomId("commit");
        next.parent_commit_id = intent.input_commit_id; next.state = std::move(next_state);
        next.state_hash = xuyan::domain::stateHash(next.state); insertSnapshot(database_, next);
        Statement branch(database_, "UPDATE branch SET head_commit_id=? WHERE id=? AND head_commit_id=?");
        bindText(branch.get(), 1, next.commit_id); bindText(branch.get(), 2, session.branch_id); bindText(branch.get(), 3, intent.input_commit_id);
        if (sqlite3_step(branch.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("导演介入更新分支头失败");
        const auto now = utcNow();
        const auto intervention_id = randomId("director");
        Statement audit(database_, "INSERT INTO simulation_director_intervention(id,session_id,input_commit_id,committed_commit_id,actor_id,speech,operation,target_id,created_at) VALUES(?,?,?,?,?,?,?,?,?)");
        bindText(audit.get(), 1, intervention_id); bindText(audit.get(), 2, session_id); bindText(audit.get(), 3, intent.input_commit_id);
        bindText(audit.get(), 4, next.commit_id); bindText(audit.get(), 5, intent.actor_id); bindText(audit.get(), 6, intent.speech);
        bindText(audit.get(), 7, intent.operation); bindText(audit.get(), 8, intent.target_id); bindText(audit.get(), 9, now);
        if (sqlite3_step(audit.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        const auto next_status = session.status == "blocked" ? "paused" : session.status;
        Statement update(database_, "UPDATE simulation_session SET status=?,revision=revision+1,updated_at=? WHERE id=? AND revision=?");
        bindText(update.get(), 1, next_status); bindText(update.get(), 2, now); bindText(update.get(), 3, session_id); sqlite3_bind_int(update.get(), 4, expected_revision);
        if (sqlite3_step(update.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("导演介入更新会话失败");
        Statement log(database_, "INSERT INTO simulation_command_log(command_id,payload_hash,result_type,result_id,created_at) VALUES(?,?,?,?,?)");
        bindText(log.get(), 1, command_id); bindText(log.get(), 2, payload); bindText(log.get(), 3, "session"); bindText(log.get(), 4, session_id); bindText(log.get(), 5, now);
        if (sqlite3_step(log.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(database_));
        auto result = readSimulationSession(database_, session_id);
        transaction.commit();
        return Result<xuyan::domain::SimulationSession>::success(std::move(result));
    } catch (const std::exception& exception) {
        return Result<xuyan::domain::SimulationSession>::failure(storageError(exception));
    }
}

Result<int> WorkspaceRepository::recoverInterruptedSimulationSessions() {
    try {
        Transaction transaction(database_);
        Statement query(database_, "SELECT c.id,c.turn_id,t.session_id FROM simulation_provider_call c JOIN simulation_turn t ON t.id=c.turn_id WHERE c.status='sent'");
        std::vector<std::tuple<std::string, std::string, std::string>> calls;
        while (sqlite3_step(query.get()) == SQLITE_ROW)
            calls.emplace_back(columnText(query.get(), 0), columnText(query.get(), 1), columnText(query.get(), 2));
        const auto now = utcNow();
        for (const auto& [call_id, turn_id, session_id] : calls) {
            Statement call(database_, "UPDATE simulation_provider_call SET status='unknown',failure_kind='interrupted',updated_at=? WHERE id=? AND status='sent'");
            bindText(call.get(), 1, now); bindText(call.get(), 2, call_id);
            if (sqlite3_step(call.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("恢复未知模型调用失败");
            Statement turn(database_, "UPDATE simulation_turn SET status='unknown',error_message='应用中断，模型调用结果未知',revision=revision+1 WHERE id=? AND status='requesting'");
            bindText(turn.get(), 1, turn_id);
            if (sqlite3_step(turn.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("恢复未知推演回合失败");
            Statement session(database_, "UPDATE simulation_session SET status='paused',reserved_calls=CASE WHEN reserved_calls>0 THEN reserved_calls-1 ELSE 0 END,unknown_calls=unknown_calls+1,pause_requested=1,revision=revision+1,updated_at=? WHERE id=?");
            bindText(session.get(), 1, now); bindText(session.get(), 2, session_id);
            if (sqlite3_step(session.get()) != SQLITE_DONE || sqlite3_changes(database_) != 1) throw std::runtime_error("恢复中断推演会话失败");
        }
        transaction.commit();
        return Result<int>::success(static_cast<int>(calls.size()));
    } catch (const std::exception& exception) {
        return Result<int>::failure(storageError(exception));
    }
}

} // namespace xuyan::storage
