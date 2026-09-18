#include "xuyan/application/workspace_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

WorkspaceService::WorkspaceService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<bool> WorkspaceService::ensureGreyHarborEntities() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto page = repository.searchEntities({}, {}, 0, 1);
    if (!page.ok()) return xuyan::domain::Result<bool>::failure(*page.error);
    if (page.value->total > 0) return xuyan::domain::Result<bool>::success(false);

    const std::vector<xuyan::domain::WorldEntity> samples{
        {"entity-xucheng", "world-grey-harbor", "character", "许澄", {"许代表"}, {"城卫署", "谈判"},
         "城卫署谈判代表，重视秩序；在场景开始时知道北门今夜封闭。", "{\"role\":\"negotiator\"}", "accepted"},
        {"entity-shentang", "world-grey-harbor", "character", "沈棠", {"沈代表"}, {"盐运商会", "谈判"},
         "盐运商会代表，重视交易信誉；担心暴雨造成货物滞留。", "{\"role\":\"merchant\"}", "accepted"},
        {"entity-seal", "world-grey-harbor", "item", "议和印章", {"印章"}, {"唯一物品", "议和"},
         "谈判凭证，初始由沈棠持有。检查后可能发现伪造迹象。", "{\"unique\":true}", "accepted"},
        {"entity-grey-harbor", "world-grey-harbor", "location", "灰港", {}, {"港口", "暴雨"},
         "城卫署与盐运商会准备谈判的港城；暴雨期间渡口停航。", "{\"weather\":\"storm\"}", "accepted"},
        {"entity-no-teleport", "world-grey-harbor", "rule", "禁止瞬间移动", {}, {"硬约束"},
         "当前世界不存在可用的瞬间移动方式。", "{\"severity\":\"hard\"}", "accepted"},
    };
    for (const auto& sample : samples) {
        auto created = repository.createEntity("seed-" + sample.id, sample);
        if (!created.ok()) return xuyan::domain::Result<bool>::failure(*created.error);
    }
    return xuyan::domain::Result<bool>::success(true);
}

xuyan::domain::Result<xuyan::domain::EntityPage> WorkspaceService::openAndList(int limit) {
    auto seeded = ensureGreyHarborEntities();
    if (!seeded.ok()) return xuyan::domain::Result<xuyan::domain::EntityPage>::failure(*seeded.error);
    return search({}, {}, 0, limit);
}

xuyan::domain::Result<xuyan::domain::EntityPage> WorkspaceService::search(
    const std::string& query, const std::string& kind, int offset, int limit) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.searchEntities(query, kind, offset, limit);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::create(
    const std::string& command_id, xuyan::domain::WorldEntity entity) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.createEntity(command_id, std::move(entity));
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::save(
    const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.saveEntity(command_id, std::move(entity), expected_revision);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::load(const std::string& entity_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.loadEntity(entity_id);
}

xuyan::domain::Result<xuyan::domain::WorldEntity> WorkspaceService::remove(
    const std::string& command_id, const std::string& entity_id, int expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.deleteEntity(command_id, entity_id, expected_revision);
}

xuyan::domain::Result<xuyan::domain::EntityMergeResult> WorkspaceService::merge(
    const std::string& command_id, const std::string& source_id, int source_expected_revision,
    const std::string& target_id, int target_expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.mergeEntities(command_id, source_id, source_expected_revision, target_id, target_expected_revision);
}

xuyan::domain::Result<xuyan::domain::EntityMergeResult> WorkspaceService::splitMerge(
    const std::string& command_id, const std::string& merge_id,
    int source_expected_revision, int target_expected_revision) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.splitEntityMerge(command_id, merge_id, source_expected_revision, target_expected_revision);
}

} // namespace xuyan::application
