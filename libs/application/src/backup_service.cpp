#include "xuyan/application/backup_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/package/json.h"
#include "xuyan/storage/workspace_repository.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <set>

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
 * 功能：把备份或恢复失败包装成带操作建议的存储错误，不附加资产正文。
 * 参数：message：输入，按值接收并移入结果的原因文本，无默认值，可为空；不对文本作脱敏。
 * 返回：storage_error 错误值，retryable 为 false，含检查路径、完整性和空间的建议。
 * 失败：字符串分配异常可传播。
 * 副作用：仅构造内存对象；调用方传入的文件/仓储异常消息可能含本地路径，不宜直接上传。
 * 线程与生命周期：调用线程同步执行，不保存输入引用，返回对象独立拥有消息。
 */
Error backupError(std::string message) { return {ErrorCode::storage_error, std::move(message), false, "检查备份路径、完整性和磁盘空间"}; }

/*
 * 功能：先检查文件大小，再以二进制方式读取备份数据库、清单或资产。
 * 参数：
 *   path：输入，只借用至返回的文件路径，无默认值，须为可取得大小且可读取的文件。
 *   limit：输入，允许的最大字节数，默认 128 MiB；0 仅允许检查时大小为 0 的文件。
 * 返回：独立拥有的文件字节，零字节文件返回空串。
 * 失败：大小超限、元数据读取或打开失败时抛异常，分配异常可传播；流中途读错不单独检查。
 * 副作用：只读并整文件载入内存；大小检查与实际读取分开执行，文件增长时不再次限流。
 * 线程与生命周期：调用线程同步执行，局部流在退出时关闭，不保留路径引用。
 */
std::string readFile(const std::filesystem::path& path, std::uintmax_t limit = maximum_asset_file) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("备份文件超过大小上限");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("无法读取备份文件");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/*
 * 功能：创建目标父目录并将字节写入备份暂存文件。
 * 参数：
 *   path：输入，目标文件路径，无默认值，只借用至返回；父目录须可创建。
 *   bytes：输入，原始字节视图，无默认值，允许为空，单位字节；底层内存须有效至写入结束。
 * 返回：无。
 * 失败：建目录、打开或 write 后流状态失败时抛异常；关闭时写入失败不单独检查，路径分配异常可传播。
 * 副作用：创建父目录，以截断模式写目标，不做 fsync 或内容脱敏。
 * 线程与生命周期：调用线程同步执行，局部流退出时关闭，不保存路径引用或字节视图。
 */
void writeFile(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("无法创建备份文件");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("写入备份文件失败");
}

/*
 * 功能：在正式目标的父目录中生成本次操作的暂存目录名。
 * 参数：destination：输入，正式目录路径，无默认值，只借用至返回；本函数不校验空值或是否存在。
 * 返回：目标父目录下由目标文件名及单调时钟原生计数生成的 .partial 路径；计数单位取决于 steady_clock，不是日期。
 * 失败：路径编码转换或分配异常可传播。
 * 副作用：只计算路径，不创建/独占目录，也不保证跨进程唯一或已解析到安全范围。
 * 线程与生命周期：调用线程同步执行，无持久状态，不保存输入引用。
 */
std::filesystem::path stagingPath(const std::filesystem::path& destination) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() / (destination.filename().string() + ".partial-" + std::to_string(stamp));
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
 * 功能：对资产路径作词法检查，拒绝绝对路径、点分量及不以 assets/ 开头的路径。
 * 参数：value：输入，文件系统路径字符串，无默认值，只借用至返回；空值、绝对路径、反斜杠及 .、.. 分量无效。
 * 返回：无；通过时不改写输入，不证明文件真实位于源根之内。
 * 失败：不满足上述检查时抛 runtime_error；路径解析/分配异常可传播，不单独拒绝根名或检查中间符号链接。
 * 副作用：纯内存检查，不读取文件或规范化真实路径。
 * 线程与生命周期：调用线程同步执行，不保留输入引用。
 */
void validateRelativeAssetPath(const std::string& value) {
    const std::filesystem::path path(value);
    if (path.empty() || path.is_absolute() || value.find('\\') != std::string::npos)
        throw std::runtime_error("备份资产路径必须是规范相对路径");
    for (const auto& component : path) if (component == ".." || component == ".")
        throw std::runtime_error("备份资产路径包含越界分量");
    if (path.generic_string().rfind("assets/", 0) != 0) throw std::runtime_error("备份资产不在 assets 目录");
}

} // namespace

BackupService::BackupService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

