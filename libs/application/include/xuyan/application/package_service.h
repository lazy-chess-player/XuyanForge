#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

/* 导入/导出报告值对象；拥有包身份、协议种类、路径及条目统计，不拥有归档句柄。 */
struct PackageReport {
    /* 清单中的包稳定标识；默认空，导出由内容摘要生成、导入由已校验清单取得。 */
    std::string package_id;
    /* 内部包类型 world/character；默认空，仅用于协议分派，界面须映射中文标签。 */
    std::string kind;
    /* 本次输入或输出包路径；默认空，由服务填入，随报告持有，不暗示路径仍存在。 */
    std::filesystem::path path;
    /* 本次世界条目数或人物卡版本数，单位条/版；初始 0，由导入/导出成功结果填写。 */
    int entity_count{0};
};

/* 可移植资料包入口；持有工作区路径，同步读取有界 ZIP 并交仓储，包数据始终按不可信输入校验。 */
class PackageService {
public:
    /*
     * 功能：绑定资料包工作区。参数：database_path 为数据库路径，按值保存。
     * 返回：路径初始化完成。失败：分配异常传播，不验证数据库。
     * 副作用：无磁盘访问；线程：同步构造，不拥有线程或打开归档。
     */
    explicit PackageService(std::filesystem::path database_path);

    /*
     * 功能：将明确选定世界的当前未删除条目导出为world-v1包，不回退首个世界。
     * 参数：world_id为借用的非空世界稳定标识，必须存在于目录；destination为借用的非空ZIP路径；
     *   title为借用的包标题，非空原样保留、空值取该世界实际名称；author为借用作者元数据，空值保持未知。
     * 返回：包标识、类型、路径及条目数；空世界可成功导出0条，不夹带其他世界。
     * 失败：空目标/标识、目录缺失、100000条/32MiB载荷上限、数据库或ZIP失败返回Result；仓储构造异常可传播。
     * 副作用：同一只读快照读取该世界目录及条目，释放快照后编码并写包；校验失败不改目标。
     *   不单独打包来源资产/证据/专用图谱/历史版本/系统凭据，条目字段可能含用户资料，不等同完整备份。
     * 线程：调用线程同步执行；输入只借用至返回，调用方须独占输出路径；不跨写包持有数据库事务。
     */
    xuyan::domain::Result<PackageReport> exportWorld(const std::string& world_id,
                                                      const std::filesystem::path& destination,
                                                      const std::string& title,
                                                      const std::string& author);
    /*
     * 功能：校验 ZIP、格式、清单所列摘要及实体载荷后导入世界条目。
     * 参数：command_id 为非空幂等命令；source 为包路径，只读，不提取到任意外部路径。
     * 返回：包报告及导入条目数。失败：ZIP/清单/实体/幂等冲突或存储错误返回 Result；仓储构造异常可传播。
     * 副作用：所有实体交仓储一次提交，不发网络请求；线程：同步，输入载荷持有至提交结束。
     */
    xuyan::domain::Result<PackageReport> importWorld(const std::string& command_id,
                                                      const std::filesystem::path& source);
    /*
     * 功能：导出人物卡从首版到当前版的全部版本。
     * 参数：blueprint_id 为卡片；destination 为输出 ZIP；author 为作者元数据；
     * include_private_notes 默认 true，包含私人备注，false 时各版本备注写空串。
     * 返回：包报告，计数为版本数。失败：版本缺失、数据库或写包错误返回 Result，仓储构造异常可传播。
     * 副作用：读取卡片并写文件，调用方决定私人备注披露；线程：同步，须独占目标。
     */
    xuyan::domain::Result<PackageReport> exportCharacter(const std::string& blueprint_id,
                                                          const std::filesystem::path& destination,
                                                          const std::string& author,
                                                          bool include_private_notes = true);
    /*
     * 功能：验证人物卡包并一次导入版本集合。
     * 参数：command_id 为幂等命令；source 为只读包路径。
     * 返回：包报告及导入版本数。失败：ZIP/清单/摘要/版本字段错误或命令、存储冲突返回 Result，构造异常可传播。
     * 副作用：仓储原子提交已校验版本及命令，不读取系统凭据；线程：调用线程同步完成。
     */
    xuyan::domain::Result<PackageReport> importCharacter(const std::string& command_id,
                                                          const std::filesystem::path& source);

private:
    /* 导入/导出资料所在工作区路径；构造后只读，与服务同寿命，无常驻归档或数据库句柄。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
