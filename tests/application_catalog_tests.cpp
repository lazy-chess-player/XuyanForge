#include "xuyan/application/world_catalog_service.h"
#include "xuyan/application/provider_connection_service.h"
#include "sqlite_support.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
/* 职责：拥有一个本测试显式创建的临时目录，仅清理成功创建的精确路径。
 * 生命周期：数据库调用先返回并关闭连接，然后守卫析构；不读取用户小说或工作区。
 */
class TemporaryWorkspace final {
public:
    /* 功能：在系统临时根中创建不存在的独占子目录，碰撞时换新身份。
     * 参数：无。返回：拥有已创建目录的守卫。
     * 失败：权限错误或16次碰撞抛异常，不接管已存在目录。
     * 副作用：仅创建本测试的空目录，不生成业务样例。
     */
    TemporaryWorkspace() {
        const auto temporary_root = std::filesystem::temp_directory_path();
        std::mt19937_64 random(std::random_device{}());
        for (int attempt = 0; attempt < 16; ++attempt) {
            root_ = temporary_root / ("xuyan-catalog-test-" + std::to_string(random()));
            if (std::filesystem::create_directory(root_)) { owned_ = true; return; }
        }
        throw std::runtime_error("无法分配独占测试目录");
    }
    /* 功能：仅移除本守卫成功创建的测试子目录。
     * 参数：无。返回：无。失败：清理错误使用error_code，不从析构抛出。
     * 副作用：移除本测试数据库，不扫描或删除其他临时目录及用户资料。
     */
    ~TemporaryWorkspace() {
        if (!owned_) return;
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    /* 功能：禁止复制目录清理责任。参数：另一个守卫。返回：无；编译期拒绝，无副作用。 */
    TemporaryWorkspace(const TemporaryWorkspace&) = delete;
    /* 功能：禁止覆盖清理责任。参数：另一个守卫。返回：无；编译期拒绝，无副作用。 */
    TemporaryWorkspace& operator=(const TemporaryWorkspace&) = delete;
    /* 功能：取得独占目录中的数据库路径。
     * 参数：无。返回：独立路径值。失败：路径分配可抛异常。副作用：不创建文件。
     */
    std::filesystem::path database() const { return root_ / "workspace.sqlite"; }
    /* 功能：取得本测试目录的独立路径，供构造明确失败的数据库目标。
     * 参数：无。返回：目录路径值。失败：路径分配可抛异常。副作用：只读。
     */
    std::filesystem::path root() const { return root_; }
private:
    // 本测试候选目录路径；只有owned_为true时才接管清理，不来自外部参数。
    std::filesystem::path root_;
    // 目录是否确由本守卫成功创建，默认false；构造成功置真，析构据此清理。
    bool owned_{false};
};

/* 职责：仅借助测试目录路径打开一个本机SQLite连接，独占句柄并在作用域结束关闭。
 * 生命周期：须比测试语句活得更久；不跨线程共享，不接触真实工作区。
 */
class TestConnection final {
public:
    /* 功能：打开测试数据库供结构故障注入和回滚后核查。
     * 参数：path为调用期间借用的测试自有文件路径，不允许用户资料。
     * 返回：独占连接对象。失败：打开失败关闭部分连接后抛中文异常。
     * 副作用：可能创建测试数据库，连接由本对象拥有。
     */
    explicit TestConnection(const std::filesystem::path& path) {
        const auto utf8 = path.u8string();
        if (sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &handle_) != SQLITE_OK) {
            sqlite3_close(handle_); handle_ = nullptr;
            throw std::runtime_error("无法打开测试数据库");
        }
    }
    /* 功能：关闭测试连接。参数：无。返回：无。
     * 失败：不从析构抛异常。副作用：释放句柄，所有借用语句须先结束。
     */
    ~TestConnection() { sqlite3_close(handle_); }
    /* 功能：禁止复制连接。参数：另一连接。返回：无，编译期拒绝；无副作用。 */
    TestConnection(const TestConnection&) = delete;
    /* 功能：禁止覆盖连接所有权。参数：另一连接。返回：无，编译期拒绝；无副作用。 */
    TestConnection& operator=(const TestConnection&) = delete;
    /* 功能：提供当前测试连接。参数：无。返回：借用指针，有效期至本对象销毁。
     * 失败：构造成功后非空；副作用：只读成员，不转移连接所有权。
     */
    sqlite3* get() const { return handle_; }
private:
    // 本对象独占的测试文件连接，默认空，构造取得、析构关闭。
    sqlite3* handle_{nullptr};
};

/* 职责：模拟新凭据写入成功、补偿删除失败的系统设施；不保存测试秘密或接触操作系统凭据。
 * 生命周期：测试栈上创建，所有方法同步执行，无成员资源和跨线程访问。
 */
