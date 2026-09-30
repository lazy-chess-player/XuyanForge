#include "xuyan/application/backup_service.h"
#include "backup_filesystem.h"

#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"

#include <chrono>
#include <fstream>
#include <set>
#include <array>
#include <memory>
#include <sqlite3.h>

namespace xuyan::application {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

/* 单个资产的元数据/读取上限，单位字节，固定 128 MiB；创建与恢复共用，只读且与程序同寿命。 */
constexpr std::uintmax_t maximum_asset_file = 128ULL * 1024 * 1024;
/* 每次操作的资产合计上限，单位字节，固定 4 GiB；不含数据库/清单，创建与恢复读取，与程序同寿命。 */
constexpr std::uintmax_t maximum_total = 4ULL * 1024 * 1024 * 1024;
/* 每次复制普通资产的数量上限，单位个，固定 10000；不计目录/数据库/清单，与程序同寿命。 */
constexpr int maximum_files = 10000;

/*
 * 功能：按UTF-8协议字节构造本机文件路径，不经过系统窄字符页转换。
 * 参数：value为调用期间借用的UTF-8资产路径字节，空值由上层路径契约拒绝。
 * 返回：独立路径值。失败：非法编码转换或分配错误传播至服务错误边界。
 * 副作用：纯内存转换。线程与生命周期：同步执行，不保留字节视图。
 */
std::filesystem::path assetPath(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

/*
 * 功能：解析无链接路径的真实绝对身份，允许目标的末尾目录尚不存在。
 * 参数：path为借用的非空源或目标路径，须无父级回退及空字符。
 * 返回：真实绝对路径值，不保留调用方引用。
 * 失败：空路径、链接、路径解析及元数据错误抛异常。
 * 副作用：只读元数据，不建目录。线程与生命周期：同步执行，目录必须保持稳定。
 */
std::filesystem::path resolvedPath(const std::filesystem::path& path) {
    if (path.empty()) throw std::runtime_error("备份路径不能为空");
    const auto absolute = std::filesystem::absolute(path);
    detail::rejectLinkedPath(absolute);
    auto resolved = std::filesystem::weakly_canonical(absolute);
    while (resolved.has_relative_path() && resolved.filename().empty()) resolved = resolved.parent_path();
#ifdef _WIN32
    // 标准库规范化不保证展开8.3别名；先展开已存在前缀，再拼接不存在的后缀做包含比较。
    auto prefix = resolved;
    std::filesystem::path suffix;
    while (!std::filesystem::exists(prefix)) {
        suffix = suffix.empty() ? prefix.filename() : prefix.filename() / suffix;
        prefix = prefix.parent_path();
        if (prefix.empty()) throw std::runtime_error("备份路径没有可解析的父目录");
    }
    const DWORD capacity = GetLongPathNameW(prefix.c_str(), nullptr, 0);
    if (capacity == 0) throw std::runtime_error("无法展开备份路径的实际名称");
    std::wstring buffer(capacity, L'\0');
    const DWORD length = GetLongPathNameW(prefix.c_str(), buffer.data(), capacity);
    if (length == 0 || length >= capacity) throw std::runtime_error("备份路径名称展开失败");
    buffer.resize(length);
    auto result = std::filesystem::path(buffer);
    if (!suffix.empty()) result /= suffix;
    return result.lexically_normal();
#else
    return resolved;
#endif
}

/*
 * 功能：判断两个已解析的绝对路径是否具有祖先或相同身份。
 * 参数：parent为候选祖先路径；child为待查路径，均只借用至返回。
 * 返回：child等于或位于parent内为true，否则false；分量比较避免文本前缀误判。
 * 失败：Windows大小写比较API失败抛异常。副作用：仅比较路径值，无文件操作。
 * 线程与生命周期：同步执行，不存引用；Windows按不区分大小写保守拒绝包含关系。
 */
bool containsPath(const std::filesystem::path& parent, const std::filesystem::path& child) {
    auto left = parent.begin();
    auto right = child.begin();
    for (; left != parent.end(); ++left, ++right) {
        if (right == child.end()) return false;
#ifdef _WIN32
        const auto a = left->native();
        const auto b = right->native();
        const auto compared = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
        if (compared == 0) throw std::runtime_error("无法比较备份路径身份");
        if (compared != CSTR_EQUAL) return false;
#else
        if (*left != *right) return false;
#endif
    }
    return true;
}

/*
 * 功能：拒绝已有目标及互相包含的源/目标；校验后才创建目标父目录。
 * 参数：source为已解析的实际源根目录；destination为借用的用户目标路径，非空。
 * 返回：尚不存在的已解析目标绝对路径。
 * 失败：包含、已存在、链接或父目录创建错误抛异常。
 * 副作用：可创建原先缺失的目标父目录，不清理这些父目录。
 * 线程与生命周期：同步执行，调用方须独占目标及稳定源树；不提供跨进程目录锁。
 */
std::filesystem::path prepareDestination(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const auto target = resolvedPath(destination);
    if (containsPath(source, target) || containsPath(target, source))
        throw std::runtime_error("备份源与目标不能相同或互相包含");
    if (std::filesystem::exists(target)) throw std::runtime_error("备份目标必须是尚不存在的新目录");
    std::filesystem::create_directories(target.parent_path());
    detail::rejectLinkedPath(target);
    if (std::filesystem::canonical(target.parent_path()) != target.parent_path())
        throw std::runtime_error("备份目标父目录身份发生变化");
    return target;
}

/*
 * 职责：持有备份验证或在线复制所用的独占SQLite连接，不做迁移及默认资料初始化。
 * 生命周期：当前同步函数局部对象；语句和备份句柄必须先释放，随后关闭连接。
 */
class BackupConnection final {
public:
    /*
     * 功能：以明确只读/创建模式打开指定SQLite文件。
     * 参数：path为借用文件路径；writable为true仅用于本调用自有暂存文件，false禁止创建源文件。
     * 返回：初始化拥有连接的对象。失败：打开失败释放部分连接并抛中文异常。
     * 副作用：writable时可创建暂存数据库；不迁移、无网络、不保留输入路径。
     * 线程与生命周期：连接只在创建线程同步使用，不跨线程共享。
     */
    BackupConnection(const std::filesystem::path& path, bool writable) {
        const auto utf8 = path.u8string();
        sqlite3* handle = nullptr;
        const int result = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &handle,
            writable ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY, nullptr);
        connection_.reset(handle);
        if (result != SQLITE_OK) throw std::runtime_error("无法打开备份数据库");
        sqlite3_busy_timeout(connection_.get(), 5000);
    }
    /*
     * 功能：提供当前局部备份连接供语句及在线复制使用。
     * 参数：无。返回：借用非空指针，有效至本对象销毁，不转移所有权。
     * 失败：构造成功后无失败。副作用：只读成员。线程与生命周期：仅本调用线程使用。
     */
    sqlite3* get() const noexcept { return connection_.get(); }
private:
    /* 独占SQLite连接，默认空，构造接管，析构关闭；不持有活动语句，不读凭据。 */
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection_{nullptr, &sqlite3_close};
};

/*
 * 功能：准备只读校验语句，由RAII在退出时完成释放。
 * 参数：database为有效借用连接，须活到返回语句释放；sql为固定内部SQL，调用期间有效。
 * 返回：独占SQLite语句，不转移连接所有权。
 * 失败：编译失败释放部分语句并抛中文异常。
 * 副作用：仅准备语句。线程与生命周期：调用线程使用，调用方先销毁语句再关闭连接。
 */
std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> backupStatement(sqlite3* database, const char* sql) {
    sqlite3_stmt* raw = nullptr;
    const auto result = sqlite3_prepare_v2(database, sql, -1, &raw, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, &sqlite3_finalize);
    if (result != SQLITE_OK) throw std::runtime_error("备份数据库结构无法读取");
    return statement;
}

/*
 * 功能：只读核对SQLite物理快速检查及16张核心表，不初始化或迁移被验证文件。
 * 参数：path为调用期间借用的现有普通数据库文件；至少包含100字节SQLite头。
 * 返回：无。失败：文件过短、SQLite读取/快速检查失败或缺少核心表抛中文异常。
 * 副作用：只读数据库；不验证每个字段业务语义，不等同完整索引及外键检查。
 * 线程与生命周期：同步检查，局部连接/语句作用域内关闭，源必须保持稳定。
 */
void validateDatabase(const std::filesystem::path& path) {
    detail::rejectLinkedPath(path);
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) < 100)
        throw std::runtime_error("备份数据库为空或不是有效文件");
    BackupConnection connection(path, false);
    auto check = backupStatement(connection.get(), "PRAGMA quick_check(1)");
    if (sqlite3_step(check.get()) != SQLITE_ROW || sqlite3_column_type(check.get(), 0) != SQLITE_TEXT
        || std::string(reinterpret_cast<const char*>(sqlite3_column_text(check.get(), 0))) != "ok"
        || sqlite3_step(check.get()) != SQLITE_DONE)
        throw std::runtime_error("备份数据库快速完整性检查失败");
    auto tables = backupStatement(connection.get(),
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ("
        "'branch','state_snapshot','world_entity','entity_revision','source_document','source_chapter',"
        "'world_template','provider_connection','evidence_reference','extraction_job','extraction_step',"
        "'extraction_candidate','character_blueprint','character_blueprint_version','world_version','simulation_session')");
    if (sqlite3_step(tables.get()) != SQLITE_ROW || sqlite3_column_int(tables.get(), 0) != 16
        || sqlite3_step(tables.get()) != SQLITE_DONE)
        throw std::runtime_error("备份数据库缺少核心资料表");
}

