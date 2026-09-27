#pragma once

#include "xuyan/domain/retrieval.h"

#include <filesystem>

namespace xuyan::application {

class RetrievalService {
public:
    /** @brief 绑定世界资料及其可见范围所在的工作区数据库。 */
    explicit RetrievalService(std::filesystem::path database_path);
    /** @brief 按期望修订保存实体的时间和可见性范围。 */
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    /** @brief 在权限与故事时间过滤后执行中文实体召回。 */
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieve(
        xuyan::domain::RetrievalRequest request);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
