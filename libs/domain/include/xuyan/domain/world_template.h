#pragma once

#include "xuyan/domain/world_entity.h"

#include <string>
#include <vector>

namespace xuyan::domain {

/* 职责：跨界面、应用与存储层传递世界目录的轻量值快照，不携带小说正文。
 * 生命周期：独立拥有字符串，不借用数据库连接；由目录服务读取或显式创建路径返回。
 * 约束：默认字段均为空，不预置世界；名称与稳定标识分离，不按名称自动合并。
 */
struct WorldTemplate {
    // 世界稳定标识，默认空；创建调用者填写，服务/仓储用于关联与隔离。
    std::string id;
    // 用户输入的显示名称，默认空；不由测试素材或安装过程生成。
    std::string name;
    // 关联小说来源稳定标识，默认空表示未关联；显式导入和绑定路径更新。
    std::string source_id;
};

/* 职责：向资料包服务传递一个明确世界的目录与当前未删除条目，二者来自同一只读快照。
 * 生命周期：独立拥有字段，不借用数据库、小说资产或线程；读取完成后交调用方持有。
 * 边界：仅为world-v1条目包载荷，不包含证据、历史版本、图谱或小说，不冒称完整工作区备份。
 */
struct WorldExportSnapshot {
    /* 实际选定的世界目录元数据，默认空；仓储按稳定标识读取，导出服务用名称补齐未知标题。 */
    WorldTemplate world;
    /* 同快照内该世界当前未删除的条目，默认空；按稳定标识排序，最多100000条。
     * 仓储写入、包服务读取；原始文本字段累计不超过32MiB，编码后的上限由包服务另行检查。 */
    std::vector<WorldEntity> entities;
};

} // namespace xuyan::domain
