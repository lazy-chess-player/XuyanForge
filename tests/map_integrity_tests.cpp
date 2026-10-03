#include "xuyan/application/world_catalog_service.h"
#include "xuyan/application/workspace_service.h"
#include "xuyan/application/world_graph_service.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <climits>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
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
using xuyan::application::WorldCatalogService;
using xuyan::application::WorkspaceService;
using xuyan::application::WorldGraphService;
using xuyan::domain::ErrorCode;
using xuyan::domain::LocationPlacement;
using xuyan::domain::TravelRoute;
using xuyan::domain::WorldEntity;
using xuyan::domain::MapView;
using Rows = std::vector<std::vector<std::string>>;

/* 本进程独占目录清理失败数，单位次，初始零；目录析构累加，入口据此返回失败；不跨线程共享。 */
int cleanup_failures{0};

/*
 * 功能：检查一个独立测试不变量，不使用可被发布构建关闭的 assert。
 * 参数：condition 为输入条件；message 为调用期间借用的中文失败原因，无默认值。
 * 返回：无；条件成立正常返回。
 * 失败：条件不成立抛 runtime_error，字符串分配异常可传播。
 * 副作用：仅构造异常，不访问文件、数据库或网络；同步调用，不保留引用。
 */
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

/*
 * 功能：提取成功结果的自有副本，并检查成功与错误互斥。
 * 参数：result 为输入结果，按值接收；message 为调用期间借用的中文上下文，无默认值。
 * 返回：拥有全部字段的 T 值，不借用 result 或数据库连接。
 * 失败：结果失败或成功携带错误时抛中文异常；移动/分配异常可传播。
 * 副作用：仅移动输入副本；同步调用，不访问外部资源。
 */
template <typename T>
T success(xuyan::domain::Result<T> result, std::string_view message) {
    if (!result.ok() || result.error)
        throw std::runtime_error(std::string(message) + (result.error ? "；" + result.error->message : "；缺少成功值"));
    return std::move(*result.value);
}

/*
 * 功能：精确断言失败分类，防止异常、成功空结果或另一错误掩盖回归。
 * 参数：result 为调用期间借用的结果；code 为预期内部分类；message 为中文上下文，无默认值。
 * 返回：无；失败结构互斥且分类一致才正常返回。
 * 失败：任一条件不符抛中文异常。
 * 副作用：只读结果，不执行修复、不输出私有资料；同步调用。
 */
template <typename T>
void rejected(const xuyan::domain::Result<T>& result, ErrorCode code, std::string_view message) {
    require(!result.ok() && !result.value && result.error && result.error->code == code, message);
    require(!result.error->message.empty(), "失败结果必须附带原因");
}

/*
 * 职责：用随机名称和原子 create_directory 取得本用例独占目录，碰撞时重试，绝不接管已有目录。
 * 资源与生命周期：拥有成功创建的绝对临时根，析构清理仅该根；禁止复制及移动。
 * 线程：仅用例调用线程使用；库、语句和服务先销毁，目录最后释放，不读取真实用户工作区。
 */
class TemporaryWorkspace final {
public:
    /*
     * 功能：在系统临时目录下原子取得新的测试根，不创建任何业务资料。
     * 参数：无。返回：完成独占路径初始化。
     * 失败：随机源、临时路径、权限错误或连续 64 次碰撞抛异常；碰撞目录保持原样。
     * 副作用：只创建一个带随机后缀的目录，绝对路径记入本对象；同步执行。
     */
    TemporaryWorkspace() {
        const auto parent = fs::weakly_canonical(fs::temp_directory_path());
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto candidate = parent / ("xuyanforge-map-integrity-" + std::to_string(random())
                                              + "-" + std::to_string(random()));
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                root_ = candidate;
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::runtime_error("创建独占地图测试目录失败");
        }
        throw std::runtime_error("独占地图测试目录随机名称连续碰撞");
    }

    /*
     * 功能：清理确已取得且未变成链接的测试根，析构不抛异常。
     * 参数：无。返回：无；释放本用例生成的文件和目录。
     * 失败：根身份或清理异常计入 cleanup_failures，中文报告，进程最终非零退出。
     * 副作用：仅删除拥有的根；删除前核对绝对路径、父目录及非链接身份，不删除临时父目录。
     */
    ~TemporaryWorkspace() noexcept {
        try {
            const auto resolved = fs::weakly_canonical(root_);
            if (!root_.is_absolute() || root_.filename().empty() || resolved != root_
                || root_ == fs::weakly_canonical(fs::temp_directory_path()) || fs::is_symlink(fs::symlink_status(root_))) {
                ++cleanup_failures;
                std::cerr << "失败：独占地图测试根身份发生变化，保留目录\n";
                return;
            }
            std::error_code error;
            fs::remove_all(root_, error);
            if (error) {
                ++cleanup_failures;
                std::cerr << "失败：清理独占地图测试目录失败\n";
            }
        } catch (...) {
            ++cleanup_failures;
            std::cerr << "失败：清理独占地图测试目录异常\n";
        }
    }

    /* 功能：禁止复制独占目录。参数：源守卫，只作编译期声明。返回：无。失败：编译期拒绝。副作用：无。 */
    TemporaryWorkspace(const TemporaryWorkspace&) = delete;
    /* 功能：禁止复制赋值。参数：源守卫，只作编译期声明。返回：无。失败：编译期拒绝。副作用：无。 */
    TemporaryWorkspace& operator=(const TemporaryWorkspace&) = delete;
    /* 功能：禁止移动目录所有权。参数：源守卫，只作编译期声明。返回：无。失败：编译期拒绝。副作用：无。 */
    TemporaryWorkspace(TemporaryWorkspace&&) = delete;
    /* 功能：禁止移动赋值。参数：源守卫，只作编译期声明。返回：无。失败：编译期拒绝。副作用：无。 */
    TemporaryWorkspace& operator=(TemporaryWorkspace&&) = delete;

    /* 功能：返回独占测试库位置。参数：无。返回：绝对路径副本。失败：路径分配可抛异常。
     * 副作用：只读自有路径，不创建文件；副本不依赖守卫内存，但访问须在守卫存活期间。
     */
    fs::path database() const { return root_ / "workspace.sqlite"; }

private:
    /* 成功 create_directory 取得的绝对测试根；初始空，构造写入一次，析构独占清理，无计量单位。 */
    fs::path root_;
};

/* 职责：unique_ptr 的 SQLite 连接删除器，无成员资源；关闭拥有的连接，不跨线程共享。 */
struct DatabaseCloser {
    /* 功能：释放 SQLite 连接。参数：database 为拥有的连接，允许空；返回：无。
     * 失败：不抛异常，close_v2 允许尚待释放的语句延迟关闭。副作用：释放连接，不删除数据库文件。
     */
    void operator()(sqlite3* database) const noexcept { if (database) sqlite3_close_v2(database); }
};

/* 职责：unique_ptr 的 SQLite 语句删除器，无成员资源；语句寿命严格短于连接。 */
struct StatementCloser {
    /* 功能：结束预编译语句。参数：statement 为拥有的语句，允许空；返回：无。
     * 失败：不抛异常，执行错误由 step 调用处检查。副作用：释放语句，不提交事务。
     */
    void operator()(sqlite3_stmt* statement) const noexcept { if (statement) sqlite3_finalize(statement); }
};

/*
 * 职责：测试专用 SQLite RAII 连接，用绑定参数注入历史坏链并读取完整业务快照。
 * 生命周期：独占一条连接，方法内语句先释放，连接最后关闭；不创建生产迁移或默认业务资料。
 * 线程：仅创建线程使用；SQL 参数均为本文件合成资料，不读取真实资料、不联网。
 */
