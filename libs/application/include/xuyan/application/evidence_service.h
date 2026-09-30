#pragma once

#include "xuyan/domain/evidence.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

/* 字段证据用例入口；持有工作区路径，同步读取不可变原文并转交仓储，不拥有线程或文件句柄。 */
class EvidenceService {
public:
    /*
     * 功能：绑定证据工作区。参数：database_path 为工作区数据库路径，按值保存。
     * 返回：完成路径初始化。失败：分配异常传播，构造不校验资产。
     * 副作用：无数据库或文件访问；线程：同步构造，不启动任务。
     */
    explicit EvidenceService(std::filesystem::path database_path);
    /*
     * 功能：从不可变原文截取并建立字段证据。
     * 参数：command_id 为幂等命令；entity_id 为被说明的实体；field_path 为实体字段路径；source_id 为来源标识；
     * start_codepoint、end_codepoint 为原文绝对 Unicode 码点半开区间，须有序且不越界；
     * provenance_type 为来源性质内部值，不因创建证据自动升级为事实。
     * 返回：含引文、哈希与位置的证据。失败：原文不可读、区间/实体/性质无效或存储错误返回 Result。
     * 副作用：读取来源资产后由仓储持久化证据与命令；线程：同步，未启动后台读写。
     */
    xuyan::domain::Result<xuyan::domain::EvidenceReference> create(
        const std::string& command_id, const std::string& entity_id, const std::string& field_path,
        const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
        const std::string& provenance_type);
    /*
     * 功能：查询某来源的字段证据。参数：source_id 为来源稳定标识。
     * 返回：证据列表，无记录为成功空列表。失败：存储错误返回 Result。
     * 副作用：只读证据；线程：同步创建局部仓储，结果为自有值。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listForSource(const std::string& source_id);
private:
    /* 工作区数据库路径；构造后只读，来源文件相对路径由同工作区解析，与服务同寿命。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
