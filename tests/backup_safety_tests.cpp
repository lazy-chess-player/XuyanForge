#include "xuyan/application/backup_service.h"
#include "xuyan/application/world_catalog_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"

#include <sqlite3.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using xuyan::application::BackupReport;
using xuyan::application::BackupService;
using xuyan::application::WorldCatalogService;
using xuyan::domain::Result;
using xuyan::package::JsonValue;

/* 本进程各独占目录析构的清理失败数，单位次，初始0；守卫累加，主入口读取并判失败。 */
int cleanup_failures{0};

/* 功能：将一个预期条件转为手动断言，保持现有纯核心测试风格。
 * 参数：condition为预期成立的条件；message为调用期间借用的中文失败说明，无默认值。
 * 返回：无。失败：条件为假时抛runtime_error，分配异常可传播。
 * 副作用：不访问文件或网络；仅在调用线程同步执行，不保存说明视图。
 */
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

/* 功能：识别测试路径的链接身份，兼容MinGW未把Windows重解析点报告成symlink的情况。
 * 参数：path为调用期间借用的自有现存路径，无默认值。
 * 返回：符号链接或Windows重解析点为true，普通目录/文件为false。
 * 失败：元数据读取失败抛异常。副作用：只查询自有路径身份，不打开链接目标。
 */
bool isLinkedPath(const fs::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    require(attributes != INVALID_FILE_ATTRIBUTES, "无法核查自有测试路径属性");
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
#endif
    return fs::is_symlink(fs::symlink_status(path));
}

/* 功能：记录链接自身的目标身份，避免MinGW的read_symlink设施缺失及跟随链接读取资料。
 * 参数：path为调用期间借用的自有已确认链接，无默认值。
 * 返回：Windows返回完整重解析数据摘要，其他平台返回链接目标文本；由快照独立持有。
 * 失败：打开/查询链接、返回长度或标准读取失败抛异常。
 * 副作用：Windows以OPEN_REPARSE_POINT打开链接自身并RAII关闭，不跟随目标或修改资料。
 */
std::string linkIdentity(const fs::path& path) {
#ifdef _WIN32
    const HANDLE opened = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    /* INVALID_HANDLE_VALUE不是空指针；先拒绝错误哨兵，再移交RAII，避免异常路径关闭无效句柄。 */
    require(opened != INVALID_HANDLE_VALUE, "无法打开测试链接自身");
    std::unique_ptr<void, decltype(&CloseHandle)> handle(opened, &CloseHandle);
    std::array<char, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer{};
    DWORD size = 0;
    require(DeviceIoControl(handle.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer.data(),
                            static_cast<DWORD>(buffer.size()), &size, nullptr) != 0,
            "无法读取测试链接的重解析身份");
    require(size > 0 && size <= buffer.size(), "测试链接重解析数据长度无效");
    return xuyan::domain::sha256(std::string_view(buffer.data(), size));
#else
    return fs::read_symlink(path).generic_string();
#endif
}

/* 职责：独占系统临时根下新建的一个测试目录；只持有本测试路径及清理责任。
 * 生命周期：栈上同步使用，服务调用和文件流先结束；不复制、不观察或清理用户目录。
 */
class TemporaryDirectory final {
public:
    /* 功能：通过create_directory成功取得独占目录，碰撞只换候选，不接管已有路径。
     * 参数：无。返回：完成独占目录初始化。
     * 失败：系统临时路径、权限或随机设施异常传播；64次碰撞后抛中文异常。
     * 副作用：仅在已解析的系统临时根下创建一个空子目录，无业务资料或网络访问。
     */
    TemporaryDirectory() {
        const auto parent = fs::canonical(fs::temp_directory_path());
        std::mt19937_64 random(std::random_device{}());
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto candidate = parent / ("xuyan-backup-safety-" + std::to_string(random()));
            /* 在取得目录前完成路径分配，避免成功建目录后路径复制异常导致所有权尚未登记。 */
            root_ = candidate;
            std::error_code error;
            if (fs::create_directory(root_, error)) {
                owned_ = true;
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::runtime_error("无法创建独占备份测试目录");
        }
        throw std::runtime_error("独占备份测试目录候选连续碰撞");
    }

    /* 功能：仅删除构造时成功创建的精确子目录，拒绝清理被替换为符号链接的根。
     * 参数：无。返回：无。失败：不抛异常；状态或删除错误计入清理失败并中文报告。
     * 副作用：删除本测试数据及树内链接自身；不跟随链接删除其目标，不枚举其他临时目录。
     */
    ~TemporaryDirectory() noexcept {
        if (!owned_) return;
        try {
            require(fs::is_directory(root_) && !isLinkedPath(root_), "自有测试根被替换，拒绝清理");
            /* 先移除链接自身，再递归清理普通树；MinGW递归删除不能作为重解析点安全边界。 */
            std::vector<fs::path> links;
            for (fs::recursive_directory_iterator iterator(root_), end; iterator != end; ++iterator) {
                if (isLinkedPath(iterator->path())) {
                    iterator.disable_recursion_pending();
                    links.push_back(iterator->path());
                }
            }
            for (const auto& link : links) {
#ifdef _WIN32
                const auto attributes = GetFileAttributesW(link.c_str());
                require(attributes != INVALID_FILE_ATTRIBUTES, "清理前无法核查测试链接");
                require(((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryW(link.c_str())
                                                                      : DeleteFileW(link.c_str())) != 0,
                        "无法移除自有测试链接");
#else
                require(fs::remove(link), "无法移除自有测试链接");
#endif
            }
            fs::remove_all(root_);
        } catch (...) {
            ++cleanup_failures;
            std::cerr << "独占备份测试目录清理失败，未计为通过\n";
        }
    }

    /* 功能：禁止复制清理责任。参数：other为另一守卫的借用引用。
     * 返回：无，构造不可用。失败：编译期拒绝。副作用：无。
     */
    TemporaryDirectory(const TemporaryDirectory& other) = delete;

    /* 功能：禁止覆盖清理责任。参数：other为另一守卫的借用引用。
     * 返回：赋值不可用。失败：编译期拒绝。副作用：无。
     */
    TemporaryDirectory& operator=(const TemporaryDirectory& other) = delete;

    /* 功能：提供当前测试唯一拥有的根路径。
     * 参数：无。返回：借用的只读绝对路径，有效期至守卫销毁。
     * 失败：无。副作用：只读成员，无文件操作；仅供同步测试使用。
     */
    const fs::path& root() const { return root_; }

private:
    /* 成功创建的绝对目录路径，无单位，默认空；构造赋值，测试借用，析构精确清理。 */
    fs::path root_;
    /* 是否取得目录所有权，默认false；只有create_directory返回true才置真，析构读取。 */
    bool owned_{false};
};

/* 职责：临时改变本测试进程的工作目录，使旧实现处理空目标时也只写独占测试树。
 * 生命周期：串行测试栈上使用；结束先恢复工作目录，再由外层目录守卫清理，无线程共享。
 */
class ScopedCurrentDirectory final {
public:
    /* 功能：保存当前目录并切入自有测试目录。
     * 参数：directory为调用期间借用、已存在的独占目录，无默认值。
     * 返回：完成保存及切换。失败：查询或切换异常传播，不启动服务。
     * 副作用：改变本进程路径解析基准；不读旧目录中的文件，不创建资料。
     */
    explicit ScopedCurrentDirectory(const fs::path& directory) : previous_(fs::current_path()) {
        fs::current_path(directory);
    }