class TestDatabase final {
public:
    /*
     * 功能：打开已由应用服务初始化的独占测试数据库。
     * 参数：path 为输入绝对文件路径，调用期间借用，不允许指向未初始化文件。
     * 返回：完成连接初始化并打开外键检查。
     * 失败：打开或 PRAGMA 失败抛中文异常，部分连接由 RAII 释放。
     * 副作用：仅打开已有测试文件；同步执行，不创建新数据库。
     */
    explicit TestDatabase(const fs::path& path) {
        sqlite3* opened = nullptr;
        const auto utf8 = path.u8string();
        const int code = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &opened,
                                          SQLITE_OPEN_READWRITE, nullptr);
        database_.reset(opened);
        require(code == SQLITE_OK, "打开测试 SQLite 连接失败");
        execute("PRAGMA foreign_keys=ON");
    }

    /*
     * 功能：执行单条测试写入/结构语句，值参数均绑定，历史损坏也仅写自有测试库。
     * 参数：sql 为输入 SQL 模板；parameters 为顺序文本参数，默认空，无调用后引用。
     * 返回：无；SQL 完整结束才返回。
     * 失败：预编译、绑定、执行失败抛中文异常；未提交事务随连接关闭回滚。
     * 副作用：按模板写测试数据或触发器；不自动提交显式事务，不联网。
     */
    void execute(const std::string& sql, const std::vector<std::string>& parameters = {}) {
        auto statement = prepare(sql, parameters);
        const int code = sqlite3_step(statement.get());
        require(code == SQLITE_DONE || (code == SQLITE_ROW && sql.starts_with("PRAGMA ")), "执行测试 SQL 失败");
    }

    /*
     * 功能：读取完整行字段，保留 SQLite 类型及空值，供失败前后精确比较。
     * 参数：sql 为输入只读 SELECT/PRAGMA；parameters 为顺序文本参数，默认空。
     * 返回：自有二维字段值，类型编号前缀区分空值、整数和文本；零行返回空容器。
     * 失败：非只读模板、预编译、绑定或 step 错误抛异常，不伪造空行。
     * 副作用：只读本测试库；语句返回前释放，结果不借用 SQLite 内存。
     */
    Rows rows(const std::string& sql, const std::vector<std::string>& parameters = {}) {
        auto statement = prepare(sql, parameters);
        require(sqlite3_stmt_readonly(statement.get()) != 0, "快照查询不得写数据库");
        Rows result;
        int code = SQLITE_OK;
        while ((code = sqlite3_step(statement.get())) == SQLITE_ROW) {
            std::vector<std::string> row;
            for (int column = 0; column < sqlite3_column_count(statement.get()); ++column) {
                // 类型须在 text 转换前取得；转换后再次调用 column_type 不能可靠证明原 SQLite 类型。
                const int original_type = sqlite3_column_type(statement.get(), column);
                const auto* bytes = sqlite3_column_text(statement.get(), column);
                row.push_back(std::to_string(original_type) + ":"
                    + (bytes ? std::string(reinterpret_cast<const char*>(bytes),
                                          static_cast<std::size_t>(sqlite3_column_bytes(statement.get(), column))) : ""));
            }
            result.push_back(std::move(row));
        }
        require(code == SQLITE_DONE, "读取测试 SQL 行失败");
        return result;
    }

private:
    /*
     * 功能：构造带绑定参数的自有语句，统一检查 SQLite 返回码。
     * 参数：sql 为单条 SQL 输入；parameters 为顺序文本输入，借用至返回，无默认值。
     * 返回：独占语句智能指针，必须在本连接销毁前释放。
     * 失败：预编译、参数数量或绑定失败抛异常，部分语句自动 finalize。
     * 副作用：仅预编译和绑定，不 step、不提交事务；限调用线程。
     */
    std::unique_ptr<sqlite3_stmt, StatementCloser> prepare(
        const std::string& sql, const std::vector<std::string>& parameters) {
        sqlite3_stmt* raw = nullptr;
        const int code = sqlite3_prepare_v2(database_.get(), sql.c_str(), -1, &raw, nullptr);
        std::unique_ptr<sqlite3_stmt, StatementCloser> statement(raw);
        require(code == SQLITE_OK && statement, "预编译测试 SQL 失败");
        require(sqlite3_bind_parameter_count(statement.get()) == static_cast<int>(parameters.size()), "测试 SQL 参数数量不符");
        for (std::size_t index = 0; index < parameters.size(); ++index)
            require(sqlite3_bind_text(statement.get(), static_cast<int>(index + 1), parameters[index].data(),
                    static_cast<int>(parameters[index].size()), SQLITE_TRANSIENT) == SQLITE_OK, "绑定测试 SQL 参数失败");
        return statement;
    }

    /* 独占的 SQLite 连接；初始空，构造取得，类内方法借用，最后自动 close_v2，限本线程。 */
    std::unique_ptr<sqlite3, DatabaseCloser> database_;
};

/*
 * 职责：每个独立用例的纯核心夹具，显式初始化空库并创建两个测试世界。
 * 生命周期：成员按声明顺序初始化、逆序销毁，目录先构造最后清理；服务只持路径不共享连接。
 * 线程：单线程使用，不调用模型、UI、系统凭据或真实用户资料。
 */
struct Fixture final {
    /* 独占目录守卫；默认构造取得随机根，所有服务和测试数据库均先于它销毁。 */
    TemporaryWorkspace temporary;
    /* 独占库绝对路径；构造从守卫取得，之后只读，单位为文件路径，与夹具同寿命。 */
    fs::path path{temporary.database()};
    /* 世界目录应用服务；构造绑定 path，夹具显式初始化和创建世界，不缓存 SQLite 连接。 */
    WorldCatalogService catalog{path};
    /* 条目应用服务；构造绑定 path，测试创建、修改、删除地点时使用，无线程或网络资源。 */
    WorkspaceService workspace{path};
    /* 地图应用服务；构造绑定 path，测试调用保存及读取接口，结果自有，不缓存资料。 */
    WorldGraphService graph{path};

    /*
     * 功能：显式建立空测试结构，验证空首启后才创建两个同名测试世界。
     * 参数：无。返回：完成服务及目录初始化。
     * 失败：初始化、空首启或创建失败抛中文异常，守卫自动清理已生成资料。
     * 副作用：只在独占库创建测试世界元数据，不创建默认地点、路线或来源。
     */
    Fixture() {
        require(success(catalog.initialize(), "初始化测试库"), "初始化必须成功");
        require(success(catalog.list(), "读取空目录").empty(), "全新库禁止预置世界");
        const auto empty = success(graph.loadMap("world-a"), "读取空地图");
        require(empty.locations.empty() && empty.routes.empty(), "全新库禁止预置地图资料");
        success(catalog.create("world-a", "测试内显式创建的世界"), "创建世界甲");
        success(catalog.create("world-b", "测试内显式创建的世界"), "创建世界乙");
    }

    /*
     * 功能：通过条目服务创建作者字段齐全的合成地点，供检验保存地图不覆盖条目资料。
     * 参数：id 为稳定标识；world 为所属世界，默认 world-a；kind 为内部类型，默认 location。
     * 返回：持久化 WorldEntity 自有值，修订由服务分配。
     * 失败：条目创建失败抛异常。
     * 副作用：仅在自有库写条目及条目命令日志；无来源、小说或网络操作。
     */
    WorldEntity entity(const std::string& id, const std::string& world = "world-a", const std::string& kind = "location") {
        WorldEntity value;
        value.id = id;
        value.world_id = world;
        value.kind = kind;
        value.name = "测试地点";
        value.aliases = {"测试别名"};
        value.tags = {"作者标签"};
        value.description = "测试内显式生成的作者说明";
        value.attributes_json = "{\"测试字段\":7}";
        return success(workspace.create("entity-" + id, value), "显式创建测试条目");
    }

