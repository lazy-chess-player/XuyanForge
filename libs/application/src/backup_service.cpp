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

constexpr std::uintmax_t maximum_asset_file = 128ULL * 1024 * 1024;
constexpr std::uintmax_t maximum_total = 4ULL * 1024 * 1024 * 1024;
constexpr int maximum_files = 10000;

/*
 * 功能：把备份或恢复失败包装成带操作建议的存储错误，不附加资产正文。
 * 参数：message 为按值接收的中文原因，移入返回对象；调用方不得传入私密内容。
 * 返回：不建议自动重试的错误值。失败：字符串分配异常可传播。
 * 副作用：仅构造内存对象；线程：同步，不保存输入引用。
 */
Error backupError(std::string message) { return {ErrorCode::storage_error, std::move(message), false, "检查备份路径、完整性和磁盘空间"}; }

/*
 * 功能：先检查文件大小，再以二进制方式读取备份数据库、清单或资产。
 * 参数：path 为借用至返回的文件路径；limit 为允许的最大字节数，默认 128 MiB。
 * 返回：独立拥有的文件字节，零字节文件返回空串。
 * 失败：大小超限、元数据读取或打开失败时抛异常；流中途读错当前实现不单独检查。
 * 副作用：只读文件；线程：同步，文件流于返回时关闭。
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
 * 参数：path 为写入目标；bytes 为借用至返回的字节视图，允许为空。
 * 返回：无。失败：建目录、打开或写入失败时抛异常。
 * 副作用：以截断模式覆盖目标文件；线程：同步，不保留字节视图。
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
 * 参数：destination 为尚不存在的正式目录路径，借用至返回。
 * 返回：由目标文件名和单调时钟计数组成的同级路径；不保证跨进程唯一。
 * 失败：路径或字符串分配异常可传播。副作用：不创建目录；线程：同步，无持久状态。
 */
std::filesystem::path stagingPath(const std::filesystem::path& destination) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() / (destination.filename().string() + ".partial-" + std::to_string(stamp));
}

/*
 * 功能：从已解析清单中读取必需的字符串字段。
 * 参数：object 为借用至返回的 JSON 对象；key 为待查字段名视图，调用期间有效。
 * 返回：字段字符串的独立副本。失败：字段缺失或不是字符串时抛异常，分配异常可传播。
 * 副作用：只读清单；线程：同步，不保留对象或字段名。
 */
std::string requiredString(const JsonValue& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->isString()) throw std::runtime_error("备份清单字段缺失或类型错误");
    return value->string();
}

/*
 * 功能：核对清单资产路径仅指向 assets 目录下的规范相对条目，防止恢复越界。
 * 参数：value 为路径字符串，借用至返回；空值、绝对路径、反斜杠及点分量均无效。
 * 返回：无；校验通过时不改写路径。失败：非法路径抛异常，路径解析异常可传播。
 * 副作用：纯内存校验，不访问文件；线程：同步。
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
    const auto staging = stagingPath(destination_directory);
    try {
        if (destination_directory.empty() || std::filesystem::exists(destination_directory))
            return Result<BackupReport>::failure(backupError("备份目标必须是尚不存在的新目录"));
        std::filesystem::create_directories(staging);
        const auto database_target = staging / "workspace.sqlite";
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto database_backup = repository.backupTo(database_target);
        if (!database_backup.ok()) throw std::runtime_error(database_backup.error->message);

        // 数据库先通过 SQLite 在线快照写入暂存目录，再逐个复制并哈希资产。
        JsonValue::Array assets;
        BackupReport report{database_target};
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
        const auto database_bytes = readFile(database_target, 2ULL * 1024 * 1024 * 1024);
        JsonValue manifest(JsonValue::Object{
            {"format", "xuyan-backup"}, {"version", 1},
            {"database", JsonValue::Object{{"path", "workspace.sqlite"}, {"sha256", xuyan::domain::sha256(database_bytes)},
                                            {"size", static_cast<std::int64_t>(database_bytes.size())}}},
            {"assets", std::move(assets)}, {"credentials_included", false}});
        // 清单最后写入；整套文件准备好后才把暂存目录重命名为正式备份。
        writeFile(staging / "manifest.json", xuyan::package::writeJson(manifest));
        if (!destination_directory.parent_path().empty()) std::filesystem::create_directories(destination_directory.parent_path());
        std::filesystem::rename(staging, destination_directory);
        report.workspace_database = destination_directory / "workspace.sqlite";
        return Result<BackupReport>::success(std::move(report));
    } catch (const std::exception& exception) {
        std::error_code ignored; std::filesystem::remove_all(staging, ignored);
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

Result<BackupReport> BackupService::restore(
    const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory) {
    const auto staging = stagingPath(destination_directory);
    try {
        // 先校验清单格式和凭据标记，恢复过程不接纳凭据文件。
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
        // 数据库与资产均核对摘要后写入暂存目录，正式目标保持未创建。
        const auto database_bytes = readFile(backup_directory / database_path, 2ULL * 1024 * 1024 * 1024);
        if (xuyan::domain::sha256(database_bytes) != requiredString(*database, "sha256"))
            throw std::runtime_error("备份数据库摘要不匹配");
        std::filesystem::create_directories(staging); writeFile(staging / database_path, database_bytes);
        BackupReport report{destination_directory / "workspace.sqlite"};
        std::set<std::string> seen_paths;
        for (const auto& item : assets->array()) {
            if (!item.isObject()) throw std::runtime_error("备份资产条目无效");
            const auto relative = requiredString(item, "path"); validateRelativeAssetPath(relative);
            if (!seen_paths.insert(relative).second) throw std::runtime_error("备份清单包含重复资产路径");
            if (std::filesystem::is_symlink(backup_directory / relative)) throw std::runtime_error("备份资产不能是符号链接");
            if (++report.asset_count > maximum_files) throw std::runtime_error("备份资产文件数量超过上限");
            const auto bytes = readFile(backup_directory / relative);
            if (xuyan::domain::sha256(bytes) != requiredString(item, "sha256")) throw std::runtime_error("备份资产摘要不匹配");
            if (report.asset_bytes + bytes.size() > maximum_total) throw std::runtime_error("备份资产总量超过上限");
            report.asset_bytes += bytes.size(); writeFile(staging / relative, bytes);
        }
        // 所有文件验证通过后才原子切换为用户指定的恢复目录。
        if (!destination_directory.parent_path().empty()) std::filesystem::create_directories(destination_directory.parent_path());
        std::filesystem::rename(staging, destination_directory);
        return Result<BackupReport>::success(std::move(report));
    } catch (const std::exception& exception) {
        std::error_code ignored; std::filesystem::remove_all(staging, ignored);
        return Result<BackupReport>::failure(backupError(exception.what()));
    }
}

} // namespace xuyan::application