    /* 功能：在测试目录清理前恢复原工作目录。
     * 参数：无。返回：无。失败：使用error_code；恢复失败计入清理失败并中文输出。
     * 副作用：改变进程工作目录，不删除文件；本可执行文件仅串行调用。
     */
    ~ScopedCurrentDirectory() noexcept {
        std::error_code error;
        fs::current_path(previous_, error);
        if (error) {
            ++cleanup_failures;
            std::cerr << "备份测试工作目录恢复失败\n";
        }
    }

    /* 功能：禁止复制恢复责任。参数：other为另一守卫的借用引用。
     * 返回：无，构造不可用。失败：编译期拒绝。副作用：无。
     */
    ScopedCurrentDirectory(const ScopedCurrentDirectory& other) = delete;

    /* 功能：禁止覆盖恢复责任。参数：other为另一守卫的借用引用。
     * 返回：赋值不可用。失败：编译期拒绝。副作用：无。
     */
    ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory& other) = delete;

private:
    /* 切换前的绝对目录，无单位，无默认值；构造保存、析构读取，仅用于恢复，不扫描内容。 */
    fs::path previous_;
};

/* 功能：在测试自有路径写入二进制探针，允许零字节与嵌入零字节，不产生小说或人物。
 * 参数：path为调用期间借用的自有文件路径；bytes为有效至返回的字节视图，允许为空。
 * 返回：无。失败：建父目录、打开、写入或关闭失败抛异常。
 * 副作用：只创建测试目录及截断指定测试文件；调用方保证路径位于独占树内。
 */
void writeBytes(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "无法打开自有测试探针文件");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(output.good(), "写入或关闭自有测试探针文件失败");
}

/* 功能：读取测试自有文件的完整字节，用于摘要及保留性检查。
 * 参数：path为调用期间借用的自有普通文件路径，无默认值。
 * 返回：独立拥有的字节串，零长度文件返回空串。
 * 失败：打开或读取失败抛异常。副作用：只读测试文件，局部流返回前关闭。
 */
std::string readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "无法读取自有测试文件");
    std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "读取自有测试文件发生错误");
    return bytes;
}

/* 测试树快照：键为不解析链接的相对路径，值为目录类别、链接文本或普通文件SHA；仅内存持有。 */
using TreeSnapshot = std::map<std::string, std::string>;

/* 功能：记录独占树的目录、文件摘要和链接身份，用于发现正式目录、暂存及源损坏。
 * 参数：root为调用期间借用的自有目录，无默认值；不得传用户资料路径。
 * 返回：独立拥有的路径到状态映射，空目录返回空映射。
 * 失败：遍历、读取或路径处理异常传播。副作用：只读，不跟随目录符号链接遍历或读其目标。
 */
TreeSnapshot snapshotTree(const fs::path& root) {
    TreeSnapshot result;
    for (fs::recursive_directory_iterator iterator(root), end; iterator != end; ++iterator) {
        const auto& entry = *iterator;
        const auto relative = entry.path().lexically_relative(root).generic_string();
        const auto status = entry.symlink_status();
        if (isLinkedPath(entry.path())) {
            iterator.disable_recursion_pending();
            result.emplace(relative, "链接:" + linkIdentity(entry.path()));
        }
        else if (fs::is_directory(status))
            result.emplace(relative, "目录");
        else if (fs::is_regular_file(status))
            result.emplace(relative, "文件:" + xuyan::domain::sha256(readBytes(entry.path())));
        else
            throw std::runtime_error("测试树中出现非预期文件类型");
    }
    return result;
}

/* 功能：说明快照发生变化的具体自有路径，不输出文件内容或摘要。
 * 参数：before为操作前快照，after为操作后快照，仅借用至返回，无默认值。
 * 返回：中文路径变化说明；完全相同时为空串。
 * 失败：字符串分配异常传播。副作用：纯内存比较，不读用户资料，不掩盖新增目录或字节变化。
 */
std::string describeChanges(const TreeSnapshot& before, const TreeSnapshot& after) {
    std::string result;
    for (const auto& [path, state] : before) {
        const auto found = after.find(path);
        if (found == after.end()) result += "移除:" + path + "；";
        else if (found->second != state) result += "改变:" + path + "；";
    }
    for (const auto& [path, state] : after) {
        (void)state;
        if (!before.contains(path)) result += "新增:" + path + "；";
    }
    return result;
}

/* 功能：验证失败结果、正式目标不存在和整棵自有树保留，同时报告多个偏离。
 * 参数：result为本调用的备份结果；destination为预期未创建的正式目标，空表示只核对快照；
 *   root为自有根目录；before为调用前快照，均只借用至返回，无默认值。
 * 返回：无。失败：任一不变量偏离抛中文断言异常；读树异常传播。
 * 副作用：只读结果及自有树；不清理服务残留，保留到外层守卫析构，避免测试掩盖缺陷。
 */
void requireRejected(const Result<BackupReport>& result, const fs::path& destination,
                     const fs::path& root, const TreeSnapshot& before) {
    std::string problems;
    if (result.ok()) problems += "非法操作被成功接受；";
    else if (!result.error) problems += "失败结果缺少错误原因；";
    if (!destination.empty() && fs::exists(destination)) problems += "留下正式目标目录；";
    const auto changes = describeChanges(before, snapshotTree(root));
    if (!changes.empty()) problems += "源内容改变或留下本调用暂存/目录；" + changes;
    require(problems.empty(), problems);
}

/* 测试SQLite连接的独占RAII类型；句柄默认须显式提供，由sqlite3_close在作用域结束释放。 */
using TestDatabase = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

/* 功能：打开明确的测试自有SQLite连接，不隐式调用迁移或生成业务资料。
 * 参数：path为调用期间借用的独占树文件路径；flags为SQLite打开标志，无默认值，由用例显式选择。
 * 返回：拥有连接的RAII值，调用方不得让借用语句活得比连接更久。
 * 失败：打开失败抛异常，包括部分连接在异常前释放。副作用：仅CREATE标志允许新建自有文件。
 */
TestDatabase openTestDatabase(const fs::path& path, int flags) {
    sqlite3* opened = nullptr;
    const auto encoded = path.u8string();
    const int status = sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()), &opened, flags, nullptr);
    TestDatabase connection(opened, &sqlite3_close);
    require(status == SQLITE_OK && connection != nullptr, "无法打开测试自有SQLite连接");
    return connection;
}