    /*
     * 功能：通过地图服务为已有地点创建根标注或合法子标注。
     * 参数：id 为地点稳定标识；parent 为父地点标识，默认空表示根。
     * 返回：已持久化标注的自有值，初始成功修订为 1。
     * 失败：保存失败抛中文异常。
     * 副作用：写自有库标注、语义和图命令日志；未知坐标及无底图保持未知。
     */
    LocationPlacement place(const std::string& id, const std::string& parent = {}) {
        LocationPlacement value;
        value.location_id = id;
        value.parent_location_id = parent;
        return success(graph.saveLocation("place-" + id, value, 0), "显式创建测试标注");
    }

    /*
     * 功能：创建一条世界甲内合法路线，并保留未知耗时和单向假设性质。
     * 参数：无。返回：路线自有值，稳定标识 route、端点 a/b、修订 1。
     * 失败：创建端点、标注或路线失败抛异常。
     * 副作用：只在独占库创建两个地点和一条路线，不共享其他用例数据。
     */
    TravelRoute route() {
        entity("a"); entity("b"); place("a"); place("b");
        TravelRoute value;
        value.id = "route"; value.from_location_id = "a"; value.to_location_id = "b";
        value.bidirectional = false; value.evidence_status = "assumption";
        return success(graph.saveRoute("route-original", value, 0), "创建合法测试路线");
    }

    /*
     * 功能：读取所有地图相关业务表的完整字段快照，检查失败及重放不改既有资料。
     * 参数：无。返回：按固定表序、主键序排列的自有快照，不包含数据库页布局或 WAL 文件。
     * 失败：任一只读查询失败抛异常。
     * 副作用：只读独占库；含条目历史、作者字段和完整命令日志时间，不读取用户资料。
     */
    std::vector<Rows> snapshot() const {
        TestDatabase database(path);
        return {database.rows("SELECT * FROM world_entity ORDER BY id"),
                database.rows("SELECT * FROM entity_revision ORDER BY entity_id,revision"),
                database.rows("SELECT * FROM entity_command_log ORDER BY command_id"),
                database.rows("SELECT * FROM location_placement ORDER BY location_id"),
                database.rows("SELECT * FROM location_semantics ORDER BY location_id"),
                database.rows("SELECT * FROM travel_route ORDER BY id"),
                database.rows("SELECT * FROM world_graph_command_log ORDER BY command_id")};
    }
};

/*
 * 功能：比较地点标注的全部领域字段，坐标空值与已知零不可等同。
 * 参数：actual 为输入实际值；expected 为输入预期值，均只借用至返回。
 * 返回：无。失败：任一字段不同抛中文异常。
 * 副作用：只读内存，无数据库写入、文件或网络操作。
 */
void sameLocation(const LocationPlacement& actual, const LocationPlacement& expected) {
    require(actual.location_id == expected.location_id && actual.parent_location_id == expected.parent_location_id
        && actual.image_x == expected.image_x && actual.image_y == expected.image_y
        && actual.background_asset_ref == expected.background_asset_ref && actual.evidence_status == expected.evidence_status
        && actual.truth_status == expected.truth_status && actual.revision == expected.revision, "地点原有字段、未知值或修订发生变化");
}

/*
 * 功能：比较路线全部领域字段，未知分钟、方向和证据性质均独立比较。
 * 参数：actual 为实际路线；expected 为预期路线，均借用至返回，无默认值。
 * 返回：无。失败：任一字段不符抛异常。
 * 副作用：只读内存，不执行修复或写入。
 */
void sameRoute(const TravelRoute& actual, const TravelRoute& expected) {
    require(actual.id == expected.id && actual.from_location_id == expected.from_location_id
        && actual.to_location_id == expected.to_location_id && actual.travel_minutes == expected.travel_minutes
        && actual.bidirectional == expected.bidirectional && actual.evidence_status == expected.evidence_status
        && actual.revision == expected.revision, "路线原有端点、耗时、方向、证据或修订变化");
}

/*
 * 功能：从自有地图值精确提取一个地点标注，缺失不回退其他项。
 * 参数：view 为输入地图；id 为输入地点标识，均借用至返回。
 * 返回：标注副本，不借用 view 容器。
 * 失败：标识不在结果中抛异常。
 * 副作用：只读内存，不读写数据库。
 */
LocationPlacement location(const MapView& view, const std::string& id) {
    const auto found = std::find_if(view.locations.begin(), view.locations.end(),
        /* 功能：匹配地点标识。参数：item 为借用标注。返回：是否精确匹配。失败：无。副作用：只读捕获 id，同步执行。 */
        [&id](const LocationPlacement& item) { return item.location_id == id; });
    require(found != view.locations.end(), "地图应包含指定地点");
    return *found;
}

/*
 * 职责：历史坏节点分类，仅用于独立测试矩阵，不属于产品协议，默认由调用方显式选择。
 * 资源：无；枚举值不包含资料，单线程值传递。
 */
enum class NodeFault {
    /* 祖先条目跨世界但仍有标注。 */ cross_world,
    /* 祖先条目当前软删除但旧标注保留。 */ deleted,
    /* 祖先当前修订已变成非 location，旧修订仍是 location。 */ changed_kind,
    /* 父链引用根本不存在的条目。 */ missing_entity,
    /* 祖先条目有效但从未加入地图。 */ unplaced,
    /* 已有实体和标注，当前条目头修订不存在。 */ missing_revision,
    /* 两个历史祖先互相指向形成环，环不经过待保存节点。 */ cycle,
    /* 待保存节点被间接选为自己的祖先。 */ self_cycle
};

/*
 * 功能：显式制造一处祖先上下文故障，始终保持待保存节点及直接父节点的合法前置状态。
 * 参数：fixture 为借用独占夹具；fault 为故障类别，无默认值；仅同步修改测试库。
 * 返回：预期错误分类；跨世界 validation_failed、环 rule_conflict、其他 missing_context。
 * 失败：注入或前置建模失败抛异常。
 * 副作用：通过条目服务建立类型新修订；SQL 只注入本用例历史父链或损坏头修订，不联网。
 */
ErrorCode breakNode(Fixture& fixture, NodeFault fault) {
    TestDatabase database(fixture.path);
    switch (fault) {
    case NodeFault::cross_world:
        fixture.entity("bad", "world-b"); fixture.place("bad");
        database.execute("UPDATE location_placement SET parent_location_id=? WHERE location_id='parent'", {"bad"});
        return ErrorCode::validation_failed;
    case NodeFault::deleted:
        fixture.entity("bad"); fixture.place("bad");
        success(fixture.workspace.remove("delete-bad", "bad", 1), "软删除祖先");
        break;
    case NodeFault::changed_kind: {
        auto value = fixture.entity("bad"); fixture.place("bad");
        value.kind = "character";
        success(fixture.workspace.save("change-bad-kind", value, value.revision), "修改祖先当前类型");
        break;
    }
    case NodeFault::missing_entity:
        break;
    case NodeFault::unplaced:
        fixture.entity("bad");
        break;
    case NodeFault::missing_revision:
        fixture.entity("bad"); fixture.place("bad");
        database.execute("UPDATE world_entity SET head_revision=999 WHERE id='bad'");
        break;
    case NodeFault::cycle:
        fixture.entity("bad"); fixture.place("bad");
        database.execute("UPDATE location_placement SET parent_location_id='parent' WHERE location_id='bad'");
        database.execute("UPDATE location_placement SET parent_location_id='bad' WHERE location_id='parent'");
        return ErrorCode::rule_conflict;
    case NodeFault::self_cycle:
        database.execute("UPDATE location_placement SET parent_location_id='child' WHERE location_id='parent'");
        return ErrorCode::rule_conflict;
    }
    database.execute("UPDATE location_placement SET parent_location_id='bad' WHERE location_id='parent'");
    return ErrorCode::missing_context;
}