/*
 * 功能：从只读源连接在线复制SQLite快照到本调用自有暂存文件，不改源结构。
 * 参数：source为借用现有数据库路径；target为借用的新暂存文件路径，仅此调用拥有。
 * 返回：无。失败：连接、备份初始化/复制/结束失败抛中文异常；句柄自动释放。
 * 副作用：创建暂存数据库；不迁移源，不复制源日志，不执行网络或凭据操作。
 * 线程与生命周期：同步执行，备份句柄先结束再关闭两端连接，无取消接口。
 */
void copyDatabaseSnapshot(const std::filesystem::path& source, const std::filesystem::path& target) {
    BackupConnection input(source, false);
    BackupConnection output(target, true);
    std::unique_ptr<sqlite3_backup, decltype(&sqlite3_backup_finish)> backup(
        sqlite3_backup_init(output.get(), "main", input.get(), "main"), &sqlite3_backup_finish);
    if (!backup) throw std::runtime_error("无法初始化数据库在线备份");
    const auto copied = sqlite3_backup_step(backup.get(), -1);
    const auto finished = sqlite3_backup_finish(backup.release());
    if (copied != SQLITE_DONE || finished != SQLITE_OK) throw std::runtime_error("数据库在线备份失败");
}

/*
 * 功能：把备份或恢复失败包装成带操作建议的存储错误，不附加资产正文。
 * 参数：message：输入，按值接收并移入结果的原因文本，无默认值，可为空；不对文本作脱敏。
 * 返回：storage_error 错误值，retryable 为 false，含检查路径、完整性和空间的建议。
 * 失败：字符串分配异常可传播。
 * 副作用：仅构造内存对象；调用方传入的文件/仓储异常消息可能含本地路径，不宜直接上传。
 * 线程与生命周期：调用线程同步执行，不保存输入引用，返回对象独立拥有消息。
 */