/* 功能：执行测试自有连接上的固定SQL，用于日志模式及探针数据，不包含小说或人物。
 * 参数：database为调用期间有效的借用连接；sql为有效的零结尾固定SQL，无默认值，不接收用户输入。
 * 返回：无。失败：执行错误抛中文断言；不输出SQLite可能携带的数据详情。
 * 副作用：按SQL改变本测试数据库模式/结构/探针数据，全部在调用线程同步执行。
 */
void executeTestSql(sqlite3* database, const char* sql) {
    require(sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "测试固定SQL执行失败");
}

/* 功能：读取测试固定查询的一行整数，确保恢复数据库内确有WAL中的提交数据。
 * 参数：database为调用期间有效的借用连接；sql为有效的零结尾固定查询，结果须恰好一行整数。
 * 返回：查询整数值，无单位，由用例赋予计数或探针含义。
 * 失败：准备、结果类型/行数或步进错误抛异常。副作用：只查询自有库，局部语句RAII释放。
 */
std::int64_t queryTestInteger(sqlite3* database, const char* sql) {
    sqlite3_stmt* prepared = nullptr;
    const int status = sqlite3_prepare_v2(database, sql, -1, &prepared, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(prepared, &sqlite3_finalize);
    require(status == SQLITE_OK && statement != nullptr, "测试整数查询准备失败");
    require(sqlite3_step(statement.get()) == SQLITE_ROW && sqlite3_column_type(statement.get(), 0) == SQLITE_INTEGER,
            "测试整数查询未返回一行整数");
    const auto value = sqlite3_column_int64(statement.get(), 0);
    require(sqlite3_step(statement.get()) == SQLITE_DONE, "测试整数查询未正常结束");
    return value;
}

/* 职责：拥有单个回归的空工作区及输出位置；数据只存在独占临时目录，不构造业务实体。
 * 生命周期：每次测试单独构造，所有服务同步返回后才销毁目录；成员路径为值，无连接共享。
 */
class BackupFixture final {
public:
    /* 功能：显式初始化空数据库，并按参数准备无内容或二进制资产。
     * 参数：with_asset默认false，true生成5字节测试探针；with_asset_root默认true，false不建assets。
     * 返回：完成自有夹具初始化。失败：建目录、初始化、空目录校验或写探针失败抛异常。
     * 副作用：只创建临时数据库结构和测试文件；显式切换DELETE日志模式以核对静态源完整树保留。
     *   活动WAL用例另行切换并持有连接；不创建世界、人物、小说，不联网。
     */
    explicit BackupFixture(bool with_asset = false, bool with_asset_root = true)
        : source(temporary.root() / "workspace"), database(source / "workspace.sqlite"),
          backup(temporary.root() / "backup"), destination(temporary.root() / "restored") {
        fs::create_directory(source);
        WorldCatalogService catalog(database);
        const auto initialized = catalog.initialize();
        require(initialized.ok() && *initialized.value, "空工作区初始化失败");
        const auto worlds = catalog.list();
        require(worlds.ok() && worlds.value->empty(), "空工作区初始化不能预置世界");
        /* 静态源完整快照不把SQLite只读WAL的协调文件创建当数据修改；此夹具选择DELETE模式。 */
        auto connection = openTestDatabase(database, SQLITE_OPEN_READWRITE);
        executeTestSql(connection.get(), "PRAGMA journal_mode=DELETE");
        require(queryTestInteger(connection.get(), "SELECT count(*) FROM pragma_journal_mode WHERE journal_mode='delete'") == 1,
                "静态测试夹具未切换DELETE日志模式");
        if (with_asset_root) fs::create_directory(source / "assets");
        if (with_asset) writeBytes(source / "assets" / "nested" / "payload.bin", std::string{"\0\1\2\3\4", 5});
    }

    /* 功能：通过公共API生成后续故障注入所用的合法备份。
     * 参数：无。返回：无。失败：创建失败或清单/数据库缺失时抛断言异常。
     * 副作用：仅写本夹具backup路径，服务文件句柄返回前释放。
     */
    void prepareBackup() const {
        BackupService service(database);
        const auto created = service.create(backup);
        require(created.ok() && created.value->workspace_database == backup / "workspace.sqlite",
                "合法故障夹具备份创建失败");
        require(fs::is_regular_file(backup / "manifest.json"), "合法备份缺少清单");
    }

    /* 功能：解析当前测试备份清单供精确修改单个字段。
     * 参数：无。返回：独立拥有的JSON对象树。失败：读取、解析或根类型错误抛异常。
     * 副作用：只读测试清单，不保留流、引用或修改其他文件。
     */
    JsonValue manifest() const {
        auto parsed = xuyan::package::parseJson(readBytes(backup / "manifest.json"));
        require(parsed.ok() && parsed.value->isObject(), "测试备份清单无法解析");
        return std::move(*parsed.value);
    }

    /* 独占临时目录守卫，构造先于全部路径，最后析构；仅本夹具拥有并清理测试数据。 */
    TemporaryDirectory temporary;
    /* 源工作区绝对目录，无单位，无默认值；构造取得，测试读取/显式造故障，随夹具失效。 */
    fs::path source;
    /* 空工作区数据库绝对路径，无单位，无默认值；构造initialize，服务同步读取。 */
    fs::path database;
    /* 备份输出绝对路径，无单位，无默认值；prepareBackup创建，测试只修改自有备份。 */
    fs::path backup;
    /* 恢复正式目标绝对路径，无单位，无默认值；初始不存在，测试及恢复服务使用。 */
    fs::path destination;
};

/* 功能：覆盖无assets目录和显式空assets目录的合法往返，恢复后仍为空世界。
 * 参数：with_asset_root决定是否显式创建空资产根，无默认值。
 * 返回：无。失败：结果、路径、零统计、源保留或重开空目录错误抛断言。
 * 副作用：只在独占目录备份/恢复；无业务数据或网络访问，结束清理。
 */
void testEmptyRoundTrip(bool with_asset_root) {
    BackupFixture fixture(false, with_asset_root);
    const auto source_before = snapshotTree(fixture.source);
    BackupService service(fixture.database);
    const auto created = service.create(fixture.backup);
    require(created.ok() && created.value->asset_count == 0 && created.value->asset_bytes == 0,
            "合法空资产备份应成功且统计为零");
    require(created.value->workspace_database == fixture.backup / "workspace.sqlite", "备份报告路径错误");
    const auto backup_before = snapshotTree(fixture.backup);
    const auto restored = BackupService::restore(fixture.backup, fixture.destination);
    require(restored.ok() && restored.value->asset_count == 0 && restored.value->asset_bytes == 0,
            "合法空资产恢复应成功且统计为零");
    require(restored.value->workspace_database == fixture.destination / "workspace.sqlite", "恢复报告路径错误");
    WorldCatalogService reopened(restored.value->workspace_database);
    require(reopened.initialize().ok(), "恢复的空数据库无法重新初始化");
    const auto worlds = reopened.list();
    require(worlds.ok() && worlds.value->empty(), "恢复不能注入默认世界");
    const auto changes = describeChanges(source_before, snapshotTree(fixture.source))
                         + describeChanges(backup_before, snapshotTree(fixture.backup));
    require(changes.empty(), "合法往返改变源工作区或备份；" + changes);
    require(std::distance(fs::directory_iterator(fixture.temporary.root()), fs::directory_iterator{}) == 3,
            "合法往返留下同级暂存目录");
}

/* 功能：验证嵌入零字节的5字节资产及可选零长度资产能合法往返，源与备份均保留。
 * 参数：with_empty_asset为true时额外创建零长度资产，无默认值。
 * 返回：无。失败：备份/恢复、统计、字节内容、目录保留或暂存清理不符合时抛异常。
 * 副作用：只在独占目录生成二进制探针及空文件；不生成小说、人物或网络请求。
 */
void testBinaryRoundTrip(bool with_empty_asset) {
    BackupFixture fixture(true);
    if (with_empty_asset) writeBytes(fixture.source / "assets" / "empty.bin", {});
    const auto source_before = snapshotTree(fixture.source);
    BackupService service(fixture.database);
    const auto created = service.create(fixture.backup);
    const int expected_count = with_empty_asset ? 2 : 1;
    require(created.ok() && created.value->asset_count == expected_count && created.value->asset_bytes == 5,
            "二进制或零长度资产备份统计错误");
    const auto backup_before = snapshotTree(fixture.backup);
    const auto restored = BackupService::restore(fixture.backup, fixture.destination);
    require(restored.ok() && restored.value->asset_count == expected_count && restored.value->asset_bytes == 5,
            "二进制或零长度资产恢复统计错误");
    require(readBytes(fixture.destination / "assets" / "nested" / "payload.bin") == std::string{"\0\1\2\3\4", 5},
            "5字节资产内容被截断或改变");
    if (with_empty_asset)
        require(fs::is_regular_file(fixture.destination / "assets" / "empty.bin")
                    && fs::file_size(fixture.destination / "assets" / "empty.bin") == 0,
                "零长度资产未被完整保留");
    const auto changes = describeChanges(source_before, snapshotTree(fixture.source))
                         + describeChanges(backup_before, snapshotTree(fixture.backup));
    require(changes.empty(), "二进制往返改变源工作区或备份；" + changes);
    require(std::distance(fs::directory_iterator(fixture.temporary.root()), fs::directory_iterator{}) == 3,
            "二进制往返留下同级暂存目录");
}

/* 功能：验证有活动WAL连接时在线备份能读到已提交但尚未checkpoint的数据。
 * 参数：无。返回：无。失败：WAL未实际形成、主数据库/资产改变、丢失已提交探针或残留暂存时抛异常。
 * 副作用：仅切换自有数据库到WAL并创建探针表；保持写连接活到备份/恢复结束。
 *   允许SQLite自身创建/更新源-shm协调元数据，不删除源aux；连接正常关闭后再清理独占目录。
 */
void testActiveWalBackup() {
    BackupFixture fixture(true);
    auto active = openTestDatabase(fixture.database, SQLITE_OPEN_READWRITE);
    executeTestSql(active.get(), "PRAGMA journal_mode=WAL");
    executeTestSql(active.get(), "PRAGMA wal_autocheckpoint=0");
    require(queryTestInteger(active.get(), "SELECT count(*) FROM pragma_journal_mode WHERE journal_mode='wal'") == 1,
            "活动WAL夹具未实际进入WAL模式");
    const auto database_before = readBytes(fixture.database);
    executeTestSql(active.get(), "CREATE TABLE backup_probe(value INTEGER)");
    executeTestSql(active.get(), "INSERT INTO backup_probe VALUES(27)");
    const auto wal = fixture.source / "workspace.sqlite-wal";
    require(fs::is_regular_file(wal) && fs::file_size(wal) > 32, "已提交探针未形成实际WAL帧");
    require(readBytes(fixture.database) == database_before, "WAL探针提交已改变主库，不能证明未checkpoint场景");
    const auto assets_before = snapshotTree(fixture.source / "assets");
    BackupService service(fixture.database);
    const auto created = service.create(fixture.backup);
    require(created.ok() && created.value->asset_count == 1 && created.value->asset_bytes == 5,
            "活动WAL在线备份失败或资产统计错误");
    const auto restored = BackupService::restore(fixture.backup, fixture.destination);
    require(restored.ok(), "活动WAL备份恢复失败");
    auto restored_database = openTestDatabase(restored.value->workspace_database, SQLITE_OPEN_READONLY);
    require(queryTestInteger(restored_database.get(), "SELECT count(*) FROM backup_probe") == 1
                && queryTestInteger(restored_database.get(), "SELECT value FROM backup_probe") == 27,
            "恢复未包含尚在WAL中已提交的探针数据");
    require(readBytes(fixture.database) == database_before, "在线备份改变了源主数据库字节");
    require(snapshotTree(fixture.source / "assets") == assets_before, "在线备份改变了源资产内容");
    require(readBytes(fixture.destination / "assets" / "nested" / "payload.bin") == std::string{"\0\1\2\3\4", 5},
            "活动WAL往返未保留资产字节");
    require(fs::is_regular_file(wal) && queryTestInteger(active.get(), "SELECT value FROM backup_probe") == 27,
            "备份删除了源WAL或破坏活动连接");
    require(std::distance(fs::directory_iterator(fixture.temporary.root()), fs::directory_iterator{}) == 3,
            "活动WAL在线备份留下同级暂存目录");
}

/* 功能：验证create拒绝源工作区内部目标和assets内部目标，且源数据库/探针字节保留。
 * 参数：inside_assets为true时目标位于assets未建的深层目录，否则直接位于工作区内。
 * 返回：无。失败：非法成功、正式目标或暂存/父目录残留、源改动时抛异常。
 * 副作用：仅在独占测试树调用create；故意给旧实现嵌套目标，最终由守卫清理。
 */
void testCreateInsideSource(bool inside_assets) {
    BackupFixture fixture(true);
    const auto target = inside_assets ? fixture.source / "assets" / "new-parent" / "backup"
                                      : fixture.source / "backup";
    const auto before = snapshotTree(fixture.temporary.root());
    BackupService service(fixture.database);
    const auto result = service.create(target);
    requireRejected(result, target, fixture.temporary.root(), before);
}

/* 功能：验证restore拒绝备份树内部目标，并保留清单、数据库及原资产。
 * 参数：无。返回：无。失败：非法成功或正式/暂存/目录残留时抛异常。
 * 副作用：仅在独占树生成合法备份后调用restore，结束清理，不读取用户资料。
 */
void testRestoreInsideBackup() {
    BackupFixture fixture(true);
    fixture.prepareBackup();
    const auto target = fixture.backup / "nested-target";
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, target);
    requireRejected(result, target, fixture.temporary.root(), before);
}

