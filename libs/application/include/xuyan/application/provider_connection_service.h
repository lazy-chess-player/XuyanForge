#pragma once

#include "xuyan/application/credential_store.h"
#include "xuyan/domain/provider_connection.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

/* 连接元数据与凭据协调入口；拥有路径、借用凭据端口。两种存储不共享事务，失败采用显式补偿。 */
class ProviderConnectionService {
public:
    /*
     * 功能：绑定元数据和凭据设施。参数：database_path 为按值路径；credentials 为非拥有适配器引用，必须长于服务及所有调用。
     * 返回：完成绑定。失败：路径分配异常传播，构造不读秘密。
     * 副作用：无存储访问；线程：同步，不启动任务，调用方满足凭据适配器线程约束。
     */
    ProviderConnectionService(std::filesystem::path database_path, ICredentialStore& credentials);

    /*
     * 功能：查询连接元数据。参数：无。
     * 返回：连接列表，空库成功；不含秘密值。失败：数据库错误返回 Result。
     * 副作用：只读连接，不读取凭据；线程：调用线程同步完成。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> list();
    /*
     * 功能：读取模型连接当前元数据，供调用方校验修订、启用状态和数据策略。
     * 参数：connection_id 为连接稳定标识，调用期间借用，不按显示名称匹配。
     * 返回：不含秘密值的连接值对象；凭据引用仅用于端口定位，不能用作秘密显示。
     * 失败：连接不存在或仓储错误经 Result 传播，打开异常转中文存储错误，不泄露异常详情。
     * 副作用：只读连接元数据，不读取凭据或发送探测请求；打开数据库可能迁移结构。
     * 线程与生命周期：调用线程同步完成，不保存参数引用或数据库连接。
     */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> load(const std::string& connection_id);
    /*
     * 功能：校验连接后保存元数据；提供新秘密时先保存凭据，数据库失败执行补偿。
     * 参数：command_id 为非空幂等命令；connection 为完整连接值，空 ID 从命令生成、凭据引用由服务重建；
     * expected_revision 为元数据当前修订；new_secret 为空可选值表示不修改凭据，有值必须非空且最多 65536 字节。
     * 返回：已保存连接。失败：字段/修订/凭据/数据库错误返回 Result；补偿失败使用独立的凭据一致性错误分类，要求人工核对，不能保证两种存储整体原子性。
     * 副作用：写系统凭据和数据库，局部秘密副本退出时尽力清除，不写秘密到日志/元数据。
     * 线程与生命周期：同步且无内部锁；同一连接的凭据修改须由调用方串行化，端口须存活到返回。
     */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> save(
        const std::string& command_id, xuyan::domain::ProviderConnection connection,
        int expected_revision, std::optional<std::string> new_secret);
    /*
     * 功能：检查连接的凭据是否存在且非空。参数：connection_id 为连接标识。
     * 返回：true 有可用秘密；false 为凭据缺失或空串。失败：连接缺失、其他凭据设施错误或数据库错误返回 Result。
     * 副作用：短暂读取秘密后清除局部副本，不返回秘密；线程：同步，遵循 credentials 的线程约束。
     */
    xuyan::domain::Result<bool> hasCredential(const std::string& connection_id);
    /*
     * 功能：先软删除连接，再删除其系统凭据。
     * 参数：command_id 为幂等命令；connection_id 为目标连接；expected_revision 为当前元数据修订。
     * 返回：成功时 true。失败：修订/数据库/凭据删除错误返回 Result，凭据删除失败时元数据可能已经删除。
     * 副作用：两种存储分别变更，不承诺跨存储回滚；线程：同步，调用方串行同连接操作，不自动停止在途请求。
     */
    xuyan::domain::Result<bool> remove(const std::string& command_id,
                                       const std::string& connection_id, int expected_revision);

private:
    /* 连接元数据数据库路径；构造后只读，与服务同寿命，不持有 SQL 事务。 */
    std::filesystem::path database_path_;
    /* 非拥有凭据端口；构造绑定，保存/检查/删除调用使用，适配器必须长于本服务及同步调用。 */
    ICredentialStore& credentials_;
};

} // namespace xuyan::application