/*
 * 功能：验证新命令必须检查所有祖先，非法历史链不得视作已到根，拒绝不能改变任一业务字段。
 * 参数：fault 为故障类别；read 为 true 检查地图读明确失败，否则检查保存新父链；无默认值。
 * 返回：无。失败：分类、回滚、历史父级保留任一断言失败抛异常。
 * 副作用：只在本用例独占库创建父/子节点并注入坏链；两个路径各自独立运行。
 */
void testAncestor(NodeFault fault, bool read) {
    Fixture fixture;
    fixture.entity("parent"); fixture.entity("child"); fixture.place("parent");
    const auto original = fixture.place("child");
    const auto code = breakNode(fixture, fault);
    const auto before = fixture.snapshot();
    if (read) {
        rejected(fixture.graph.loadMap("world-a"), code, "历史非法父链读取必须明确失败且分类正确");
    } else {
        auto update = original;
        update.parent_location_id = "parent";
        update.image_x = 3; update.image_y = 9;
        update.background_asset_ref = "测试内底图引用";
        update.evidence_status = "assumption"; update.truth_status = "claim";
        rejected(fixture.graph.saveLocation("ancestor-new-command", update, original.revision), code,
                 "新标注命令必须拒绝坏祖先，不能只检查直接父标注");
    }
    require(fixture.snapshot() == before, "拒绝非法父链不得改标注、语义、条目、历史或命令日志");
}

/*
 * 功能：通过绑定 SQL 创建有限深度的合成父链，避开产品写校验以构造旧历史边界。
 * 参数：fixture 为独占夹具；count 为祖先节点数，范围 1—260；cycle 为尾节点是否回指首节点。
 * 返回：无。失败：输入范围或 SQL 注入失败抛异常，未完成事务回滚。
 * 副作用：只在独占库同一事务创建合成条目、当前修订、标注及语义，无产品初始化样例。
 */
void seedChain(Fixture& fixture, int count, bool cycle) {
    require(count >= 1 && count <= 260, "测试链规模越界");
    TestDatabase database(fixture.path);
    database.execute("BEGIN");
    for (int index = 0; index < count; ++index) {
        const auto id = "ancestor-" + std::to_string(index);
        const auto parent = index + 1 < count ? "ancestor-" + std::to_string(index + 1)
                                             : cycle ? "ancestor-0" : "";
        database.execute("INSERT INTO world_entity(id,world_id,head_revision,deleted,created_at) VALUES(?,'world-a',1,0,'测试时间')", {id});
        database.execute("INSERT INTO entity_revision(entity_id,revision,kind,name,aliases,tags,description,attributes_json,review_status,deleted,updated_at) "
                         "VALUES(?,1,'location','测试祖先','','','','{}','accepted',0,'测试时间')", {id});
        database.execute("INSERT INTO location_placement VALUES(?,?,0,0,0,'','evidence',1)", {id, parent});
        database.execute("INSERT INTO location_semantics VALUES(?,'fact')", {id});
    }
    database.execute("COMMIT");
}

/*
 * 功能：精确检查父链 128 个祖先达到根才允许保存，超限和深处闭环不得放行。
 * 参数：count 为合成祖先数，允许 1—260；cycle 为尾节点回环开关；read 为地图读路径开关。
 * 返回：无。失败：边界、分类或失败写入保护不符抛异常。
 * 副作用：独占库注入合成历史链，再读取或新增子标注；不复用全核心固定路径。
 */
void testDepth(int count, bool cycle, bool read) {
    Fixture fixture;
    fixture.entity("child");
    seedChain(fixture, count, cycle);
    const auto before = fixture.snapshot();
    if (read) {
        const auto result = fixture.graph.loadMap("world-a");
        // 地图节点自身不计为祖先，129 个节点的根链最多含 128 个祖先。
        if (!cycle && count <= 129) require(result.ok(), "历史链最多128个祖先且到根应可读");
        else rejected(result, ErrorCode::rule_conflict, "历史过深/环链必须明确失败");
        require(fixture.snapshot() == before, "地图读深度校验不得写入");
    } else {
        LocationPlacement value; value.location_id = "child"; value.parent_location_id = "ancestor-0";
        const auto result = fixture.graph.saveLocation("depth-command", value, 0);
        if (!cycle && count <= 128) require(success(result, "合法祖先深度边界").revision == 1, "新标注修订为1");
        else {
            rejected(result, ErrorCode::rule_conflict, "超过128个祖先仍未到根或深处成环必须拒绝");
            require(fixture.snapshot() == before, "超深失败不得留标注、语义或日志");
        }
    }
}

/*
 * 功能：检查直接父上下文，防止祖先遍历实现遗漏父节点自身。
 * 参数：fault 为跨世界、删除、变类型、缺失或未标注之一，无默认值。
 * 返回：无。失败：错误分类或业务表保护不符抛异常。
 * 副作用：只在独占库构造父节点故障，再用新命令保存子标注。
 */
void testDirectParent(NodeFault fault) {
    Fixture fixture;
    fixture.entity("child");
    ErrorCode code = ErrorCode::missing_context;
    if (fault != NodeFault::missing_entity) {
        auto parent = fixture.entity("parent", fault == NodeFault::cross_world ? "world-b" : "world-a");
        if (fault != NodeFault::unplaced) fixture.place("parent");
        if (fault == NodeFault::cross_world) code = ErrorCode::validation_failed;
        if (fault == NodeFault::deleted) success(fixture.workspace.remove("delete-parent", "parent", parent.revision), "删除直接父地点");
        if (fault == NodeFault::changed_kind) {
            parent.kind = "character";
            success(fixture.workspace.save("parent-kind", parent, parent.revision), "变更直接父类型");
        }
    }
    const auto before = fixture.snapshot();
    LocationPlacement value; value.location_id = "child"; value.parent_location_id = "parent";
    rejected(fixture.graph.saveLocation("direct-parent", value, 0), code, "直接父上下文必须有效且同世界");
    require(fixture.snapshot() == before, "直接父校验失败必须保持原有资料");
}

/*
 * 功能：检查路线端点当前状态与世界范围，分别覆盖起点/终点而非只验证标注存在。
 * 参数：fault 为跨世界、删除、变类型、缺失或未标注之一；from 为是否损坏起点，无默认值。
 * 返回：无。失败：错误分类或回滚不符抛异常。
 * 副作用：只在独占库建立两个端点并显式损坏其中一侧，不触碰其他用例。
 */
