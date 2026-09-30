#include "xuyan/application/world_catalog_service.h"
#include "xuyan/application/provider_connection_service.h"
#include "xuyan/application/package_service.h"
#include "xuyan/package/json.h"
#include "xuyan/package/zip_archive.h"
#include "xuyan/storage/workspace_repository.h"
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

/* 功能：验证未指定包标题/作者时保留用户世界名且不填入虚构作者，显式元数据仍原样保留。
 * 参数：无。返回：无；每种元数据契约成立才正常结束。
 * 失败：创建、导出、归档/清单读取或值不符时抛中文断言异常。
 * 副作用：仅在独占临时目录生成世界与两个包并读取；守卫最终清理，无用户资料或网络访问。
 */
void testPackageMetadataHasNoPresetValues() {
    TemporaryWorkspace temporary;
    xuyan::application::WorldCatalogService catalog(temporary.database());
    require(catalog.create("owned-package-world", "用户实际命名").ok(), "测试世界须由用例显式创建");
    xuyan::application::PackageService service(temporary.database());
    for (const bool explicit_metadata : {false, true}) {
        /* 本轮输出路径位于当前测试独占目录，循环开始前不存在，不覆盖外部文件。 */
        const auto target = temporary.root() / (explicit_metadata ? "explicit.zip" : "inferred.zip");
        const auto exported = service.exportWorld("owned-package-world", target, explicit_metadata ? "作者填写的标题" : "",
                                                  explicit_metadata ? "作者填写的署名" : "");
        require(exported.ok(), "两类元数据输入都须导出成功");
        const auto archive = xuyan::package::readZip(target);
        require(archive.ok(), "测试导出包须可读取");
        /* 清单发现标记初始false，读取manifest后置真；缺清单不能伪装通过字段检查。 */
        bool manifest_found = false;
        for (const auto& entry : *archive.value) {
            if (entry.path != "manifest.json") continue;
            manifest_found = true;
            const auto manifest = xuyan::package::parseJson(entry.data);
            require(manifest.ok(), "清单须是有效结构");
            const auto* title = manifest.value->find("title");
            const auto* author = manifest.value->find("author");
            require(title && title->isString() && title->string() ==
                    (explicit_metadata ? "作者填写的标题" : "用户实际命名"), "未填标题只能来自真实世界名称");
            require(author && author->isString() && author->string() ==
                    (explicit_metadata ? "作者填写的署名" : ""), "未知作者必须留空，不得预填身份");
        }
        require(manifest_found, "导出包必须存在清单");
    }
}

/* 功能：验证明确选择的世界不是目录首项时仍导出该世界，并核对超过旧分页大小的资料隔离。
 * 参数：无。返回：无；清单身份、标题及所有载荷均属于明确选定世界时正常结束。
 * 失败：临时创建、写入、导出、读取或断言失败抛中文异常。
 * 副作用：仅在独占临时目录创建两个世界和205条同名测试资料，写一个测试包；无网络或用户资料访问。
 */
