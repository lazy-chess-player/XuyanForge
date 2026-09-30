#pragma once

#include "xuyan/domain/world_template.h"
#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

/*
 * 世界目录用例入口，供界面及其他调用方通过应用层访问模板元数据。
 * 仅拥有数据库路径，每次同步调用建立并关闭独立仓储，不拥有 GUI、线程、数据库连接或业务样例。
 * 调用方负责同一工作区的操作协调，返回值不借用仓储资源。
 */
class WorldCatalogService {
public:
    /*
     * 功能：绑定世界目录工作区，不打开连接或创建业务资料。
     * 参数：database_path 为数据库文件路径，按值取得并持有，构造时不校验可访问性。
     * 返回：完成路径初始化。失败：路径复制/分配异常可传播。
     * 副作用：无磁盘、凭据或网络访问；线程与生命周期：调用线程同步构造，不启动任务。
     */
    explicit WorldCatalogService(std::filesystem::path database_path);

    /*
     * 功能：读取工作区已有世界模板目录。
     * 参数：无。返回：模板值列表，全新工作区成功返回空列表，不补默认世界。
     * 失败：仓储查询错误经 Result 传播，打开/迁移异常转为中文存储错误，不暴露异常详情。
     * 副作用：只读业务资料；打开数据库可能创建文件及迁移结构。
     * 线程与生命周期：调用线程同步完成，连接于返回前释放，结果由调用方持有。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldTemplate>> list();

    /*
     * 功能：按调用方明确提供的身份和名称创建空白世界模板。
     * 参数：world_id 为非空稳定世界标识；name 为含 1—120 个 Unicode 码点的合法 UTF-8 名称，重复身份由仓储校验。
     * 返回：创建后的模板值，不附带来源、人物或小说。
     * 失败：字段/身份冲突及仓储错误经 Result 传播；打开/迁移异常转中文存储错误，详情隐藏。
     * 副作用：在仓储事务内写模板元数据；不会推导默认身份或联网。
     * 线程与生命周期：调用线程同步执行，参数只借用至返回，连接不跨调用共享。
     */
    xuyan::domain::Result<xuyan::domain::WorldTemplate> create(
        const std::string& world_id, const std::string& name);

    /*
     * 功能：把已导入来源关联到明确选择的世界模板。
     * 参数：world_id 为目标世界标识；source_id 为已有来源标识，均非空；仓储校验来源属于目标世界，重复绑定同一来源可成功返回同一关联。
     * 返回：关联后的模板元数据。
     * 失败：世界/来源缺失、范围不匹配或仓储错误经 Result 传播，打开/迁移异常转中文存储错误。
     * 副作用：持久化模板来源关联，不重写原文、不发送小说。
     * 线程与生命周期：调用线程同步执行，参数只借用至返回，不保留数据库连接。
     */
    xuyan::domain::Result<xuyan::domain::WorldTemplate> attachSource(
        const std::string& world_id, const std::string& source_id);

    /*
     * 功能：确认工作区数据库能打开、完成支持的结构迁移，且核心资料表齐全。
     * 参数：无。返回：成功值 true 表示连接打开/迁移成功，不表示已创建世界或其他业务资料。
     * 失败：打开、迁移异常或核心表缺失返回中文存储错误，禁止把失败伪装成成功 false。
     * 副作用：可能创建数据库文件和迁移结构，不创建模板、来源、人物或测试资料，不联网。
     * 线程与生命周期：调用线程同步执行，临时连接在返回前关闭；服务只继续持有路径。
     */
    xuyan::domain::Result<bool> initialize();

private:
    /* 工作区数据库路径；构造时拥有，之后只读，与服务同寿命，不持有活跃连接。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