void testRouteEndpoint(NodeFault fault, bool from) {
    Fixture fixture;
    const std::string bad = from ? "a" : "b";
    const std::string good = from ? "b" : "a";
    fixture.entity(good); fixture.place(good);
    ErrorCode code = ErrorCode::missing_context;
    if (fault != NodeFault::missing_entity) {
        auto value = fixture.entity(bad, fault == NodeFault::cross_world ? "world-b" : "world-a");
        if (fault != NodeFault::unplaced) fixture.place(bad);
        if (fault == NodeFault::cross_world) code = ErrorCode::validation_failed;
        if (fault == NodeFault::deleted) success(fixture.workspace.remove("delete-endpoint", bad, value.revision), "删除路线端点");
        if (fault == NodeFault::changed_kind) {
            value.kind = "character";
            success(fixture.workspace.save("endpoint-kind", value, value.revision), "变更路线端点类型");
        }
    }
    const auto before = fixture.snapshot();
    TravelRoute value; value.id = "new-route"; value.from_location_id = "a"; value.to_location_id = "b";
    rejected(fixture.graph.saveRoute("invalid-endpoint", value, 0), code, "路线两端必须是同世界有效且已标注的当前地点");
    require(fixture.snapshot() == before, "非法路线端点不得改变路线、地点或日志");
}

/*
 * 功能：验证已有路线稳定 ID 不能以有效修订重新绑定另一世界的两个合法地点。
 * 参数：无。返回：无。失败：跨世界换绑被允许、分类不正确或旧字段被改时抛异常。
 * 副作用：独占库创建两世界地点与原路线，再尝试非法更新，原路线应完整保留。
 */
void testRouteRebind() {
    Fixture fixture;
    const auto original = fixture.route();
    fixture.entity("other-a", "world-b"); fixture.entity("other-b", "world-b");
    fixture.place("other-a"); fixture.place("other-b");
    auto update = original;
    update.from_location_id = "other-a"; update.to_location_id = "other-b";
    update.travel_minutes = 7; update.bidirectional = true; update.evidence_status = "evidence";
    const auto before = fixture.snapshot();
    rejected(fixture.graph.saveRoute("rebind-world", update, original.revision), ErrorCode::validation_failed,
             "已有路线ID禁止改绑另一个世界");
    require(fixture.snapshot() == before, "跨世界换绑失败必须保留旧路线所有字段与日志");
    const auto map = success(fixture.graph.loadMap("world-a"), "读取原世界路线");
    require(map.routes.size() == 1, "原世界路线必须保留"); sameRoute(map.routes.front(), original);
    require(success(fixture.graph.loadMap("world-b"), "读取另一世界").routes.empty(), "另一世界不能出现原路线");
}

/*
 * 功能：验证命令长度和预期修订校验优先于写入/重放，拒绝不得产生任何业务变更。
 * 参数：route 为是否测试路线；command 为输入命令字节串；revision 为预期修订；无默认值。
 * 返回：无。失败：未返回 validation_failed 或改变资料时抛异常。
 * 副作用：只构造独占测试资料并调用非法命令；INT_MAX 不做加法，防止测试自身有符号溢出。
 */
void testCommandBoundary(bool route, const std::string& command, int revision) {
    Fixture fixture;
    if (route) {
        const auto value = fixture.route();
        const auto before = fixture.snapshot();
        rejected(fixture.graph.saveRoute(command, value, revision), ErrorCode::validation_failed, "路线命令或修订边界必须校验拒绝");
        require(fixture.snapshot() == before, "非法路线命令不能改任何业务资料");
    } else {
        fixture.entity("a"); const auto value = fixture.place("a");
        const auto before = fixture.snapshot();
        rejected(fixture.graph.saveLocation(command, value, revision), ErrorCode::validation_failed, "标注命令或修订边界必须校验拒绝");
        require(fixture.snapshot() == before, "非法标注命令不能改任何业务资料");
    }
}

/*
 * 功能：检查 512 字节命令合法边界、未知值及已知零字段往返，作者条目字段不得被地图保存覆盖。
 * 参数：route 为是否验证路线，无默认值。
 * 返回：无。失败：合法命令、字段往返或作者字段保护失败抛异常。
 * 副作用：仅在独占测试库更新一次标注/路线，再通过全新服务连接读取。
 */
void testLegalBoundary(bool route) {
    Fixture fixture;
    const std::string command(512, 'c');
    if (route) {
        auto value = fixture.route(); value.travel_minutes = 1; value.bidirectional = true; value.evidence_status = "evidence";
        const auto before = fixture.snapshot();
        const auto saved = success(fixture.graph.saveRoute(command, value, value.revision), "512字节路线命令合法");
        value.revision = 2; sameRoute(saved, value);
        const auto map = success(WorldGraphService(fixture.path).loadMap("world-a"), "重开路线地图");
        require(map.routes.size() == 1, "合法路线只保留一条"); sameRoute(map.routes.front(), value);
        const auto after = fixture.snapshot();
        require(std::equal(before.begin(), before.begin() + 5, after.begin()), "路线保存不得覆盖条目、历史、地点或语义");
    } else {
        fixture.entity("a"); auto value = fixture.place("a");
        require(!value.image_x && !value.image_y && value.truth_status == "fact", "未知坐标不能默认填零");
        value.image_x = 0; value.image_y = 0; value.evidence_status = "assumption"; value.truth_status = "claim";
        value.background_asset_ref = "测试地图|背景";
        const auto before = fixture.snapshot();
        const auto saved = success(fixture.graph.saveLocation(command, value, value.revision), "512字节标注命令合法");
        value.revision = 2; sameLocation(saved, value);
        sameLocation(location(success(WorldGraphService(fixture.path).loadMap("world-a"), "重开地点地图"), "a"), value);
        const auto after = fixture.snapshot();
        require(std::equal(before.begin(), before.begin() + 3, after.begin()), "标注保存不得覆盖作者字段及条目修订");
    }
}

/*
 * 功能：检验新地图命令摘要绑定 expected_revision，合法重复为只读，不同修订/字段/记录类型须冲突。
 * 参数：route 为是否验证路线；change 为 0 重放、1 改预期修订、2 改字段、3 混用记录类型。
 * 返回：无。失败：重复重写、摘要未绑定或分类错误时抛异常。
 * 副作用：独占库创建合法记录后执行冲突/重放；所有分支比较完整业务快照。
 */
void testNewReplay(bool route, int change) {
    Fixture fixture;
    if (route) {
        const auto value = fixture.route();
        const auto before = fixture.snapshot();
        if (change == 3) {
            LocationPlacement other; other.location_id = "a";
            rejected(fixture.graph.saveLocation("route-original", other, 1), ErrorCode::command_conflict, "路线命令不能混用标注记录类型");
        } else {
            auto input = value; if (change == 2) input.travel_minutes = 2;
            const auto result = fixture.graph.saveRoute("route-original", input, change == 1 ? 1 : 0);
            if (change == 0) sameRoute(success(result, "新路线命令只读重放"), value);
            else rejected(result, ErrorCode::command_conflict, "新路线摘要必须绑定负载和预期修订");
        }
        require(fixture.snapshot() == before, "路线重放/冲突不得重写字段或日志时间");
    } else {
        fixture.entity("a"); const auto value = fixture.place("a");
        const auto before = fixture.snapshot();
        if (change == 3) {
            TravelRoute other; other.id = "other-route"; other.from_location_id = "a"; other.to_location_id = "missing";
            rejected(fixture.graph.saveRoute("place-a", other, 0), ErrorCode::command_conflict, "标注命令不能混用路线记录类型");
        } else {
            auto input = value; if (change == 2) input.background_asset_ref = "另一底图";
            const auto result = fixture.graph.saveLocation("place-a", input, change == 1 ? 1 : 0);
            if (change == 0) sameLocation(success(result, "新地点命令只读重放"), value);
            else rejected(result, ErrorCode::command_conflict, "新标注摘要必须绑定负载和预期修订");
        }
        require(fixture.snapshot() == before, "标注重放/冲突不得写字段或日志时间");
    }
}

