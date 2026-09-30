#include "sqlite_support.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace xuyan::storage::detail {
/* 功能：为同一个数据库的独立连接分配共享进程锁，不在登记锁内执行数据库事务。
 * 参数：database为有效连接，仅借用文件名元数据。返回：拥有锁对象的共享指针。
 * 副作用：更新弱引用登记表；活动事务持有强引用，结束后锁允许回收；路径规范化失败可抛异常。
 */
std::shared_ptr<std::mutex> writeMutexFor(sqlite3* database) {
    // 仅保护登记表的短临界区，避免将SQL执行串入全局登记锁。
    static std::mutex registry_mutex;
    // 按规范化文件名登记弱引用；不通过登记表永久延长数据库锁的寿命。
    static std::unordered_map<std::string, std::weak_ptr<std::mutex>> registry;
    const auto* filename = sqlite3_db_filename(database, "main");
    const std::string key = filename == nullptr ? std::string{"<memory>"}
        : std::filesystem::weakly_canonical(filename).string();
    std::lock_guard lock(registry_mutex);
    auto& entry = registry[key];
    auto shared = entry.lock();
    if (!shared) { shared = std::make_shared<std::mutex>(); entry = shared; }
    return shared;
}
Statement::Statement(sqlite3* database, const char* sql) {
    if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        // 构造失败不会调用析构，必须在抛出前释放可能存在的部分语句。
        sqlite3_finalize(statement_);
        statement_ = nullptr;
        throw std::runtime_error(message);
    }
}
Statement::~Statement() { sqlite3_finalize(statement_); }

Transaction::Transaction(sqlite3* database)
    : database_(database), write_mutex_(writeMutexFor(database)), write_lock_(*write_mutex_) {
    char* message = nullptr;
    if (sqlite3_exec(database_, "BEGIN IMMEDIATE", nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string detail = message == nullptr ? "无法开始写事务" : message;
        sqlite3_free(message);
        throw std::runtime_error(detail);
    }
}
Transaction::~Transaction() {
    if (!committed_) sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
}
void Transaction::commit() {
    if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
    committed_ = true;
}

ReadTransaction::ReadTransaction(sqlite3* database) : database_(database) {
    if (sqlite3_exec(database_, "BEGIN DEFERRED", nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
}
ReadTransaction::~ReadTransaction() {
    if (!committed_) sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
}
void ReadTransaction::commit() {
    if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database_));
    committed_ = true;
}

void bindText(sqlite3_stmt* statement, int index, const std::string& value) {
    // SQLite长度是int，先检查再缩窄，防止超大输入产生负长度并退化为零终止读取。
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("无法绑定数据库文本参数");
}
std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    return value == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(value),
        static_cast<std::size_t>(sqlite3_column_bytes(statement, column)));
}
bool stepRow(sqlite3_stmt* statement) {
    const auto status = sqlite3_step(statement);
    if (status == SQLITE_ROW) return true;
    if (status == SQLITE_DONE) return false;
    throw std::runtime_error("工作区查询执行失败");
}
std::string randomId(std::string_view prefix) {
    // 每线程独立拥有的随机状态，避免并发共享发生器；不保存人物或小说信息。
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
xuyan::domain::Error storageError(const std::exception& exception) {
    const std::string detail = exception.what();
    const bool has_localized_text = std::any_of(detail.begin(), detail.end(),
        // 这里只判断是否含非ASCII字节，用于保留既有内部中文原因，不代表任意多语言翻译。
        [](unsigned char value) { return value >= 0x80; });
    return {xuyan::domain::ErrorCode::storage_error,
        has_localized_text ? detail : "工作区数据库操作失败", true, "检查工作区路径和磁盘空间后重试"};
}
} // namespace xuyan::storage::detail