Result<BackupReport> BackupService::create(const std::filesystem::path& destination_directory) {
    /* 暂存路径在校验和 try 之前生成；只由名称与单调计数推导，尚未独占创建或解析真实父目录。 */
    const auto staging = stagingPath(destination_directory);
    try {
        if (destination_directory.empty() || std::filesystem::exists(destination_directory))
            return Result<BackupReport>::failure(backupError("备份目标必须是尚不存在的新目录"));
        std::filesystem::create_directories(staging);
        const auto database_target = staging / "workspace.sqlite";
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto database_backup = repository.backupTo(database_target);
        if (!database_backup.ok()) throw std::runtime_error(database_backup.error->message);

        /* 数据库先完成在线快照，资产随后逐个复制；二者不在同一一致性边界，调用方须保持资产稳定。 */
        JsonValue::Array assets;
        BackupReport report{database_target};
        /* 仅扫描源数据库父目录/assets；无目录时资产数为 0。目录迭代可能跳过无权限子目录，且不检查根自身的符号链接。 */
        const auto assets_root = database_path_.parent_path() / "assets";
        if (std::filesystem::exists(assets_root)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     assets_root, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_symlink()) throw std::runtime_error("资产目录包含符号链接，拒绝备份");
                if (!entry.is_regular_file()) continue;
                if (++report.asset_count > maximum_files) throw std::runtime_error("资产文件数量超过备份上限");
                const auto size = entry.file_size();
                if (size > maximum_asset_file || report.asset_bytes + size > maximum_total)
                    throw std::runtime_error("资产大小超过备份上限");
                report.asset_bytes += size;
                const auto relative = std::filesystem::relative(entry.path(), database_path_.parent_path()).generic_string();
                validateRelativeAssetPath(relative);
                const auto bytes = readFile(entry.path());
                writeFile(staging / relative, bytes);
                assets.emplace_back(JsonValue::Object{{"path", relative}, {"sha256", xuyan::domain::sha256(bytes)},
                                                       {"size", static_cast<std::int64_t>(size)}});
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
        writeFile(staging / "manifest.json", xuyan::package::writeJson(manifest));
        if (!destination_directory.parent_path().empty()) std::filesystem::create_directories(destination_directory.parent_path());
        std::filesystem::rename(staging, destination_directory);
        report.workspace_database = destination_directory / "workspace.sqlite";
        return Result<BackupReport>::success(std::move(report));
    } catch (const std::exception& exception) {
        /* 仅尽力递归删除计算所得暂存路径，忽略清理错误；不清理父目录，也不回滚已重命名的正式目标。 */
        std::error_code ignored; std::filesystem::remove_all(staging, ignored);
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

Result<BackupReport> BackupService::restore(
    const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory) {
    /* 与 create 相同，暂存路径生成不在异常捕获范围内；后续失败清理仅使用这一计算结果。 */
    const auto staging = stagingPath(destination_directory);
    try {
        /* 先核对协议及 credentials_included=false；这不识别文件内容中的凭据，也未单独拒绝空目标或源/目标包含关系。 */
        if (!std::filesystem::is_directory(backup_directory) || std::filesystem::exists(destination_directory))
            return Result<BackupReport>::failure(backupError("备份源无效或恢复目标已存在"));
        if (std::filesystem::is_symlink(backup_directory / "manifest.json")) throw std::runtime_error("备份清单不能是符号链接");
        const auto manifest_bytes = readFile(backup_directory / "manifest.json", 4 * 1024 * 1024);
        auto parsed = xuyan::package::parseJson(manifest_bytes);
        if (!parsed.ok() || !parsed.value->isObject() || requiredString(*parsed.value, "format") != "xuyan-backup")
            throw std::runtime_error("备份清单格式无效");
        const auto* version = parsed.value->find("version");
        const auto* database = parsed.value->find("database"); const auto* assets = parsed.value->find("assets");
        const auto* credentials = parsed.value->find("credentials_included");
        if (version == nullptr || !version->isInteger() || version->integer() != 1 || database == nullptr
            || !database->isObject() || assets == nullptr || !assets->isArray() || credentials == nullptr
            || !credentials->isBool() || credentials->boolean()) throw std::runtime_error("不支持的备份版本或凭据标记");
        const auto database_path = requiredString(*database, "path");
        if (database_path != "workspace.sqlite") throw std::runtime_error("备份数据库路径无效");
        if (std::filesystem::is_symlink(backup_directory / database_path)) throw std::runtime_error("备份数据库不能是符号链接");
        /* 数据库固定为 workspace.sqlite，按实际字节核对 SHA-256；清单 size 字段未参与校验，也不打开 SQLite 验证可用性。 */
        const auto database_bytes = readFile(backup_directory / database_path, 2ULL * 1024 * 1024 * 1024);
        if (xuyan::domain::sha256(database_bytes) != requiredString(*database, "sha256"))
            throw std::runtime_error("备份数据库摘要不匹配");
        std::filesystem::create_directories(staging); writeFile(staging / database_path, database_bytes);
        BackupReport report{destination_directory / "workspace.sqlite"};
        /* 本次清单已处理的原始路径字符串，初始空，用于拒绝相同文本重复；不合并大小写、根名或文件系统别名。 */
        std::set<std::string> seen_paths;
        for (const auto& item : assets->array()) {
            if (!item.isObject()) throw std::runtime_error("备份资产条目无效");
            const auto relative = requiredString(item, "path"); validateRelativeAssetPath(relative);
            if (!seen_paths.insert(relative).second) throw std::runtime_error("备份清单包含重复资产路径");
            /* 这里只检查最终条目，不检查备份根或中间目录的符号链接链；摘要一致也不证明源路径未越出根。 */
            if (std::filesystem::is_symlink(backup_directory / relative)) throw std::runtime_error("备份资产不能是符号链接");
            if (++report.asset_count > maximum_files) throw std::runtime_error("备份资产文件数量超过上限");
            const auto bytes = readFile(backup_directory / relative);
            if (xuyan::domain::sha256(bytes) != requiredString(item, "sha256")) throw std::runtime_error("备份资产摘要不匹配");
            if (report.asset_bytes + bytes.size() > maximum_total) throw std::runtime_error("备份资产总量超过上限");
            report.asset_bytes += bytes.size(); writeFile(staging / relative, bytes);
        }
        /* 清单列出的文件复制完才重命名发布；无额外 SQLite 完整性检查、持久化屏障或系统凭据恢复。 */
        if (!destination_directory.parent_path().empty()) std::filesystem::create_directories(destination_directory.parent_path());
        std::filesystem::rename(staging, destination_directory);
        return Result<BackupReport>::success(std::move(report));
    } catch (const std::exception& exception) {
        /* 尽力删除暂存树并忽略清理错误；父目录和已发布目标不在回滚范围，不能承诺失败没有文件残留。 */
        std::error_code ignored; std::filesystem::remove_all(staging, ignored);
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

} // namespace xuyan::application
