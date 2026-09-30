#pragma once

#include "xuyan/domain/retrieval.h"

#include <filesystem>

namespace xuyan::application {

/* 世界资料召回入口；每次调用独立仓储，在调用线程执行权限及时间过滤，不拥有索引线程。 */
class RetrievalService {
public:
    /*
     * 功能：绑定召回工作区。参数：database_path 为工作区路径，按值持有。
     * 返回：完成初始化。失败：分配异常传播，不提前打开数据库。
     * 副作用：无读写；线程：同步构造，路径与对象同寿命。
     */
    explicit RetrievalService(std::filesystem::path database_path);
    /*
     * 功能：更新实体召回权限及故事时间范围。参数：command_id 为幂等命令；scope 为范围值；
     * expected_revision 为现有范围修订，创建语义及合法范围由仓储校验。
     * 返回：已持久化范围。失败：实体、范围或修订/存储错误返回 Result。
     * 副作用：同事务写范围与命令；线程：同步，不调用模型。
     */
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    /*
     * 功能：先过滤权限/时间再召回资料。参数：request 为查询值，携带世界、角色、时间、文本和结果上限。
     * 返回：命中值列表，无匹配为成功空列表。失败：请求不合法或数据库错误返回 Result。
     * 副作用：只读业务资料，不扩大角色权限；线程：同步，按值请求不保留外部引用。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieve(
        xuyan::domain::RetrievalRequest request);
private:
    /* 召回资料所在数据库路径；构造取得后只读，与服务同寿命，不缓存查询结果。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
