#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

/* 备份/恢复结果值对象；仅包含路径及规模统计，不保存正文、模型输出或秘密。调用线程生成并交调用方持有。 */
struct BackupReport {
    /* 成功目标目录中的数据库路径；默认空，完成重命名后填写，供调用方重新打开工作区。 */
    std::filesystem::path workspace_database;
    /* 本次成功复制的资产文件数，单位个，初始 0，上限 10000，由备份/恢复流程累计。 */
    int asset_count{0};
    /* 本次资产总字节数，不含数据库及清单；初始 0，上限 4 GiB，随结果持有。 */
    std::uintmax_t asset_bytes{0};
};

/* 完整工作区备份入口；拥有数据库路径，同步使用 SQLite 快照与同级暂存目录，不读系统凭据。 */
class BackupService {
public:
    /*
     * 功能：绑定备份源工作区。参数：database_path 为数据库路径，其父目录为资产根。
     * 返回：完成路径初始化。失败：路径分配异常传播，不验证源。
     * 副作用：无读写；线程：同步构造，服务不拥有连接/线程。
     */
    explicit BackupService(std::filesystem::path database_path);
    /*
     * 功能：快照数据库并复制资产，最终发布带哈希清单的备份。
     * 参数：destination_directory 为非空、尚不存在的新目录；不能与其他写入者共用目标。
     * 返回：备份数据库位置及资产统计。失败：目标、权限、文件类型、大小/数量或复制错误返回 Result；路径分配异常可传播。
     * 副作用：创建同级暂存目录、复制工作区资料、重命名为正式目录；失败尽力清理暂存，不读取系统凭据。
     * 线程与生命周期：同步磁盘操作，无取消令牌；调用方确保资产在备份期间稳定，数据库使用在线快照。
     */
    xuyan::domain::Result<BackupReport> create(const std::filesystem::path& destination_directory);
    /*
     * 功能：验证备份后恢复到新目录。参数：backup_directory 为只读备份根；destination_directory 为尚不存在目标。
     * 返回：目标数据库路径及资产统计。失败：清单/版本/凭据标记、路径、哈希、数量/容量或文件错误返回 Result。
     * 副作用：写暂存副本，全部验证后重命名；失败尽力清理暂存，不覆盖现有工作区、不恢复系统凭据。
     * 线程与生命周期：同步执行，不启动线程或支持中途取消；调用方须保持备份源稳定且独占目标。
     */
    static xuyan::domain::Result<BackupReport> restore(
        const std::filesystem::path& backup_directory, const std::filesystem::path& destination_directory);
private:
    /* 备份源数据库路径；构造后只读，与服务同寿命，资产目录由其父目录推导。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
