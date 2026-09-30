#pragma once

#include "xuyan/domain/world_version.h"

#include <filesystem>

namespace xuyan::application {

/* 不可变世界版本及历史快照入口；仅拥有数据库路径，调用线程使用局部仓储，不持有发布事务。 */
class WorldVersionService {
public:
    /*
     * 功能：绑定世界版本工作区。参数：database_path 为数据库路径，按值持有。
     * 返回：完成初始化。失败：分配异常传播，不校验数据库。
     * 副作用：无磁盘访问；线程：同步构造，不启动任务。
     */
    explicit WorldVersionService(std::filesystem::path database_path);
    /*
     * 功能：冻结当前已确认资料为不可变版本。参数：command_id 为幂等命令；world_id 为目标世界；
     * parent_id 为父版本标识，默认空表示无父版本。
     * 返回：新版本或重放结果。失败：世界/父版本无效、命令或存储错误返回 Result。
     * 副作用：仓储事务写版本、快照及命令，不覆盖旧版本；线程：同步，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> publish(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id = {});
    /*
     * 功能：列出某世界已发布版本。参数：world_id 为世界稳定标识。
     * 返回：版本列表，无版本为成功空列表。失败：数据库错误返回 Result。
     * 副作用：只读版本；线程：调用线程同步完成，结果不借用连接。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> list(const std::string& world_id);
    /*
     * 功能：读取已发布版本。参数：version_id 为版本稳定标识。
     * 返回：版本值对象。失败：缺失版本或存储错误返回 Result。
     * 副作用：只读版本；线程：同步，无共享数据库连接。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> load(const std::string& version_id);
    /*
     * 功能：从发布版本准备时间过滤后的历史快照。参数：command_id 为幂等命令；
     * world_version_id 为基线版本；story_time 为领域故事时间刻度，不是系统时间或章节序号。
     * 返回：持久化快照。失败：版本缺失、时间/命令无效或存储错误返回 Result。
     * 副作用：写新快照和命令，不改变发布版本；线程：调用线程同步事务。
     */
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> prepareSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
private:
    /* 世界版本数据库路径；构造时取得，之后只读，与服务同寿命，不缓存历史快照。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