Error backupError(std::string message) { return {ErrorCode::storage_error, std::move(message), false, "检查备份路径、完整性和磁盘空间"}; }

/*
 * 功能：读取普通备份文件，元数据预检及实际读取分别执行字节上限校验。
 * 参数：
 *   path：输入，只借用至返回的文件路径，无默认值，须为可取得大小且可读取的文件。
 *   limit：输入，允许读取的最大字节数，默认128 MiB；0仅允许实际读取为空文件。
 * 返回：独立拥有的文件字节，零字节文件返回空串。
 * 失败：路径包含链接、不是普通文件、大小超限或流读错时抛异常，分配异常可传播。
 * 副作用：只读并有界载入内存；文件增长也受limit限制，不保证阻止限额内并发修改。
 * 线程与生命周期：调用线程同步执行，局部流在退出时关闭，不保留路径引用。
 */
std::string readFile(const std::filesystem::path& path, std::uintmax_t limit = maximum_asset_file) {
    detail::rejectLinkedPath(path);
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("备份条目必须是普通文件");
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("备份文件超过大小上限");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取备份文件");
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(size));
    std::array<char, 8192> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::uintmax_t>(input.gcount());
        // 元数据可能在打开前过期；每块检查剩余额度，避免增长文件突破内存上限。
        if (count > limit - bytes.size()) throw std::runtime_error("备份文件读取期间超过大小上限");
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("读取备份文件失败");
    return bytes;
}