/*
 * 功能：注入旧分隔串命令摘要，验证历史只读重放即便当前端点已删除也不补写历史资料。
 * 参数：route 为是否验证路线；change 为是否改变原负载，无默认值。
 * 返回：无。失败：兼容重放被重写、补写、拒绝或不同负载未冲突时抛异常。
 * 副作用：独占库修改一条自有图命令摘要和软删除端点，再尝试旧命令重放；不修改生产兼容逻辑。
 */
void testLegacyReplay(bool route, bool change) {
    Fixture fixture;
    TestDatabase database(fixture.path);
    if (route) {
        const auto original = fixture.route();
        database.execute("UPDATE world_graph_command_log SET payload_hash=? WHERE command_id='route-original'",
            {"route|a|b|null|0|assumption"});
        success(fixture.workspace.remove("legacy-delete", "b", 1), "旧路线端点后续删除");
        const auto before = fixture.snapshot();
        auto input = original; if (change) input.travel_minutes = 1;
        const auto result = fixture.graph.saveRoute("route-original", input, 1);
        if (change) rejected(result, ErrorCode::command_conflict, "旧路线摘要不能接受不同负载");
        else sameRoute(success(result, "旧路线命令只读兼容"), original);
        require(fixture.snapshot() == before, "旧路线重放不得修复端点或改写旧摘要、修订、日志时间");
    } else {
        fixture.entity("a"); const auto original = fixture.place("a");
        database.execute("UPDATE world_graph_command_log SET payload_hash=? WHERE command_id='place-a'", {"a||null|null||evidence"});
        success(fixture.workspace.remove("legacy-delete", "a", 1), "旧标注条目后续删除");
        const auto before = fixture.snapshot();
        auto input = original; if (change) input.background_asset_ref = "不同底图";
        const auto result = fixture.graph.saveLocation("place-a", input, 1);
        if (change) rejected(result, ErrorCode::command_conflict, "旧地点摘要不能接受不同负载");
        else sameLocation(success(result, "旧地点命令只读兼容"), original);
        require(fixture.snapshot() == before, "旧标注重放不得补写语义或修改条目、旧摘要、日志时间");
    }
}

/*
 * 功能：在最终图命令日志插入处故障，确认前面的地点本体/语义或路线更新整体回滚，并可同命令重试。
 * 参数：route 为是否测试路线；update 为 true 更新原记录、false 首次创建；无默认值。
 * 返回：无。失败：未返回 storage_error、任何表未回滚、重试或幂等性不符时抛异常。
 * 副作用：独占库创建/删除一个失败触发器；只作用于 fault-command，不影响其他用例或用户库。
 */
void testLogRollback(bool route, bool update) {
    Fixture fixture;
    TravelRoute route_value;
    LocationPlacement location_value;
    int revision = 0;
    if (route) {
        if (update) { route_value = fixture.route(); revision = route_value.revision; }
        else {
            fixture.entity("a"); fixture.entity("b"); fixture.place("a"); fixture.place("b");
            route_value.id = "route"; route_value.from_location_id = "a"; route_value.to_location_id = "b";
        }
        route_value.travel_minutes = 23; route_value.bidirectional = true; route_value.evidence_status = "evidence";
    } else {
        fixture.entity("a"); fixture.entity("parent"); fixture.place("parent");
        if (update) { location_value = fixture.place("a"); revision = location_value.revision; }
        else location_value.location_id = "a";
        location_value.parent_location_id = "parent";
        location_value.image_x = 11; location_value.image_y = 19; location_value.background_asset_ref = "测试底图";
        location_value.evidence_status = "assumption"; location_value.truth_status = "claim";
    }
    TestDatabase database(fixture.path);
    database.execute("CREATE TRIGGER fail_final_graph_log BEFORE INSERT ON world_graph_command_log "
                     "WHEN NEW.command_id='fault-command' BEGIN SELECT RAISE(ABORT,'测试最终日志故障'); END");
    const auto before = fixture.snapshot();
    if (route) rejected(fixture.graph.saveRoute("fault-command", route_value, revision), ErrorCode::storage_error, "最终日志故障必须传播路线存储失败");
    else rejected(fixture.graph.saveLocation("fault-command", location_value, revision), ErrorCode::storage_error, "最终日志故障必须传播标注存储失败");
    require(fixture.snapshot() == before, "最终日志失败必须原子回滚本体、语义及日志，所有旧字段保留");
    database.execute("DROP TRIGGER fail_final_graph_log");
    if (route) {
        const auto retried = success(fixture.graph.saveRoute("fault-command", route_value, revision), "相同路线命令故障恢复后可重试");
        route_value.revision = revision + 1; sameRoute(retried, route_value);
        const auto committed = fixture.snapshot();
        sameRoute(success(fixture.graph.saveRoute("fault-command", route_value, revision), "重试后的路线只读重放"), retried);
        require(fixture.snapshot() == committed, "重试成功后的重复路线命令不能再次写入");
    } else {
        const auto retried = success(fixture.graph.saveLocation("fault-command", location_value, revision), "相同标注命令故障恢复后可重试");
        location_value.revision = revision + 1; sameLocation(retried, location_value);
        const auto committed = fixture.snapshot();
        sameLocation(success(fixture.graph.saveLocation("fault-command", location_value, revision), "重试后的标注只读重放"), retried);
        require(fixture.snapshot() == committed, "重试成功后的重复标注命令不能再次写入");
    }
    const auto after = fixture.snapshot();
    require(std::equal(before.begin(), before.begin() + 3, after.begin()), "故障恢复重试不得修改作者条目和历史字段");
}

/*
 * 功能：检查地图只返回当前有效地点，路线起终点都须在同一世界可见地点集合中。
 * 参数：fault 为删除、变类型、跨世界或缺失标注；from 为损坏起点开关，无默认值。
 * 返回：无。失败：非法地点/路线泄漏、字段变化或读取写入时抛异常。
 * 副作用：独占库建立合法原路线，再通过服务/SQL 修改端点当前状态；保留另一世界合法资料验证隔离。
 */
void testMapVisibility(NodeFault fault, bool from) {
    Fixture fixture;
    const auto original = fixture.route();
    const std::string bad = from ? "a" : "b";
    const std::string good = from ? "b" : "a";
    fixture.entity("other", "world-b"); fixture.place("other");
    TestDatabase database(fixture.path);
    if (fault == NodeFault::deleted) success(fixture.workspace.remove("map-delete", bad, 1), "地图端点软删除");
    if (fault == NodeFault::changed_kind) {
        auto value = success(fixture.workspace.load(bad), "读取地图端点条目"); value.kind = "character";
        success(fixture.workspace.save("map-kind", value, value.revision), "地图端点变类型");
    }
    if (fault == NodeFault::cross_world) database.execute("UPDATE world_entity SET world_id='world-b' WHERE id=?", {bad});
    if (fault == NodeFault::unplaced) {
        // 旧数据允许存在悬空端点，仅在注入连接关闭外键，产品连接仍正常开启外键。
        database.execute("PRAGMA foreign_keys=OFF");
        database.execute("DELETE FROM location_semantics WHERE location_id=?", {bad});
        database.execute("DELETE FROM location_placement WHERE location_id=?", {bad});
    }
    const auto before = fixture.snapshot();
    const auto map = success(fixture.graph.loadMap("world-a"), "读取过滤后的地图");
    require(map.locations.size() == 1 && map.locations.front().location_id == good, "地图只能返回当前世界未删且当前类型为location的标注");
    require(map.routes.empty(), "任意路线端点不可见时整条路线不得出现");
    const auto other = success(fixture.graph.loadMap("world-b"), "读取另一世界地图");
    require(other.routes.empty(), "跨世界路线不得在另一世界泄漏");
    require(fixture.snapshot() == before, "地图过滤不能改写历史路线或字段");
    require(original.revision == 1, "原路线测试前置修订有效");
}

