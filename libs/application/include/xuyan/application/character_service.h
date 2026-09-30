#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

/* 人物卡版本管理入口，仅持有路径；同步创建局部仓储，不拥有线程，不将实例记忆写回卡片。 */
class CharacterService {
public:
    /*
     * 功能：绑定人物卡工作区。参数：database_path 为数据库路径，按值持有。
     * 返回：完成路径初始化。失败：分配异常传播，不在构造时校验路径。
     * 副作用：无磁盘访问；线程与生命周期：同步构造，服务不拥有数据库连接。
     */
    explicit CharacterService(std::filesystem::path database_path);

    /*
     * 功能：打开工作区并查询人物卡。参数：无。
     * 返回：卡片列表，无卡片为成功空列表。失败：仓储错误返回 Result，构造异常可传播。
     * 副作用：打开数据库可能初始化结构，不创建卡片；线程：调用线程同步查询。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> openAndList();
    /*
     * 功能：创建可移植人物卡的首个版本。参数：command_id 为非空幂等命令；blueprint 为完整卡片值，按值转交。
     * 返回：已保存卡片或重放结果。失败：字段、身份或命令冲突返回 Result，仓储构造异常可传播。
     * 副作用：同事务写入卡片版本与命令记录；线程：同步，不触发模型生成。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> create(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    /*
     * 功能：保留旧版并新增卡片版本。参数：command_id 为幂等命令；blueprint 为编辑后的卡片；
     * expected_version 为调用方读到的现有版本号，须与最新版本匹配。
     * 返回：新版本卡片或重放结果。失败：字段、版本冲突、存储错误返回 Result，构造异常可传播。
     * 副作用：写版本及命令记录，不改变已创建的人物实例；线程：同步事务。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> save(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    /*
     * 功能：读取人物卡指定版本。参数：blueprint_id 为卡片稳定标识；version 为版本号，默认 -1 读取最新。
     * 返回：独立卡片值。失败：卡片/版本缺失或存储错误返回 Result，仓储构造异常可传播。
     * 副作用：只读卡片，打开数据库可能初始化结构；线程：同步，结果不借用数据库资源。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> load(
        const std::string& blueprint_id, int version = -1);
    /*
     * 功能：查询当前工作区人物卡列表。参数：无。
     * 返回：卡片列表，空库成功。失败：仓储错误返回 Result，仓储构造异常可传播。
     * 副作用：只读卡片，打开数据库可能初始化结构；线程：调用线程同步完成。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> list();

private:
    /* 卡片数据库路径；构造时持有、其后只读，与服务同寿命，每次调用独立打开仓储。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