/* 功能：验证空目标失败，将进程工作目录移到自有树以隔离旧实现的相对暂存路径。
 * 参数：restore为true调用restore，否则调用create，无默认值。
 * 返回：无。失败：空目标成功或任何目录/字节变化时抛异常。
 * 副作用：串行临时切换当前目录，服务只能写测试树；退出恢复目录并清理自有资料。
 */
void testEmptyDestination(bool restore) {
    BackupFixture fixture;
    if (restore) fixture.prepareBackup();
    ScopedCurrentDirectory current(fixture.temporary.root());
    const auto before = snapshotTree(fixture.temporary.root());
    BackupService service(fixture.database);
    const auto result = restore ? BackupService::restore(fixture.backup, fs::path{}) : service.create(fs::path{});
    requireRejected(result, {}, fixture.temporary.root(), before);
}

/* 功能：验证缺失数据库的备份请求失败，不能自动创建数据库或接受一份凭空空库。
 * 参数：missing_parent为true时源父目录也不存在，否则存在空父目录，无默认值。
 * 返回：无。失败：数据库被补建、备份成功或残留时抛异常。
 * 副作用：只在独占树向缺失路径调用create，不调用initialize造库。
 */
void testMissingSource(bool missing_parent) {
    TemporaryDirectory temporary;
    const auto source = temporary.root() / "missing-source";
    if (!missing_parent) fs::create_directory(source);
    const auto database = source / "workspace.sqlite";
    const auto destination = temporary.root() / "backup";
    const auto before = snapshotTree(temporary.root());
    BackupService service(database);
    const auto result = service.create(destination);
    requireRejected(result, destination, temporary.root(), before);
}