void testWorldExportSelection() {
    TemporaryWorkspace temporary;
    xuyan::storage::WorkspaceRepository repository(temporary.database());
    require(repository.createWorldTemplate("a-first-world", "同名世界").ok(), "先创建目录首项");
    require(repository.createWorldTemplate("z-selected-world", "同名世界").ok(), "后创建明确选定世界");
    xuyan::domain::WorldEntity excluded;
    excluded.id = "other-world-item"; excluded.world_id = "a-first-world";
    excluded.kind = "rule"; excluded.name = "其他世界不得导出的资料";
    require(repository.createEntity("create-other-world", excluded).ok(), "其他世界测试条目须显式创建");
    for (int index = 0; index < 205; ++index) {
        xuyan::domain::WorldEntity entity;
        entity.id = "selection-entity-" + std::to_string(index);
        entity.world_id = "z-selected-world";
        entity.kind = "rule";
        entity.name = "当前世界的用户资料";
        require(repository.createEntity("selection-create-" + entity.id, entity).ok(), "测试条目须显式创建");
    }
    const auto target = temporary.root() / "selected.zip";
    xuyan::domain::WorldEntity deleted;
    deleted.id = "selected-deleted"; deleted.world_id = "z-selected-world";
    deleted.kind = "rule"; deleted.name = "已删除条目";
    const auto created = repository.createEntity("create-deleted", deleted);
    require(created.ok() && repository.deleteEntity("delete-selected", deleted.id, created.value->revision).ok(),
            "测试须显式软删除一条资料");
    xuyan::application::PackageService service(temporary.database());
    const auto exported = service.exportWorld("z-selected-world", target, {}, {});
    require(exported.ok() && exported.value->entity_count == 205, "导出不能回退目录首个世界，须保留选定世界跨页资料");
    const auto archive = xuyan::package::readZip(target);
    require(archive.ok(), "选定世界包须可读取");
    bool world_found = false, entities_found = false;
    for (const auto& entry : *archive.value) {
        if (entry.path == "entities.jsonl") {
            entities_found = true;
            require(entry.data.find("other-world-item") == std::string::npos
                    && entry.data.find("selected-deleted") == std::string::npos, "其他世界及软删除条目不得泄漏入包");
        }
        if (entry.path != "world.json") continue;
        world_found = true;
        const auto world = xuyan::package::parseJson(entry.data);
        require(world.ok() && world.value->find("world_id")
                && world.value->find("world_id")->string() == "z-selected-world", "同名世界必须按稳定标识隔离");
    }
    require(world_found && entities_found, "选定世界包须包含完整必需载荷");
    const auto protected_target = temporary.root() / "protected.zip";
    require(xuyan::package::writeZip(protected_target, {{"keep.txt", "调用者已有的文件内容"}}).ok(), "测试须显式创建受保护输出");
    for (const std::string& id : {std::string{}, std::string{"missing"}, std::string{"' OR 1=1 --"}}) {
        const auto failed = service.exportWorld(id, protected_target, {}, {});
        require(!failed.ok() && failed.error->code == xuyan::domain::ErrorCode::validation_failed,
                "未选择、缺失及伪造世界标识均须明确拒绝");
        const auto preserved = xuyan::package::readZip(protected_target);
        require(preserved.ok() && preserved.value->size() == 1
                && preserved.value->front().data == "调用者已有的文件内容", "校验失败不得覆盖原目标文件");
    }
    require(!service.exportWorld("z-selected-world", {}, {}, {}).ok(), "空输出路径须拒绝");
    require(repository.createWorldTemplate("empty-world", "用户空白世界").ok(), "空世界须显式创建");
    const auto empty = service.exportWorld("empty-world", temporary.root() / "empty.zip", {}, {});
    require(empty.ok() && empty.value->entity_count == 0, "空世界须成功导出零条而非借用其他世界条目");
    {
        TestConnection connection(temporary.database());
        require(sqlite3_exec(connection.get(), "DELETE FROM entity_revision WHERE entity_id='selection-entity-0'",
                             nullptr, nullptr, nullptr) == SQLITE_OK, "测试须显式制造当前修订缺失");
    }
    require(!service.exportWorld("z-selected-world", protected_target, {}, {}).ok(), "当前修订损坏不能静默漏项后成功导出");
    const auto preserved = xuyan::package::readZip(protected_target);
    require(preserved.ok() && preserved.value->size() == 1, "修订损坏须保留既有输出");
}

/* 功能：分别验证原始字段与JSON转义后的32MiB限额，失败必须保留既有输出。
 * 参数：无。返回：无；两种超限与目标保护均满足时正常结束。
 * 失败：测试显式建库、写载荷、导出或保护断言失败抛中文异常。
 * 副作用：仅两个独占临时数据库生成重复字符规模素材；不读取用户小说、不联网，结束清理。
 */
