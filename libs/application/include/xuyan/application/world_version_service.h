#pragma once

#include "xuyan/domain/world_version.h"

#include <filesystem>

namespace xuyan::application {

class WorldVersionService {
public:
    /** @brief 绑定保存不可变世界版本的工作区数据库。 */
    explicit WorldVersionService(std::filesystem::path database_path);
    /** @brief 从当前已确认资料发布新世界版本，并可关联父版本。 */
    xuyan::domain::Result<xuyan::domain::WorldVersion> publish(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id = {});
    /** @brief 列出指定世界的已发布版本。 */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> list(const std::string& world_id);
    /** @brief 按稳定版本 ID 读取不可变版本。 */
    xuyan::domain::Result<xuyan::domain::WorldVersion> load(const std::string& version_id);
    /** @brief 基于已发布版本和故事时间准备历史快照，不把未来资料提前引入。 */
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> prepareSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