/* 功能：验证已有目标及同级partial候选不会被接管、覆盖或清理。
 * 参数：restore决定调用恢复或备份；existing_target为true时目标已有哨兵，否则保留旁边候选后合法发布。
 * 返回：无。失败：拒绝/成功行为不符、哨兵或源改变、新暂存残留时抛异常。
 * 副作用：仅造自有哨兵；不模拟或调用内部暂存碰撞helper，不使用时钟猜测候选名。
 */
void testExistingPaths(bool restore, bool existing_target) {
    BackupFixture fixture;
    if (restore) fixture.prepareBackup();
    const auto target = restore ? fixture.destination : fixture.backup;
    const auto sibling = target.parent_path() / (target.filename().string() + ".partial-preserved");
    writeBytes(sibling / "marker.bin", "\1\2\3");
    if (existing_target) writeBytes(target / "marker.bin", "\4\5\6");
    const auto before = snapshotTree(fixture.temporary.root());
    const auto sibling_before = snapshotTree(sibling);
    const auto source_before = snapshotTree(fixture.source);
    BackupService service(fixture.database);
    const auto result = restore ? BackupService::restore(fixture.backup, target) : service.create(target);
    if (existing_target) {
        require(!result.ok() && result.error.has_value(), "已有目标必须明确拒绝");
        require(snapshotTree(fixture.temporary.root()) == before, "拒绝已有目标时清理或覆盖了已有资料");
    } else {
        require(result.ok(), "旁边已有partial候选不应阻止合法操作");
        require(snapshotTree(sibling) == sibling_before, "本调用清理或覆盖了旁边的partial候选");
        const auto changes = describeChanges(source_before, snapshotTree(fixture.source));
        require(changes.empty(), "合法操作改变了源工作区；" + changes);
        require(std::distance(fs::directory_iterator(fixture.temporary.root()), fs::directory_iterator{})
                    == (restore ? 4 : 3), "合法操作留下新的同级暂存");
    }
}

/* 职责：清单size故障类别，仅测试协议值；无资源和线程状态，各值互斥。 */
enum class SizeFault {
    /* 删除size字段，验证不能把缺失当默认零。 */
    missing,
    /* 将size写成数字字符串，验证禁止隐式转换。 */
    text,
    /* 将size写成非整数实数，验证禁止截断。 */
    real,
    /* 将size写成null，验证不能当作未知并继续恢复。 */
    null_value,
    /* 将size写成布尔值，验证不能当成0或1。 */
    boolean,
    /* 将size写成负整数，验证字节数不能为负。 */
    negative,
    /* 将size写成实际字节数加一，验证不是只检查类型或摘要。 */
    mismatch
};

/* 功能：逐项修改数据库或资产的size，保留原SHA与文件，证明大小契约独立校验。
 * 参数：asset为true修改唯一资产，否则修改数据库；fault为size故障类别，无默认值。
 * 返回：无。失败：故障条目被接受或失败留下正式/暂存目录、改变备份时抛异常。
 * 副作用：仅改自有JSON清单并调用restore；不改生产文件、不发送请求。
 */
