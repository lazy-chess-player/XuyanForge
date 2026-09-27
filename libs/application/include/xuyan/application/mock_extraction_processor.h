#pragma once

#include "xuyan/domain/extraction_job.h"

#include <filesystem>

namespace xuyan::application {

class MockExtractionProcessor {
public:
    /** @brief 绑定持久化队列以执行完全离线的合成提取。 */
    explicit MockExtractionProcessor(std::filesystem::path database_path);
    /** @brief 在不访问网络的情况下处理一个待执行切片。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
    /** @brief 至多处理指定数量的离线步骤，并返回最新任务状态。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processAll(const std::string& job_id, int maximum_steps = 10000);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