/*
 * 功能：核对正常地图完整字段、未知分钟和父子关系，并检查同世界路线可更新端点。
 * 参数：无。返回：无。失败：读取缺项、未知补值、字段或更新错误抛异常。
 * 副作用：独占库显式创建合法层级和路线；不引入底图文件或 UI。
 */
void testHealthyMap() {
    Fixture fixture;
    const auto original = fixture.route();
    fixture.entity("c"); const auto child = fixture.place("c", "a");
    const auto before = fixture.snapshot();
    const auto map = success(fixture.graph.loadMap("world-a"), "合法地图读取");
    require(map.locations.size() == 3 && map.routes.size() == 1, "合法地图地点与路线数量正确");
    sameLocation(location(map, "c"), child); sameRoute(map.routes.front(), original);
    require(!map.routes.front().travel_minutes, "未知旅行耗时必须保持空值");
    require(fixture.snapshot() == before, "正常地图读不得更新任何表");
    auto update = original; update.to_location_id = "c";
    const auto saved = success(fixture.graph.saveRoute("same-world-rebind", update, original.revision), "同世界合法端点更新");
    update.revision = 2; sameRoute(saved, update);
}

/*
 * 功能：验证新的命令标识不能绕过标注/路线自身的修订冲突，拒绝不改作者资料。
 * 参数：route 为 true 测试路线、false 测试标注；按值传入，无默认值。
 * 返回：无。失败：过期修订未返回 revision_conflict 或有写入时抛中文异常。
 * 副作用：仅在独占夹具内创建记录并尝试过期写入，保留全部业务表快照，不联网。
 */
void testStaleRevision(bool route) {
    Fixture fixture;
    if (route) {
        auto value = fixture.route();
        value.travel_minutes = 31;
        const auto before = fixture.snapshot();
        rejected(fixture.graph.saveRoute("new-stale-command", value, 0), ErrorCode::revision_conflict,
                 "新路线命令不能覆盖已有非零修订");
        require(fixture.snapshot() == before, "过期路线修订不能产生任何写入");
    } else {
        fixture.entity("a");
        auto value = fixture.place("a");
        value.image_x = 8; value.image_y = 13;
        const auto before = fixture.snapshot();
        rejected(fixture.graph.saveLocation("new-stale-command", value, 0), ErrorCode::revision_conflict,
                 "新标注命令不能覆盖已有非零修订");
        require(fixture.snapshot() == before, "过期标注修订不能产生任何写入");
    }
}

/*
 * 功能：用两个分隔串会碰撞的合法端点组合检验结构化路线命令身份，不能把另一请求当重放。
 * 参数：无。返回：无；第二请求必须 command_conflict，原路线、全部作者字段和日志保留。
 * 失败：建立夹具、原请求或碰撞拒绝断言失败时抛中文异常。
 * 副作用：只在独占库显式创建四个含分隔符的合成地点和一条路线，不恢复历史样例或访问用户资料。
 */
void testRouteDelimiterCollision() {
    Fixture fixture;
    for (const auto& id : {"a|b", "c", "a", "b|c"}) {
        fixture.entity(id);
        fixture.place(id);
    }
    TravelRoute value;
    value.id = "delimiter-route"; value.from_location_id = "a|b"; value.to_location_id = "c";
    const auto saved = success(fixture.graph.saveRoute("delimiter-command", value, 0), "保存含分隔符合法端点");
    const auto before = fixture.snapshot();
    value.from_location_id = "a"; value.to_location_id = "b|c";
    rejected(fixture.graph.saveRoute("delimiter-command", value, 0), ErrorCode::command_conflict,
             "不同端点组合不能因分隔串相同而成为重放");
    require(fixture.snapshot() == before, "命令身份碰撞拒绝不得修改任何表");
    const auto map = success(fixture.graph.loadMap("world-a"), "核对原含分隔符路线");
    require(map.routes.size() == 1, "碰撞请求不能新增路线");
    sameRoute(map.routes.front(), saved);
}

/*
 * 功能：拒绝仅凭属性文本伪装的未确认地点，合法 typed 说法/假设须由接受映射证明来源。
 * 参数：无。返回：无；地图隐藏伪装节点及路线，新地点/路线写入均返回 missing_context。
 * 失败：伪装被显示、被当有效端点或业务表变化时抛中文异常。
 * 副作用：只在独占夹具将地点改为 other 并填写合成元数据，不建立真实候选来源、不调用模型。
 */
void testForgedUnconfirmedLocation() {
    Fixture fixture;
    auto route = fixture.route();
    auto entity = success(fixture.workspace.load("a"), "读取伪装测试端点");
    entity.kind = "other";
    entity.attributes_json = "{\"kind\":\"location\",\"xuyan_truth_status\":\"claim\"}";
    success(fixture.workspace.save("forge-location-type", entity, entity.revision), "修改为无接受映射的其他条目");
    const auto before = fixture.snapshot();
    const auto map = success(fixture.graph.loadMap("world-a"), "读取属性伪装过滤结果");
    require(map.locations.size() == 1 && map.locations.front().location_id == "b" && map.routes.empty(),
            "没有接受映射的属性文本不能伪装为未确认地点");
    rejected(fixture.graph.saveRoute("forge-route-use", route, route.revision), ErrorCode::missing_context,
             "伪装未确认地点不能成为路线有效端点");
    LocationPlacement placement; placement.location_id = "a";
    rejected(fixture.graph.saveLocation("forge-placement-use", placement, 1), ErrorCode::missing_context,
             "非地点条目不能凭元数据保存地点标注");
    require(fixture.snapshot() == before, "伪装过滤及拒绝不得改写历史记录");
}

/*
 * 职责：按中文分组逐条捕获回归异常并独立计数；一个红色用例不阻止后续组执行。
 * 生命周期：main 栈对象，只拥有计数及分组名称，不拥有测试资料、不跨线程共享。
 */
class TestRunner final {
public:
    /*
     * 功能：开始新分组，先打印上一组独立计数。
     * 参数：name 为借用中文组名，不可空，无默认值。
     * 返回：无。失败：字符串分配异常可传播。
     * 副作用：输出中文组状态并重置组计数，累计总数不变。
     */
    void group(std::string_view name) {
        reportGroup(); group_ = name; group_passed_ = 0; group_failed_ = 0;
    }

    /*
     * 功能：同步运行一个独立回归并捕获异常，所有用例使用自己的 Fixture。
     * 参数：name 为中文用例名；function 为可调用对象，借用至返回；args 为按值测试参数，无默认值。
     * 返回：无。失败：标准/非标准异常均计为失败，继续后续用例，不允许跳过算通过。
     * 副作用：运行自有目录测试，中文输出并更新分组和总计数，不保存回调或参数引用。
     */
    template <typename Function, typename... Args>
    void run(std::string_view name, Function function, Args... args) {
        try {
            std::invoke(function, args...);
            ++passed_; ++group_passed_;
            std::cout << "通过：" << group_ << " / " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_; ++group_failed_;
            std::cerr << "失败：" << group_ << " / " << name << "；" << error.what() << '\n';
        } catch (...) {
            ++failed_; ++group_failed_;
            std::cerr << "失败：" << group_ << " / " << name << "；非标准异常\n";
        }
    }

