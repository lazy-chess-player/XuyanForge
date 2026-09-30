#pragma once

#include "xuyan/domain/world_entity.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

/*
 * 工作区条目用例入口，仅保存数据库路径，每次调用在调用线程创建独立仓储。
 * 不拥有线程或共享连接；对象可复用，调用方负责写入顺序及命令、修订的一致性。
 */
class WorkspaceService {
public:
    /*
     * 功能：绑定世界条目所在的工作区。参数：database_path 为数据库路径，按值持有。
     * 返回：完成路径初始化。失败：路径分配异常向外传播，构造时不验证数据库。
     * 副作用：无文件操作；线程与生命周期：同步构造，不创建连接或后台任务。
     */
    explicit WorkspaceService(std::filesystem::path database_path);

    /*
     * 功能：查询全部类型的第一页。参数：limit 为页容量，默认 50，合法范围由仓储校验。
     * 返回：条目及总数，空工作区返回成功空页。失败：参数或存储错误经 Result 返回，仓储构造异常可传播。
     * 副作用：打开数据库可能初始化结构，不生成资料；线程：调用线程同步完成。
     */
    xuyan::domain::Result<xuyan::domain::EntityPage> openAndList(int limit = 50);
    /*
     * 功能：分页检索当前工作区条目。参数：query 为查询文本，空串不限名称；kind 为内部类型，空串不限类型；
     * offset 为从零起的非负条目偏移，默认 0；limit 为页容量，默认 50，范围由仓储校验。
     * 返回：匹配页及总数，无匹配仍成功。失败：查询参数和数据库错误返回 Result，构造异常可传播。
     * 副作用：仅查询业务资料，打开数据库可能初始化结构；线程：同步，不保存参数引用。
     */
    xuyan::domain::Result<xuyan::domain::EntityPage> search(
        const std::string& query, const std::string& kind = {}, int offset = 0, int limit = 50);
    /*
     * 功能：以幂等命令创建条目。参数：command_id 为非空命令标识；entity 为待创建的完整值对象，按值移交仓储。
     * 返回：持久化条目或命令重放结果。失败：字段、身份、命令冲突或存储错误返回 Result，构造异常可传播。
     * 副作用：原子写入条目与命令记录；线程：调用线程同步事务，无后台任务。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> create(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    /*
     * 功能：保存条目编辑并防止覆盖并发修改。参数：command_id 为非空幂等命令；entity 为完整编辑值；
     * expected_revision 为调用方读到的当前修订，须与仓储匹配。
     * 返回：新修订条目或重放结果。失败：校验、修订冲突及存储错误返回 Result，构造异常可传播。
     * 副作用：同事务更新条目和命令记录；线程：调用线程同步完成。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> save(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    /*
     * 功能：读取最新条目。参数：entity_id 为稳定条目标识。
     * 返回：条目值对象。失败：不存在或存储错误返回 Result，仓储构造异常可传播。
     * 副作用：只读业务资料，打开数据库可能初始化结构；线程：同步，结果不借用仓储资源。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> load(const std::string& entity_id);
    /*
     * 功能：按修订软删除条目。参数：command_id 为非空幂等命令；entity_id 为待删除标识；
     * expected_revision 为当前条目修订。
     * 返回：删除后的条目或重放结果。失败：缺失条目、修订或存储错误返回 Result，构造异常可传播。
     * 副作用：保留历史并写删除状态和命令记录；线程：同步事务，无文件删除。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> remove(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    /*
     * 功能：按作者选择合并同世界同类条目，专用本体冲突仍须校对。
     * 参数：command_id 为幂等命令；source_id、target_id 分别为来源与保留目标的稳定标识，不能相同；
     * source_expected_revision、target_expected_revision 为两侧当前修订。
     * 返回：合并记录及条目结果。失败：范围、类型、引用、两侧修订或存储错误返回 Result，构造异常可传播。
     * 副作用：证据、引用端点、合并记录与条目变更在仓储同事务提交；线程：同步，不自动选目标。
     */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> merge(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    /*
     * 功能：利用合并记录恢复条目及可追溯引用。
     * 参数：command_id 为幂等命令；merge_id 为既有合并记录；source_expected_revision、target_expected_revision
     * 为两侧当前修订，默认 -1 使用仓储兼容校验方式，不代表允许覆盖后续编辑。
     * 返回：拆分结果。失败：合并记录缺失、引用已改变、修订或存储冲突返回 Result，构造异常可传播。
     * 副作用：仓储原子恢复可恢复引用并登记命令；线程：同步，不回退不相关编辑。
     */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision = -1, int target_expected_revision = -1);

private:
    /* 工作区数据库路径；构造时取得，之后只读，与服务对象同寿命，不持有数据库连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
