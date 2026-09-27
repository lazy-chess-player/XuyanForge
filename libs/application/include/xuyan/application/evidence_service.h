#pragma once

#include "xuyan/domain/evidence.h"

#include <filesystem>
#include <vector>

namespace xuyan::application {

class EvidenceService {
public:
    /** @brief 绑定原文和字段级证据所在的工作区数据库。 */
    explicit EvidenceService(std::filesystem::path database_path);
    /** @brief 逐字校验原文半开码点区间后创建字段证据。 */
    xuyan::domain::Result<xuyan::domain::EvidenceReference> create(
        const std::string& command_id, const std::string& entity_id, const std::string& field_path,
        const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
        const std::string& provenance_type);
    /** @brief 列出指定来源文件的全部证据引用。 */
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listForSource(const std::string& source_id);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