    /*
     * 功能：打印最后分组和总计数，以回归/清理共同决定退出状态。
     * 参数：无。返回：全部实际执行且清理成功为 0，否则 1；无跳过分支。
     * 失败：流错误按标准流配置处理，不执行新测试。
     * 副作用：中文输出统计，只读计数，不清理或操作数据库。
     */
    int finish() const {
        reportGroup();
        std::cout << "地图完整性回归总计：通过 " << passed_ << "，失败 " << failed_
                  << "，清理失败 " << cleanup_failures << "。\n";
        return failed_ == 0 && cleanup_failures == 0 ? 0 : 1;
    }

private:
    /* 功能：输出当前组计数。参数：无。返回：无。失败：标准流错误依配置处理。
     * 副作用：只读统计并中文输出；空组名表示尚未开始，不输出虚假分组。
     */
    void reportGroup() const {
        if (!group_.empty()) std::cout << "分组计数：" << group_ << "，通过 " << group_passed_ << "，失败 " << group_failed_ << "。\n";
    }
    /* 当前中文组名；默认空，group 赋值，run/reportGroup 读取，与统计器同寿命。 */
    std::string group_;
    /* 本进程正常完成的用例数，单位项，默认0；run累加，finish读取，不含失败或清理计数。 */
    int passed_{0};
    /* 本进程异常用例数，单位项，默认0；run累加，finish用于非零退出。 */
    int failed_{0};
    /* 当前组通过数，单位项，默认0；group重置，run累加，reportGroup读取。 */
    int group_passed_{0};
    /* 当前组失败数，单位项，默认0；group重置，run累加，reportGroup读取。 */
    int group_failed_{0};
};

/*
 * 功能：注册独立测试矩阵，每次 run 都产生全新随机独占库，失败不短路其他边界。
 * 参数：runner 为借用串行统计器，调用期间有效，不转移所有权，无默认值。
 * 返回：无。失败：单用例异常由 runner 收集，注册/名称分配异常可传播到入口。
 * 副作用：只运行本文件纯核心目标，不启动全核心、网络、UI或读取用户资料。
 */
void runTests(TestRunner& runner) {
    /* 故障协议值与中文诊断配对，仅在测试进程内构造，不进入产品默认数据。 */
    const std::pair<const char*, NodeFault> context_faults[] = {
        {"跨世界", NodeFault::cross_world}, {"软删除", NodeFault::deleted},
        {"当前类型改变", NodeFault::changed_kind}, {"实体不存在", NodeFault::missing_entity},
        {"从未标注", NodeFault::unplaced}
    };
    runner.group("地点父级与所有祖先");
    for (const auto& [name, fault] : context_faults) {
        runner.run(std::string("直接父：") + name, testDirectParent, fault);
        runner.run(std::string("间接祖先：") + name, testAncestor, fault, false);
    }
    runner.run("祖先当前修订缺失", testAncestor, NodeFault::missing_revision, false);
    runner.run("祖先独立成环", testAncestor, NodeFault::cycle, false);
    runner.run("间接回到当前地点", testAncestor, NodeFault::self_cycle, false);
    for (const int count : {1, 127, 128, 129, 130, 260})
        runner.run("祖先数=" + std::to_string(count), testDepth, count, false, false);
    runner.run("129层深处环", testDepth, 129, true, false);

    runner.group("路线端点与世界归属");
    for (const auto& [name, fault] : context_faults) {
        runner.run(std::string("起点：") + name, testRouteEndpoint, fault, true);
        runner.run(std::string("终点：") + name, testRouteEndpoint, fault, false);
    }
    runner.run("已有路线不得换绑另一世界", testRouteRebind);
    runner.run("结构化端点分隔符身份不碰撞", testRouteDelimiterCollision);

    runner.group("命令与修订边界");
    for (const bool route : {false, true}) {
        const std::string kind = route ? "路线：" : "标注：";
        runner.run(kind + "空命令", testCommandBoundary, route, std::string{}, 1);
        runner.run(kind + "513字节命令", testCommandBoundary, route, std::string(513, 'c'), 1);
        runner.run(kind + "负修订", testCommandBoundary, route, std::string{"negative-revision"}, -1);
        runner.run(kind + "最小整数修订", testCommandBoundary, route, std::string{"minimum-revision"}, INT_MIN);
        runner.run(kind + "最大整数修订", testCommandBoundary, route, std::string{"maximum-revision"}, INT_MAX);
        runner.run(kind + "512字节合法命令与字段保护", testLegalBoundary, route);
        runner.run(kind + "新命令过期修订拒绝", testStaleRevision, route);
    }

    runner.group("新命令幂等与历史只读兼容");
    const std::array<const char*, 4> replay_names{"相同命令只读重放", "预期修订改变冲突", "负载改变冲突", "记录类型冲突"};
    for (const bool route : {false, true}) {
        const std::string kind = route ? "路线：" : "标注：";
        for (int change = 0; change < 4; ++change)
            runner.run(kind + replay_names[static_cast<std::size_t>(change)], testNewReplay, route, change);
        runner.run(kind + "旧摘要只读且不修复已删端点", testLegacyReplay, route, false);
        runner.run(kind + "旧摘要不同负载冲突", testLegacyReplay, route, true);
    }

    runner.group("最终日志故障回滚与同命令重试");
    for (const bool route : {false, true}) {
        const std::string kind = route ? "路线：" : "地点本体及语义：";
        runner.run(kind + "首次创建回滚", testLogRollback, route, false);
        runner.run(kind + "已有字段更新回滚", testLogRollback, route, true);
    }

    runner.group("地图读取与历史坏链");
    runner.run("合法层级完整字段与同世界路线更新", testHealthyMap);
    runner.run("属性文本不能伪装未确认地点", testForgedUnconfirmedLocation);
    for (const auto& [name, fault] : context_faults) {
        runner.run(std::string("历史祖先：") + name, testAncestor, fault, true);
        if (fault == NodeFault::missing_entity) continue;
        runner.run(std::string("隐藏起点：") + name, testMapVisibility, fault, true);
        runner.run(std::string("隐藏终点：") + name, testMapVisibility, fault, false);
    }
    runner.run("历史祖先独立环", testAncestor, NodeFault::cycle, true);
    runner.run("历史链128个祖先到根", testDepth, 129, false, true);
    runner.run("历史链129个祖先未到根", testDepth, 130, false, true);
    runner.run("历史深处环", testDepth, 129, true, true);
}
} // namespace

/*
 * 功能：独立纯核心地图完整性测试入口，适用于 main 接入独立 CMake/CTest 目标后的 red/green 验证。
 * 参数：无；不接受工作区、小说、模型、凭据或网络地址，全部资料由测试显式创建。
 * 返回：0 为所列测试全部实际执行通过且独占目录清理成功，1 为回归/清理/入口异常。
 * 失败：各测试异常独立计数继续执行；入口异常中文报告并非零退出，无跳过算通过。
 * 副作用：只写随机独占临时库并输出中文分组报告，不修改生产、CMake、文档或UI。
 */
int main() {
    try {
        TestRunner runner;
        runTests(runner);
        return runner.finish();
    } catch (const std::exception& error) {
        std::cerr << "地图完整性回归入口失败；" << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "地图完整性回归入口失败；非标准异常\n";
        return 1;
    }
}