void testWorldExportPayloadLimits() {
    for (const bool escaped : {false, true}) {
        TemporaryWorkspace temporary;
        xuyan::storage::WorkspaceRepository repository(temporary.database());
        require(repository.createWorldTemplate("sized-world", "载荷规模测试").ok(), "规模世界须显式创建");
        const auto destination = temporary.root() / "protected.zip";
        require(xuyan::package::writeZip(destination, {{"keep.txt", "保持原文件"}}).ok(), "保护目标须测试显式创建");
        const int count = escaped ? 22 : 32;
        for (int index = 0; index < count; ++index) {
            xuyan::domain::WorldEntity entity;
            entity.id = "sized-" + std::to_string(index); entity.world_id = "sized-world";
            entity.kind = "rule"; entity.name = "规模资料";
            entity.description = std::string(escaped ? 800000 : 1024 * 1024, escaped ? '\t' : 'a');
            require(repository.createEntity("create-" + entity.id, entity).ok(), "规模载荷须在测试中显式写入");
        }
        const auto snapshot = repository.readWorldExportSnapshot("sized-world");
        require(snapshot.ok() == escaped, "原始字段与转义载荷限额须分别检验，不能事后截断条目");
        const auto exported = xuyan::application::PackageService(temporary.database()).exportWorld("sized-world", destination, {}, {});
        require(!exported.ok() && exported.error->code == xuyan::domain::ErrorCode::validation_failed, "超限导出须返回明确校验错误");
        const auto preserved = xuyan::package::readZip(destination);
        require(preserved.ok() && preserved.value->size() == 1 && preserved.value->front().data == "保持原文件",
                "编码或原始字段超限不得更新既有输出");
    }
}

/* 功能：核对世界条目包100000条数量边界，超限不得静默截断或改目标。
 * 参数：无。返回：无；100001条拒绝、删一条后100000条成功才正常结束。
 * 失败：临时库批量生成、导出、输出保护或边界断言失败抛中文异常。
 * 副作用：仅测试独占临时库用固定SQL显式生成规模条目和包，不进入正式目标或用户工作区，不联网。
 */
void testWorldExportCountLimit() {
    TemporaryWorkspace temporary;
    xuyan::storage::WorkspaceRepository repository(temporary.database());
    require(repository.createWorldTemplate("count-world", "数量边界测试").ok(), "数量世界须显式创建");
    {
        TestConnection connection(temporary.database());
        /* 批量SQL仅为测试生成数量边界，不经过产品初始化，也不读私有素材。 */
        const char* sql =
            "BEGIN; WITH RECURSIVE numbers(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM numbers WHERE n<100000) "
            "INSERT INTO world_entity(id,world_id,head_revision,deleted,created_at) "
            "SELECT 'bulk-'||n,'count-world',1,0,strftime('%Y-%m-%dT%H:%M:%SZ','now') FROM numbers;"
            "INSERT INTO entity_revision(entity_id,revision,kind,name,aliases,tags,description,attributes_json,review_status,deleted,updated_at) "
            "SELECT id,1,'rule','规模资料','','','','{}','accepted',0,created_at FROM world_entity; COMMIT;";
        require(sqlite3_exec(connection.get(), sql, nullptr, nullptr, nullptr) == SQLITE_OK, "规模条目须在测试独占事务显式生成");
    }
    const auto destination = temporary.root() / "count.zip";
    require(xuyan::package::writeZip(destination, {{"keep.txt", "数量超限仍保留"}}).ok(), "数量保护目标须显式创建");
    xuyan::application::PackageService service(temporary.database());
    const auto refused = service.exportWorld("count-world", destination, {}, {});
    require(!refused.ok() && refused.error->code == xuyan::domain::ErrorCode::validation_failed, "100001条不得静默截断导出");
    const auto preserved = xuyan::package::readZip(destination);
    require(preserved.ok() && preserved.value->size() == 1 && preserved.value->front().data == "数量超限仍保留",
            "数量超限须保留既有输出");
    {
        TestConnection connection(temporary.database());
        require(sqlite3_exec(connection.get(),
            "BEGIN; DELETE FROM entity_revision WHERE entity_id='bulk-100000'; DELETE FROM world_entity WHERE id='bulk-100000'; COMMIT;",
            nullptr, nullptr, nullptr) == SQLITE_OK, "测试须显式移除自身生成的超限一项");
    }
    const auto accepted = service.exportWorld("count-world", destination, {}, {});
    require(accepted.ok() && accepted.value->entity_count == 100000, "100000条且载荷合法须完整导出成功");
    require(xuyan::package::readZip(destination).ok(), "数量边界成功包须满足默认读包限额");
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
        testPackageMetadataHasNoPresetValues();
        testWorldExportSelection();
        testWorldExportPayloadLimits();
        testWorldExportCountLimit();
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