/*
 * 功能：创建目标父目录并将字节写入备份暂存文件。
 * 参数：
 *   path：输入，目标文件路径，无默认值，只借用至返回；父目录须可创建。
 *   bytes：输入，原始字节视图，无默认值，允许为空，单位字节；底层内存须有效至写入结束。
 * 返回：无。
 * 失败：链接、已有目标、建目录、打开、写入或关闭失败抛异常，路径分配异常可传播。
 * 副作用：创建父目录并写新文件，不做fsync或内容脱敏；仅用于本调用独占的暂存树。
 * 线程与生命周期：调用线程同步执行，局部流退出时关闭，不保存路径引用或字节视图。
 */
void writeFile(const std::filesystem::path& path, std::string_view bytes) {
    detail::rejectLinkedPath(path);
    if (std::filesystem::exists(path)) throw std::runtime_error("备份暂存文件路径重复或已被占用");
    std::filesystem::create_directories(path.parent_path());
    detail::rejectLinkedPath(path);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建备份文件");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("写入备份文件失败");
}

/*
 * 功能：在正式目标的父目录中生成本次操作的暂存目录名。
 * 参数：destination：输入，正式目录路径，无默认值，只借用至返回；本函数不校验空值或是否存在。
 * 返回：目标父目录下由目标文件名及单调时钟原生计数生成的.partial路径；计数单位依时钟，不是日期。
 * 失败：路径分配异常传播至服务错误边界。
 * 副作用：只计算路径，不创建/独占目录，也不保证跨进程唯一或已解析到安全范围。
 * 线程与生命周期：调用线程同步执行，无持久状态，不保存输入引用。
 */
std::filesystem::path stagingPath(const std::filesystem::path& destination) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto filename = destination.filename();
    filename += ".partial-" + std::to_string(stamp);
    return destination.parent_path() / filename;
}

/*
 * 功能：从已解析清单中读取必需的字符串字段。
 * 参数：
 *   object：输入，只借用至返回的已解析 JSON 值，无默认值；预期为清单对象。
 *   key：输入，待查字段名视图，无默认值，允许空键；底层文本有效至查询结束。
 * 返回：字段字符串的独立副本，字段为空字符串仍可成功，不验证字符串内容。
 * 失败：字段缺失或不是字符串时抛异常，分配异常可传播。
 * 副作用：只读清单内存，不访问文件。
 * 线程与生命周期：调用线程同步执行，不保留对象引用或字段名视图。
 */
std::string requiredString(const JsonValue& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->isString()) throw std::runtime_error("备份清单字段缺失或类型错误");
    return value->string();
}

/*
 * 功能：核对清单资产路径为规范可移植相对路径，拒绝越界、文件流及平台别名分量。
 * 参数：value：输入，文件系统路径字符串，无默认值，只借用至返回；空值、绝对路径、反斜杠及 .、.. 分量无效。
 * 返回：无；通过时不改写输入，不证明文件真实位于源根之内。
 * 失败：绝对/根名、点分量、重复分隔、控制字符、冒号、保留名称或尾随点空格抛异常；真实链接由读文件入口检查。
 * 副作用：纯内存检查，不读取文件或规范化真实路径。
 * 线程与生命周期：调用线程同步执行，不保留输入引用。
 */
