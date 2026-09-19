#pragma once

#include "xuyan/domain/world_entity.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

class WorkspaceService {
public:
    explicit WorkspaceService(std::filesystem::path database_path);

    xuyan::domain::Result<xuyan::domain::EntityPage> openAndList(int limit = 50);
    xuyan::domain::Result<bool> installTestFixture();
    xuyan::domain::Result<xuyan::domain::EntityPage> search(
        const std::string& query, const std::string& kind = {}, int offset = 0, int limit = 50);
    xuyan::domain::Result<xuyan::domain::WorldEntity> create(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    xuyan::domain::Result<xuyan::domain::WorldEntity> save(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    xuyan::domain::Result<xuyan::domain::WorldEntity> load(const std::string& entity_id);
    xuyan::domain::Result<xuyan::domain::WorldEntity> remove(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> merge(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision = -1, int target_expected_revision = -1);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
