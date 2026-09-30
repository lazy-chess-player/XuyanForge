#pragma once

#include <string>

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

} // namespace xuyan::domain