class FailingCompensationCredentials final : public xuyan::application::ICredentialStore {
public:
    /* 功能：模拟初次写入系统凭据成功。
     * 参数：第一个字符串为调用期间借用的凭据引用，第二个为借用的新秘密；均不保留。
     * 返回：成功 true。失败：本替身不模拟初次写入失败。副作用：不写磁盘或内存秘密。
     */
    xuyan::domain::Result<bool> put(const std::string&, const std::string&) override {
        return xuyan::domain::Result<bool>::success(true);
    }
    /* 功能：模拟此前没有同引用的旧秘密。
     * 参数：字符串为调用期间借用的凭据引用，不保存。
     * 返回：missing_context 失败。失败：该失败表示旧秘密不存在，不是设施读故障。
     * 副作用：无，不读取真实系统凭据。
     */
    xuyan::domain::Result<std::string> get(const std::string&) override {
        return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "测试凭据不存在", false, "无"});
    }
    /* 功能：模拟数据库失败后的凭据补偿删除失败。
     * 参数：字符串为调用期间借用的待删除引用，不保存。
     * 返回：设施读写失败 Result。失败：每次调用都按预期失败。
     * 副作用：不删除或暴露任何真实凭据。
     */
    xuyan::domain::Result<bool> remove(const std::string&) override {
        return xuyan::domain::Result<bool>::failure(
            {xuyan::domain::ErrorCode::storage_error, "测试补偿失败", false, "无"});
    }
};

/* 功能：将回归条件转为中文异常报告。
 * 参数：condition为应成立的条件；message为调用期间借用的失败说明。
 * 返回：无。失败：条件为false抛runtime_error。副作用：不修改资料。
 */
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

/* 功能：验证目录用例仅显式创建世界，合法中文名称不按UTF-8字节被提前拒绝。
 * 参数：无。返回：无。失败：用例契约偏离时抛断言异常。
 * 副作用：仅在独占临时数据库创建测试世界，结束清理；不进入正式程序资源。
 */
void testCatalogUseCases() {
    TemporaryWorkspace temporary;
    xuyan::application::WorldCatalogService service(temporary.database());
    const auto initialized = service.initialize();
    require(initialized.ok() && *initialized.value, "工作区结构初始化必须成功");
    const auto initial = service.list();
    require(initial.ok() && initial.value->empty(), "初始化不能预置世界");
    std::string name;
    for (int index = 0; index < 100; ++index) name += "界";
    const auto created = service.create("catalog-test-world", name);
    require(created.ok() && created.value->name == name && created.value->source_id.empty(), "合法中文名称须创建空白世界");
    require(!service.create("catalog-test-world", name).ok(), "重复稳定身份须拒绝而非创建第二世界");
    require(!service.attachSource("catalog-test-world", "missing-source").ok(), "不存在来源不能伪装绑定成功");
    const auto after = xuyan::application::WorldCatalogService(temporary.database()).list();
    require(after.ok() && after.value->size() == 1 && after.value->front().source_id.empty(), "失败绑定不能污染目录");
    name += std::string(30, 'a');
    require(!service.create("over-limit-world", name).ok(), "超过120个码点的名称须拒绝");
}

/* 功能：验证数据库打开失败以中文错误传播，不导致后台异常脱离结果边界。
 * 参数：无。返回：无。失败：错误被当成成功或空列表时断言失败。
 * 副作用：仅把当前测试自有目录用作非法文件目标，不创建用户数据。
 */
void testOpenFailure() {
    TemporaryWorkspace temporary;
    xuyan::application::WorldCatalogService service(temporary.root());
    const auto initialized = service.initialize();
    const auto listed = service.list();
    require(!initialized.ok() && initialized.error.has_value(), "无效数据库目标必须返回初始化错误");
    require(!listed.ok() && listed.error.has_value(), "目录查询失败不能返回成功空列表");
}

/* 功能：验证版本号与世界目录表看似正常、但核心资料表缺失时切换前仍会被拒绝。
 * 参数：无。返回：无；所有检查通过则正常结束。
 * 失败：目录单独读取失败或初始化误报成功时抛中文断言异常。
 * 副作用：只在独占临时数据库创建不完整结构，不接触用户工作区。
 */