void testManifestSize(bool asset, SizeFault fault) {
    BackupFixture fixture(true);
    fixture.prepareBackup();
    auto manifest = fixture.manifest();
    auto& item = asset ? manifest.object().at("assets").array().at(0) : manifest.object().at("database");
    auto& object = item.object();
    const auto actual = object.at("size").integer();
    switch (fault) {
    case SizeFault::missing: object.erase("size"); break;
    case SizeFault::text: object.at("size") = JsonValue(std::to_string(actual)); break;
    case SizeFault::real: object.at("size") = JsonValue(static_cast<double>(actual) + 0.5); break;
    case SizeFault::null_value: object.at("size") = JsonValue(nullptr); break;
    case SizeFault::boolean: object.at("size") = JsonValue(true); break;
    case SizeFault::negative: object.at("size") = JsonValue(-1); break;
    case SizeFault::mismatch: object.at("size") = JsonValue(actual + 1); break;
    }
    writeBytes(fixture.backup / "manifest.json", xuyan::package::writeJson(manifest));
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：写入非SQLite字节并更新正确的SHA及size，验证不能仅凭清单摘要发布数据库。
 * 参数：无。返回：无。失败：非数据库被成功恢复或失败残留时抛异常。
 * 副作用：只截断测试备份数据库及改清单，不触碰源库，结束清理。
 */
void testNonSqliteDatabase() {
    BackupFixture fixture;
    fixture.prepareBackup();
    const std::string bytes{"\0\1\2\3\4\5\6\7", 8};
    writeBytes(fixture.backup / "workspace.sqlite", bytes);
    auto manifest = fixture.manifest();
    auto& database = manifest.object().at("database").object();
    database.at("sha256") = JsonValue(xuyan::domain::sha256(bytes));
    database.at("size") = JsonValue(static_cast<std::int64_t>(bytes.size()));
    writeBytes(fixture.backup / "manifest.json", xuyan::package::writeJson(manifest));
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：验证零字节数据库及可正常打开但没有核心表的SQLite不会作为工作区发布。
 * 参数：empty为true时使用零字节文件，否则由SQLite显式创建仅含探针表的有效库，无默认值。
 * 返回：无。失败：夹具SQLite创建失败、错误数据库被接受或失败残留时抛异常。
 * 副作用：只改自有备份和清单，SHA及size保持真实；不调用迁移或自动补核心表。
 */
void testEmptyOrSchemaLessDatabase(bool empty) {
    BackupFixture fixture;
    fixture.prepareBackup();
    std::string bytes;
    if (!empty) {
        /* SQLite句柄先经unique_ptr接管，包括打开失败时产生的部分连接，作用域退出关闭。 */
        sqlite3* opened = nullptr;
        const auto path = fixture.temporary.root() / "minimal.sqlite";
        const auto encoded = path.u8string();
        const int status = sqlite3_open_v2(reinterpret_cast<const char*>(encoded.c_str()), &opened,
                                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(opened, &sqlite3_close);
        require(status == SQLITE_OK && connection != nullptr, "无核心表SQLite夹具无法创建");
        require(sqlite3_exec(connection.get(), "CREATE TABLE backup_probe(value INTEGER)", nullptr, nullptr, nullptr)
                    == SQLITE_OK, "无核心表SQLite探针表创建失败");
        require(sqlite3_close(connection.get()) == SQLITE_OK, "无核心表SQLite夹具关闭失败");
        connection.release();
        bytes = readBytes(path);
        require(bytes.size() >= 16 && bytes.substr(0, 16) == std::string{"SQLite format 3\0", 16},
                "无核心表夹具必须是实际SQLite文件");
    }
    writeBytes(fixture.backup / "workspace.sqlite", bytes);
    auto manifest = fixture.manifest();
    auto& database = manifest.object().at("database").object();
    database.at("sha256") = JsonValue(xuyan::domain::sha256(bytes));
    database.at("size") = JsonValue(static_cast<std::int64_t>(bytes.size()));
    writeBytes(fixture.backup / "manifest.json", xuyan::package::writeJson(manifest));
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：验证大小超过4MiB的合法JSON清单在读取边界失败，不能只依赖解析节点限额。
 * 参数：无。返回：无。失败：大清单被接受、源改变或正式/暂存残留时抛异常。
 * 副作用：仅将自有合法清单后补JSON空白至4MiB加1字节，约占4MiB内存，无网络。
 */
void testOversizedManifest() {
    BackupFixture fixture;
    fixture.prepareBackup();
    auto bytes = readBytes(fixture.backup / "manifest.json");
    bytes.resize(4U * 1024U * 1024U + 1U, ' ');
    writeBytes(fixture.backup / "manifest.json", bytes);
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：验证完全相同的资产条目重复出现时拒绝整个恢复，第一次复制不得遗留暂存。
 * 参数：无。返回：无。失败：重复路径被接受或失败改变备份/留下目录时抛异常。
 * 副作用：只复制自有清单内的值对象，不修改原资产；恢复数据仅在独占目录。
 */
void testDuplicateAssetPath() {
    BackupFixture fixture(true);
    fixture.prepareBackup();
    auto manifest = fixture.manifest();
    auto& assets = manifest.object().at("assets").array();
    /* 独立复制后再扩容，避免把即将失效的vector元素引用作为插入来源。 */
    const auto duplicate = assets.at(0);
    assets.push_back(duplicate);
    writeBytes(fixture.backup / "manifest.json", xuyan::package::writeJson(manifest));
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：覆盖普通故障发生在暂存创建之后的清理，数据库/资产损坏和清单语法损坏分别运行。
 * 参数：part为0损坏数据库，1损坏资产，2损坏JSON清单；无默认值。
 * 返回：无。失败：故障未被拒绝或源改变、正式/暂存残留时抛异常。
 * 副作用：仅在独占备份追加单字节或写无效JSON，不读取用户资料。
 */
void testCorruptedBackup(int part) {
    BackupFixture fixture(true);
    fixture.prepareBackup();
    if (part == 2) {
        writeBytes(fixture.backup / "manifest.json", "{");
    } else {
        const auto path = fixture.backup / (part == 0 ? "workspace.sqlite" : "assets/nested/payload.bin");
        writeBytes(path, readBytes(path) + std::string(1, '\x7f'));
    }
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 功能：逐项给资产条目提供非法路径，保留正确摘要；可解析的越界路径配真实自有探针。
 * 参数：kind为0绝对路径，1根名路径，2父分量，3越出备份根，4反斜杠，5冒号，6点分量。
 * 返回：无。失败：路径被接受或失败改变备份/留下正式与暂存时抛异常。
 * 副作用：只修改自有备份清单，所需探针全部位于独占树；不构造指向用户资料的路径。
 */
void testIllegalAssetPath(int kind) {
    BackupFixture fixture(true);
    fixture.prepareBackup();
    const auto bytes = readBytes(fixture.backup / "assets" / "nested" / "payload.bin");
    std::string path;
    switch (kind) {
    case 0: path = (fixture.backup / "assets" / "nested" / "payload.bin").generic_string(); break;
    case 1: path = "C:/assets/payload.bin"; break;
    case 2:
        path = "assets/../payload.bin";
        writeBytes(fixture.backup / "payload.bin", bytes);
        break;
    case 3:
        path = "assets/../../payload.bin";
        writeBytes(fixture.temporary.root() / "payload.bin", bytes);
        break;
    case 4: path = "assets\\nested\\payload.bin"; break;
    case 5:
        /* Windows的NTFS流和Linux的普通冒号文件都必须先由测试自己创建，避免仅因源缺失而绿灯。 */
        path = "assets/nested/payload.bin:alternate";
        writeBytes(fixture.backup / fs::path(path), bytes);
        break;
    case 6: path = "assets/./nested/payload.bin"; break;
    default: throw std::runtime_error("非法路径用例编号错误");
    }
    auto manifest = fixture.manifest();
    manifest.object().at("assets").array().at(0).object().at("path") = JsonValue(path);
    writeBytes(fixture.backup / "manifest.json", xuyan::package::writeJson(manifest));
    const auto before = snapshotTree(fixture.temporary.root());
    const auto result = BackupService::restore(fixture.backup, fixture.destination);
    requireRejected(result, fixture.destination, fixture.temporary.root(), before);
}

/* 职责：区分已确认的链接权限/设施缺失与真实回归失败；无资源/成员，不泛化其他系统错误。 */
class SymlinkUnavailable final : public std::runtime_error {
public:
    /* 功能：保存明确的中文未执行原因及实际错误码分类，便于核对MinGW映射差异。
     * 参数：error为调用期间借用的链接创建错误；reason为已确认的中文条件不足原因，无默认值。
     * 返回：完成异常初始化。失败：字符串分配异常传播。
     * 副作用：仅构造内存异常，不把未执行计为通过。
     */
    SymlinkUnavailable(const std::error_code& error, std::string_view reason)
        : std::runtime_error(std::string(reason) + "，未执行目录符号链接用例；错误码=" + std::to_string(error.value())
                             + "，分类=" + error.category().name()) {}
};

/* 功能：创建指向测试自有目录的链接，明确区分权限不足和其他设施故障。
 * 参数：target为已有自有目录，link为尚不存在的自有链接路径，仅借用至返回，无默认值。
 * 返回：无。失败：权限不足或标准设施明确不支持抛SymlinkUnavailable；其他系统错误或身份错误按失败抛异常。
 * 副作用：先用标准API，Windows设施/权限错误时用原生API及非特权标志回退并输出实际错误诊断。
 *   只创建自有目录链接，不改系统权限或目标；目标全在同一独占根内，守卫删除链接本身。
 */
void createDirectorySymlink(const fs::path& target, const fs::path& link) {
    std::error_code error;
    fs::create_directory_symlink(target, link, error);
#ifdef _WIN32
    if (error == std::errc::function_not_supported || error == std::errc::operation_not_supported
        || error == std::errc::permission_denied || error == std::errc::operation_not_permitted
        || error.value() == ERROR_PRIVILEGE_NOT_HELD) {
        std::cout << "标准链接创建失败；错误码=" << error.value() << "，分类=" << error.category().name()
                  << "；改用Windows原生链接创建核查实际权限。\n";
        /* 0x2是官方允许非特权标志；旧Windows返回无效参数时仅回退目录标志，不改变系统权限。 */
        constexpr DWORD allow_unprivileged = 0x2;
        BOOLEAN created = CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | allow_unprivileged);
        DWORD native_error = created != 0 ? ERROR_SUCCESS : GetLastError();
        if (created == 0 && native_error == ERROR_INVALID_PARAMETER) {
            created = CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY);
            native_error = created != 0 ? ERROR_SUCCESS : GetLastError();
        }
        if (created != 0) error.clear();
        else error = std::error_code(static_cast<int>(native_error), std::system_category());
    }
#endif
    /* MinGW可能把Windows权限错误1314放入generic_category，不能仅按system_category判断。 */
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted
#ifdef _WIN32
        || error.value() == 1314
#endif
    )
        throw SymlinkUnavailable(error, "权限不足");
    /* 当前MinGW的ENOSYS为40；仅匹配标准errc明确的不支持，不以未知数字或通用失败伪造跳过。 */
    if (error == std::errc::function_not_supported || error == std::errc::operation_not_supported
#ifdef _WIN32
        || (error.category() == std::system_category()
            && (error.value() == ERROR_NOT_SUPPORTED || error.value() == ERROR_CALL_NOT_IMPLEMENTED))
#endif
    )
        throw SymlinkUnavailable(error, "当前标准库或文件系统不支持创建目录符号链接");
    require(!error, "目录符号链接创建失败，原因并非权限不足；错误码=" + std::to_string(error.value())
                    + "，分类=" + error.category().name());
    require(isLinkedPath(link), "目录符号链接创建后身份不符");
}

/* 职责：标记待验证的目录链接位置；值只用于本文件测试分支，无资源或生产协议影响。 */
enum class LinkPosition {
    /* 源工作区或备份根自身为链接。 */
    source_root,
    /* 源根的中间祖先目录为链接，最终根为普通目录。 */
    source_parent,
    /* assets根自身为链接。 */
    asset_root,
    /* assets内中间目录为链接，最终资产为普通文件。 */
    asset_parent,
    /* 正式目标本身为已有链接。 */
    target_root,
    /* 尚不存在的正式目标通过链接父目录解析。 */
    target_parent
};

/* 功能：验证create/restore拒绝目录链各层链接，摘要有效也不能绕过路径身份校验。
 * 参数：restore为true测试恢复，否则测试备份；position为链接位置，无默认值。
 * 返回：无。失败：权限/设施不足明确抛未执行异常；非法接受、源/链接目标改变或暂存残留抛断言。
 * 副作用：只移动本测试资产目录并建立指向同一独占树的链接；不访问树外用户资料。
 */
void testDirectorySymlink(bool restore, LinkPosition position) {
    BackupFixture fixture(true);
    if (restore) fixture.prepareBackup();
    const auto root = fixture.temporary.root();
    auto source = restore ? fixture.backup : fixture.source;
    auto target = restore ? fixture.destination : fixture.backup;
    bool target_exists = false;
    switch (position) {
    case LinkPosition::source_root:
        createDirectorySymlink(source, root / "source-link");
        source = root / "source-link";
        break;
    case LinkPosition::source_parent:
        createDirectorySymlink(root, root / "parent-link");
        source = root / "parent-link" / source.filename();
        break;
    case LinkPosition::asset_root:
        fs::rename(source / "assets", root / "linked-assets");
        createDirectorySymlink(root / "linked-assets", source / "assets");
        break;
    case LinkPosition::asset_parent:
        fs::rename(source / "assets" / "nested", root / "linked-nested");
        createDirectorySymlink(root / "linked-nested", source / "assets" / "nested");
        break;
    case LinkPosition::target_root:
        fs::create_directory(root / "existing-target");
        writeBytes(root / "existing-target" / "marker.bin", "\1\2");
        createDirectorySymlink(root / "existing-target", target);
        target_exists = true;
        break;
    case LinkPosition::target_parent:
        fs::create_directory(root / "target-parent");
        createDirectorySymlink(root / "target-parent", root / "target-link");
        target = root / "target-link" / "output";
        break;
    }
    const auto before = snapshotTree(root);
    BackupService service(source / "workspace.sqlite");
    const auto result = restore ? BackupService::restore(source, target) : service.create(target);
    if (target_exists) {
        require(!result.ok() && result.error.has_value(), "已有目标根链接必须拒绝");
        require(snapshotTree(root) == before, "拒绝目标根链接时改变了链接或其自有目标");
    } else {
        requireRejected(result, target, root, before);
    }
}

/* 职责：串行逐条运行测试，收集通过、失败和链接权限/设施不足未执行计数；只拥有整数，无文件资源。
 * 生命周期：main栈上创建，各测试和目录守卫先结束再计数，不跨线程共享。
 */
class TestRunner final {
public:
    /* 功能：运行一个独立用例并捕获其异常，允许其他红色用例继续执行。
     * 参数：name为调用期间借用的中文用例名；function为同步测试函数；args为按值传入的测试参数。
     * 返回：无。失败：回归异常计为失败；SymlinkUnavailable只计未执行；其他未知异常计失败。
     * 副作用：执行指定测试，在控制台输出中文状态并修改统计，不保存函数或参数引用。
     */
    template <typename Function, typename... Args>
    void run(std::string_view name, Function function, Args... args) {
        try {
            std::invoke(function, args...);
            ++passed_;
            std::cout << "通过：" << name << '\n';
        } catch (const SymlinkUnavailable& error) {
            ++not_executed_;
            std::cout << "未执行：" << name << "；" << error.what() << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "失败：" << name << "；" << error.what() << '\n';
        } catch (...) {
            ++failed_;
            std::cerr << "失败：" << name << "；非标准异常\n";
        }
    }

    /* 功能：输出分开的计数和本机覆盖边界，不把未执行算作通过。
     * 参数：无。返回：回归/清理有失败为1；只有链接条件不足未执行为77；全部执行通过为0。
     * 失败：不执行新用例；流异常按标准流配置处理。
     * 副作用：只读统计并中文输出；77供调用方识别待条件，主线程可自行配置CTest策略。
     */
    int finish() const {
        std::cout << "备份安全回归：通过 " << passed_ << "，失败 " << failed_
                  << "，链接权限/设施不足未执行 " << not_executed_ << "，清理失败 " << cleanup_failures << "。\n";
        if (not_executed_ != 0)
            std::cout << "符号链接覆盖待具备创建权限及设施的环境验证，未执行项不属于通过证据。\n";
        if (failed_ != 0 || cleanup_failures != 0) return 1;
        return not_executed_ == 0 ? 0 : 77;
    }

private:
    /* 本次进程中正常返回的用例数，单位项，默认0；run累加，finish读取，不含未执行。 */
    int passed_{0};
    /* 本次回归异常用例数，单位项，默认0；run累加，finish用于非零退出。 */
    int failed_{0};
    /* 仅因创建目录链接权限/设施不足未执行的用例数，单位项，默认0；run累加，finish单独报告。 */
    int not_executed_{0};
};

/* 功能：登记并运行不依赖符号链接设施的45项独立公共API回归。
 * 参数：runner为调用期间借用的串行统计器，无默认值，不转移所有权。
 * 返回：无。失败：各用例异常由runner收集；用例名分配异常可传播。
 * 副作用：只运行自有临时目录测试并输出状态，和链接分组没有共享文件或先后依赖。
 */
void runOrdinaryTests(TestRunner& runner) {
    runner.run("无资产根的空工作区往返", testEmptyRoundTrip, false);
    runner.run("显式空资产根的空工作区往返", testEmptyRoundTrip, true);
    runner.run("5字节二进制资产往返", testBinaryRoundTrip, false);
    runner.run("二进制及零字节资产往返", testBinaryRoundTrip, true);
    runner.run("活动WAL已提交数据在线备份", testActiveWalBackup);
    runner.run("备份目标位于源工作区内", testCreateInsideSource, false);
    runner.run("备份目标位于源资产树内", testCreateInsideSource, true);
    runner.run("恢复目标位于备份树内", testRestoreInsideBackup);
    runner.run("备份空目标", testEmptyDestination, false);
    runner.run("恢复空目标", testEmptyDestination, true);
    runner.run("缺失源数据库不自动建库", testMissingSource, false);
    runner.run("缺失源父目录不自动创建", testMissingSource, true);
    runner.run("备份已有目标与旁边候选保留", testExistingPaths, false, true);
    runner.run("恢复已有目标与旁边候选保留", testExistingPaths, true, true);
    runner.run("合法备份旁边候选保留", testExistingPaths, false, false);
    runner.run("合法恢复旁边候选保留", testExistingPaths, true, false);

    /* 字段故障名称及参数逐项配对，仅用于中文诊断；每次run创建全新夹具，无共享文件。 */
    const std::pair<const char*, SizeFault> size_faults[] = {
        {"缺失", SizeFault::missing}, {"字符串", SizeFault::text}, {"实数", SizeFault::real},
        {"空值", SizeFault::null_value}, {"布尔", SizeFault::boolean},
        {"负数", SizeFault::negative}, {"字节数不匹配", SizeFault::mismatch}
    };
    for (const auto& [name, fault] : size_faults) {
        runner.run(std::string("数据库大小：") + name, testManifestSize, false, fault);
        runner.run(std::string("资产大小：") + name, testManifestSize, true, fault);
    }
    runner.run("摘要正确的非SQLite数据库", testNonSqliteDatabase);
    runner.run("摘要正确的零字节数据库", testEmptyOrSchemaLessDatabase, true);
    runner.run("摘要正确但无核心表的有效SQLite", testEmptyOrSchemaLessDatabase, false);
    runner.run("超4MiB合法清单", testOversizedManifest);
    runner.run("重复资产路径且无残留", testDuplicateAssetPath);
    runner.run("数据库摘要损坏且无残留", testCorruptedBackup, 0);
    runner.run("资产摘要损坏且无残留", testCorruptedBackup, 1);
    runner.run("清单语法损坏且无残留", testCorruptedBackup, 2);

    /* 路径名称按testIllegalAssetPath编号排列，只描述协议错误，不包含真实用户路径。 */
    const char* path_names[] = {"绝对路径", "根名路径", "父分量", "越出备份根", "反斜杠", "冒号", "点分量"};
    for (int index = 0; index < 7; ++index)
        runner.run(std::string("资产非法路径：") + path_names[index], testIllegalAssetPath, index);
}

/* 功能：登记并运行12项目录链链接安全回归，将明确的权限/设施缺失独立记录。
 * 参数：runner为调用期间借用的串行统计器，无默认值，不转移所有权。
 * 返回：无。失败：链接用例异常由runner收集，未知系统错误仍是失败，不泛化跳过。
 * 副作用：只运行自有临时目录测试，链接目标全在同一独占目录中，不接触用户资料。
 */
void runLinkTests(TestRunner& runner) {
    /* 链接位置及中文诊断一一配对，每个位置分别覆盖create和restore，权限未执行单独计数。 */
    const std::pair<const char*, LinkPosition> link_positions[] = {
        {"源根", LinkPosition::source_root}, {"源中间目录", LinkPosition::source_parent},
        {"资产根", LinkPosition::asset_root}, {"资产中间目录", LinkPosition::asset_parent},
        {"目标根", LinkPosition::target_root}, {"目标中间目录", LinkPosition::target_parent}
    };
    for (const auto& [name, position] : link_positions) {
        runner.run(std::string("备份目录符号链接：") + name, testDirectorySymlink, false, position);
        runner.run(std::string("恢复目录符号链接：") + name, testDirectorySymlink, true, position);
    }
}

} // namespace

/* 功能：运行公共BackupService安全回归，支持普通与链接用例分别验证，逐条捕获失败。
 * 参数：argc为含程序名的参数数量，仅允许1或2；argv为运行时借用字符串数组，生命周期覆盖main。
 *   无选项只运行45项普通回归；唯一选项--links-only只运行12项链接回归，不接受资料路径。
 * 返回：0为所选用例全部执行通过，1为至少一个回归/清理失败或参数无效，77为仅链接条件不足未执行。
 * 失败：用例异常由runner逐条收集，不因首项失败提前终止；外层标准异常中文报告并返回1。
 * 副作用：只写独占测试目录并输出中文报告；不修改生产资料，不创建世界/人物或读取小说。
 */
int main(int argc, char* argv[]) {
    try {
        require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--links-only"),
                "测试参数无效；仅支持普通回归或链接回归分组选项");
        TestRunner runner;
        if (argc == 1) runOrdinaryTests(runner);
        else runLinkTests(runner);
        return runner.finish();
    } catch (const std::exception& error) {
        std::cerr << "备份安全测试入口失败；" << error.what() << '\n';
        return 1;
    }
}