void validateRelativeAssetPath(const std::string& value) {
    const auto path = assetPath(value);
    if (path.empty() || path.has_root_path() || value.find('\\') != std::string::npos
        || value.find('\0') != std::string::npos || value.find("//") != std::string::npos
        || value.back() == '/')
        throw std::runtime_error("备份资产路径必须是规范相对路径");
    for (const auto& component : path) {
        const auto encoded = component.generic_u8string();
        const std::string text(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        if (component == ".." || component == "." || text.empty() || text.back() == '.' || text.back() == ' ')
            throw std::runtime_error("备份资产路径包含非法分量");
        std::string stem;
        for (unsigned char character : text) {
            if (character == '.') break;
            stem.push_back(character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A') : static_cast<char>(character));
        }
        // 所有字符都检查，不因设备名前缀计算遇到扩展名而遗漏非法字符。
        for (unsigned char character : text) if (character < 32 || std::string_view(":*?\"<>|").find(static_cast<char>(character)) != std::string_view::npos)
            throw std::runtime_error("备份资产路径包含保留字符");
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
            || (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9')
            || stem == "COM\xc2\xb9" || stem == "COM\xc2\xb2" || stem == "COM\xc2\xb3"
            || stem == "LPT\xc2\xb9" || stem == "LPT\xc2\xb2" || stem == "LPT\xc2\xb3")
            throw std::runtime_error("备份资产路径包含设备保留名称");
    }
    if (value.rfind("assets/", 0) != 0) throw std::runtime_error("备份资产不在 assets 目录");
}

/*
 * 功能：核对清单文件条目的字节长度及SHA-256，防止合法摘要掩盖错误大小。
 * 参数：item为借用JSON对象，必须有非负整数size和字符串sha256；bytes为借用实际有界文件字节。
 * 返回：无。失败：字段缺失/类型错误、大小或摘要不一致抛中文异常。
 * 副作用：只读内存，不输出正文或摘要。线程与生命周期：同步执行，不保存输入引用。
 */
void verifyFile(const JsonValue& item, const std::string& bytes) {
    const auto* size = item.find("size");
    if (size == nullptr || !size->isInteger() || size->integer() < 0
        || static_cast<std::uintmax_t>(size->integer()) != bytes.size())
        throw std::runtime_error("备份清单文件大小缺失、无效或不匹配");
    if (xuyan::domain::sha256(bytes) != requiredString(item, "sha256"))
        throw std::runtime_error("备份文件摘要不匹配");
}

} // namespace

BackupService::BackupService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

Result<BackupReport> BackupService::create(const std::filesystem::path& destination_directory) {
    try {
        // 在创建任何输出之前确认现有源、路径链及包含关系，缺失源绝不自动建库或迁移。
        const auto source_database = resolvedPath(database_path_);
        if (!std::filesystem::is_regular_file(source_database)) throw std::runtime_error("备份源数据库不存在或不是普通文件");
        const auto source_root = source_database.parent_path();
        const auto destination = prepareDestination(source_root, destination_directory);
        const auto assets_root = source_root / "assets";
        detail::rejectLinkedPath(assets_root);
        if (std::filesystem::file_size(source_database) > 2ULL * 1024 * 1024 * 1024)
            throw std::runtime_error("源数据库超过备份大小上限");
        validateDatabase(source_database);
        detail::OwnedBackupStaging owner(stagingPath(destination));
        const auto& staging = owner.path();
        const auto database_target = staging / "workspace.sqlite";
        copyDatabaseSnapshot(source_database, database_target);
        validateDatabase(database_target);

        /* 数据库先完成在线快照，资产随后逐个复制；二者不在同一一致性边界，调用方须保持资产稳定。 */
        JsonValue::Array assets;
        BackupReport report{database_target};
        if (std::filesystem::exists(assets_root)) {
            if (!std::filesystem::is_directory(assets_root)) throw std::runtime_error("资产根必须是普通目录");
            // 不再skip_permission_denied；任何不可读目录或特殊文件都使整个备份失败，不能伪装完整。
            for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_root)) {
                detail::rejectLinkedPath(entry.path());
                if (entry.is_directory()) continue;
                if (!entry.is_regular_file()) throw std::runtime_error("资产目录包含非普通文件");
                if (++report.asset_count > maximum_files) throw std::runtime_error("资产文件数量超过备份上限");
                const auto encoded = entry.path().lexically_relative(source_root).generic_u8string();
                const std::string relative(reinterpret_cast<const char*>(encoded.data()), encoded.size());
                validateRelativeAssetPath(relative);
                const auto bytes = readFile(entry.path());
                if (bytes.size() > maximum_total - report.asset_bytes) throw std::runtime_error("资产大小超过备份上限");
                report.asset_bytes += bytes.size();
                writeFile(staging / assetPath(relative), bytes);
                assets.emplace_back(JsonValue::Object{{"path", relative}, {"sha256", xuyan::domain::sha256(bytes)},
                                                       {"size", static_cast<std::int64_t>(bytes.size())}});
            }
        }
        /* 数据库整文件读入以计算摘要，独立限制为 2 GiB；不计入资产总量，也不逐字段过滤用户或模型内容。 */
        const auto database_bytes = readFile(database_target, 2ULL * 1024 * 1024 * 1024);
        JsonValue manifest(JsonValue::Object{
            {"format", "xuyan-backup"}, {"version", 1},
            {"database", JsonValue::Object{{"path", "workspace.sqlite"}, {"sha256", xuyan::domain::sha256(database_bytes)},
                                            {"size", static_cast<std::int64_t>(database_bytes.size())}}},
            {"assets", std::move(assets)}, {"credentials_included", false}});
        /* 清单最后写入再重命名发布；凭据标记仅表示未读取系统凭据，不代表数据库及资产内容无秘密。 */
        const auto manifest_bytes = xuyan::package::writeJson(manifest);
        if (manifest_bytes.size() > 4ULL * 1024 * 1024) throw std::runtime_error("备份清单超过大小上限");
        writeFile(staging / "manifest.json", manifest_bytes);
        report.workspace_database = destination / "workspace.sqlite";
        auto result = Result<BackupReport>::success(std::move(report));
        owner.publish(destination);
        return result;
    } catch (const std::filesystem::filesystem_error&) {
        return Result<BackupReport>::failure(backupError("备份文件操作失败，请检查路径权限和磁盘空间"));
    } catch (const std::bad_alloc&) {
        return Result<BackupReport>::failure(backupError("备份可用内存不足"));
    } catch (const std::exception& exception) {
        // 只有成功独占创建的守卫能清理；不按异常路径或计算名称接管目录。
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

Result<BackupReport> BackupService::restore(
    const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory) {
    try {
        const auto source = resolvedPath(backup_directory);
        if (!std::filesystem::is_directory(source)) throw std::runtime_error("备份源必须是已有普通目录");
        const auto destination = prepareDestination(source, destination_directory);
        detail::rejectLinkedPath(source / "assets");
        if (std::filesystem::exists(source / "assets") && !std::filesystem::is_directory(source / "assets"))
            throw std::runtime_error("备份资产根必须是普通目录");
        const auto manifest_bytes = readFile(source / "manifest.json", 4ULL * 1024 * 1024);
        auto parsed = xuyan::package::parseJson(manifest_bytes);
        if (!parsed.ok() || !parsed.value->isObject() || requiredString(*parsed.value, "format") != "xuyan-backup")
            throw std::runtime_error("备份清单格式无效");
        const auto* version = parsed.value->find("version");
        const auto* database = parsed.value->find("database");
        const auto* assets = parsed.value->find("assets");
        const auto* credentials = parsed.value->find("credentials_included");
        if (version == nullptr || !version->isInteger() || version->integer() != 1 || database == nullptr
            || !database->isObject() || assets == nullptr || !assets->isArray() || credentials == nullptr
            || !credentials->isBool() || credentials->boolean()) throw std::runtime_error("不支持的备份版本或凭据标记");
        if (assets->array().size() > static_cast<std::size_t>(maximum_files)) throw std::runtime_error("备份资产文件数量超过上限");
        if (requiredString(*database, "path") != "workspace.sqlite") throw std::runtime_error("备份数据库路径无效");
        const auto database_bytes = readFile(source / "workspace.sqlite", 2ULL * 1024 * 1024 * 1024);
        verifyFile(*database, database_bytes);

        detail::OwnedBackupStaging owner(stagingPath(destination));
        const auto& staging = owner.path();
        writeFile(staging / "workspace.sqlite", database_bytes);
        // 对真正将要发布的文件只读验证，不能用仓储构造来“修好”输入，也不信任摘要即可用。
        validateDatabase(staging / "workspace.sqlite");
        BackupReport report{destination / "workspace.sqlite"};
        /* 本清单已处理的规范路径，初始空；防止同文本重复，writeFile再拒绝实际文件系统别名碰撞。 */
        std::set<std::string> seen_paths;
        for (const auto& item : assets->array()) {
            if (!item.isObject()) throw std::runtime_error("备份资产条目无效");
            const auto relative = requiredString(item, "path");
            validateRelativeAssetPath(relative);
            if (!seen_paths.insert(relative).second) throw std::runtime_error("备份清单包含重复资产路径");
            const auto path = assetPath(relative);
            const auto bytes = readFile(source / path);
            verifyFile(item, bytes);
            if (bytes.size() > maximum_total - report.asset_bytes) throw std::runtime_error("备份资产总量超过上限");
            ++report.asset_count;
            report.asset_bytes += bytes.size();
            writeFile(staging / path, bytes);
        }
        auto result = Result<BackupReport>::success(std::move(report));
        owner.publish(destination);
        return result;
    } catch (const std::filesystem::filesystem_error&) {
        return Result<BackupReport>::failure(backupError("恢复文件操作失败，请检查路径权限和磁盘空间"));
    } catch (const std::bad_alloc&) {
        return Result<BackupReport>::failure(backupError("恢复可用内存不足"));
    } catch (const std::exception& exception) {
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

} // namespace xuyan::application
