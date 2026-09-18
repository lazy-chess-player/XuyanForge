#pragma once

#include "xuyan/domain/world_version.h"

#include <filesystem>

namespace xuyan::application {

class WorldVersionService {
public:
    explicit WorldVersionService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::WorldVersion> publish(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id = {});
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> list(const std::string& world_id);
    xuyan::domain::Result<xuyan::domain::WorldVersion> load(const std::string& version_id);
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> prepareSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