void testIncompleteCurrentSchemaRefused() {
    TemporaryWorkspace temporary;
    {
        TestConnection database(temporary.database());
        const char* sql =
            "PRAGMA user_version=30;"
            "CREATE TABLE state_snapshot(branch_id TEXT,commit_id TEXT,parent_commit_id TEXT,"
            "state_hash TEXT,state_json TEXT,created_at TEXT);"
            "CREATE TABLE world_template(id TEXT PRIMARY KEY,name TEXT,source_id TEXT,created_at TEXT);";
        require(sqlite3_exec(database.get(), sql, nullptr, nullptr, nullptr) == SQLITE_OK,
                "不完整当前版本结构须由测试显式创建成功");
    }
    xuyan::application::WorldCatalogService catalog(temporary.database());
    const auto listed = catalog.list();
    require(listed.ok() && listed.value->empty(), "单独读取世界目录不能覆盖其他表缺失的事实");
    const auto initialized = catalog.initialize();
    require(!initialized.ok() && initialized.error.has_value(), "工作区切换前须拒绝缺少核心资料表的数据库");
    TestConnection database(temporary.database());
    xuyan::storage::detail::Statement version(database.get(), "PRAGMA user_version");
    require(xuyan::storage::detail::stepRow(version.get()) && sqlite3_column_int(version.get(), 0) == 30,
            "结构预检失败不得改写工作区版本");
}

/* 功能：验证凭据补偿失败具有独立错误分类，界面可提示人工核对而非普通重试。
 * 参数：无。返回：无，分类正确则正常结束。
 * 失败：连接校验未通过或补偿失败仍被归为普通存储错误时抛断言异常。
 * 副作用：仅使用测试自有无效数据库路径与不保存秘密的替身，不调用网络或系统凭据。
 */
void testCredentialCompensationFailureIsDistinct() {
    TemporaryWorkspace temporary;
    FailingCompensationCredentials credentials;
    xuyan::application::ProviderConnectionService service(temporary.root(), credentials);
    xuyan::domain::ProviderConnection connection;
    connection.name = "测试连接";
    connection.kind = "openai";
    connection.endpoint = "https://api.example.test/v1";
    connection.default_model = "test-model";
    const auto saved = service.save("credential-compensation-test", connection, 0, std::string{"test-secret"});
    require(!saved.ok() && saved.error->code == xuyan::domain::ErrorCode::credential_consistency_failed,
            "凭据补偿失败必须要求人工核对，不能归为普通存储错误");
}

/* 功能：制造空旧快照升级中后续SQL失败，验证删除旧表也随整次迁移回滚。
 * 参数：无。返回：无。失败：旧表结构/用户版本未保留或失败变成功时抛断言异常。
 * 副作用：只在测试自有数据库创建空结构和故障表，不复制旧测试人物或用户资料。
 */
void testEmptySchemaMigrationRollsBack() {
    TemporaryWorkspace temporary;
    {
        TestConnection database(temporary.database());
        // 只生成结构检测所需20列；中间六列不携带任何旧人物映射或业务内容。
        const char* sql =
            "PRAGMA user_version=24; CREATE TABLE state_snapshot("
            "branch_id TEXT,commit_id TEXT,parent_commit_id TEXT,state_hash TEXT,state_json TEXT,"
            "revision INTEGER,turn INTEGER,elapsed_ticks INTEGER,seal_holder_id TEXT,seal_inspected INTEGER,"
            "paused INTEGER,completed INTEGER,narration TEXT,legacy_field_1 TEXT,legacy_field_2 TEXT,"
            "legacy_field_3 TEXT,legacy_field_4 TEXT,legacy_field_5 TEXT,legacy_field_6 TEXT,created_at TEXT);"
            // 预算回填引用total_steps，故意缺失使删除旧表之后的迁移失败。
            "CREATE TABLE extraction_job(id TEXT PRIMARY KEY);";
        require(sqlite3_exec(database.get(), sql, nullptr, nullptr, nullptr) == SQLITE_OK, "迁移故障结构须显式创建成功");
    }
    const auto migrated = xuyan::application::WorldCatalogService(temporary.database()).initialize();
    require(!migrated.ok(), "损坏的升级输入必须拒绝");
    TestConnection database(temporary.database());
    xuyan::storage::detail::Statement columns(database.get(), "PRAGMA table_info(state_snapshot)");
    int count = 0;
    while (xuyan::storage::detail::stepRow(columns.get())) ++count;
    require(count == 20, "失败迁移必须回滚空旧表删除，保留原20列结构");
    xuyan::storage::detail::Statement version(database.get(), "PRAGMA user_version");
    require(xuyan::storage::detail::stepRow(version.get()) && sqlite3_column_int(version.get(), 0) == 24,
            "失败迁移必须保留原用户版本");
}
} // namespace

/* 功能：运行目录应用层回归，覆盖空首启、显式创建、错误传播与跨连接持久化。
 * 参数：无。返回：全部通过为0，异常失败为1。
 * 失败：捕获标准异常并打印中文失败信息。副作用：只操作测试自有临时目录及终端。
 */
int main() {
    try {
        testCatalogUseCases();
        testOpenFailure();
        testIncompleteCurrentSchemaRefused();
        testCredentialCompensationFailureIsDistinct();
        testEmptySchemaMigrationRollsBack();
        std::cout << "世界目录应用回归通过\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "世界目录应用回归失败：" << exception.what() << '\n';
        return 1;
    }
}
