#pragma once

#include "xuyan/domain/evidence.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

/*
 * 职责：从来源的标准化不可变原文建立字段证据，并按来源读取已保存证据。
 * 资源与生命周期：仅拥有数据库路径；仓储、来源服务及原文读取资源在同步调用内使用/释放，结果为自有值。
 * 线程：在调用线程执行，不拥有线程；调用期间对象须存活，销毁不能与调用并发。
 * 存储边界：业务调用打开仓储时可能创建父目录/数据库并迁移结构；查询只读证据业务记录。
 * 异常边界：业务方法 try 内的标准异常转为 storage_error；实参构造和非标准异常可向调用方传播。
 */
class EvidenceService {
public:
    /*
     * 功能：保存证据记录和来源资产所在工作区的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在后续调用时解析，空值不在此校验。
     * 返回：完成路径初始化，不校验数据库或原文资产。
     * 失败：路径构造/分配异常直接传播。
     * 副作用：仅初始化本对象，不保留调用方路径引用。
     */
    explicit EvidenceService(std::filesystem::path database_path);
    /*
     * 功能：核对实体/来源存在，从标准化原文重新读取引文，计算 SHA-256 后创建字段证据。
     * 参数：所有字符串均为输入，仅调用期间借用。
     *   command_id：幂等标识，调用方应提供非空值；其摘要前 20 位生成证据 ID，同标识必须复用相同证据负载。
     *   entity_id：已有且未删除的实体 ID；调用方须保证其与来源业务上相容，当前未校验两者属于同一世界。
     *   field_path：被引文说明的字段路径，1—256 字节；当前仅校验长度，不验证实体中实际存在该字段。
     *   source_id：已有来源 ID，其 normalized_asset_ref 必须指向可读取的工作区原文资产。
     *   start_codepoint：输入区间起点，从标准化原文开头计的零基 Unicode 码点偏移，包含此位置。
     *   end_codepoint：输入区间终点，同为绝对码点偏移，不包含此位置；须大于起点、不超原文长度，区间最多 20000 码点。
     *   provenance_type：来源性质，仅 original_fact、in_text_claim、model_inference、author_setting、simulation_result 合法。
     * 返回：含原文引文、摘要、绝对区间及修订 1 的证据，或同负载命令的已存证据值。
     * 失败：实体读取失败（含仓储错误）或实体已删统一返回 missing_context；来源/原文读取错误继续传播其 Result；
     *   反向/越界区间及原文无效 UTF-8 在读取阶段返回 storage_error；成功读取后的空区间、超过 20000 码点、
     *   字段路径/性质无效为 validation_failed，不同负载重放为 command_conflict，打开/迁移/其他标准异常为 storage_error。
     * 副作用：同步读取标准化资产，同事务写证据和命令；打开可能创建/迁移结构，来源性质不自动改变实体事实级别。
     */
    xuyan::domain::Result<xuyan::domain::EvidenceReference> create(
        const std::string& command_id, const std::string& entity_id, const std::string& field_path,
        const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
        const std::string& provenance_type);
    /*
     * 功能：读取精确匹配来源 ID 的全部字段证据，不分页，也不重新读取原文。
     * 参数：source_id：输入来源稳定 ID，调用期间借用；不预先校验来源存在，空值仍按精确值查询。
     * 返回：按起始码点、证据 ID 排序的自有值列表；无匹配为成功空列表。
     * 失败：数据库打开/迁移或查询的标准异常转为 storage_error，不伪造成功空列表。
     * 副作用：只读证据记录，打开可能创建/迁移结构；不复核资产是否仍可读取。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listForSource(const std::string& source_id);
private:
    /* 证据数据库文件路径，无计量单位；初值为构造实参，无默认实参，空值原样保存。
     * 构造写入，方法只读；拥有路径值至服务销毁，来源资产由局部来源服务按数据库父目录解析。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
