#include "xuyan/application/world_catalog_service.h"

#include "xuyan/storage/workspace_repository.h"

#include <utility>

namespace xuyan::application {

WorldCatalogService::WorldCatalogService(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

xuyan::domain::Result<std::vector<xuyan::domain::WorldTemplate>> WorldCatalogService::list() {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).listWorldTemplates();
    } catch (...) {
        return xuyan::domain::Result<std::vector<xuyan::domain::WorldTemplate>>::failure(
            {xuyan::domain::ErrorCode::storage_error, "无法读取世界目录，详情已隐藏", true, "检查工作区后重试"});
    }
}

xuyan::domain::Result<xuyan::domain::WorldTemplate> WorldCatalogService::create(
    const std::string& world_id, const std::string& name) {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).createWorldTemplate(world_id, name);
    } catch (...) {
        return xuyan::domain::Result<xuyan::domain::WorldTemplate>::failure(
            {xuyan::domain::ErrorCode::storage_error, "无法创建世界模板，详情已隐藏", true, "检查工作区后重试"});
    }
}

xuyan::domain::Result<xuyan::domain::WorldTemplate> WorldCatalogService::attachSource(
    const std::string& world_id, const std::string& source_id) {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).attachWorldSource(world_id, source_id);
    } catch (...) {
        return xuyan::domain::Result<xuyan::domain::WorldTemplate>::failure(
            {xuyan::domain::ErrorCode::storage_error, "无法关联世界来源，详情已隐藏", true, "检查世界与来源后重试"});
    }
}

xuyan::domain::Result<bool> WorldCatalogService::initialize() {
    try {
        // 构造完成迁移后再检查核心表；版本号本身不能证明所有资料表仍存在。
        xuyan::storage::WorkspaceRepository repository(database_path_);
        return repository.validateCoreSchema();
    } catch (...) {
        return xuyan::domain::Result<bool>::failure(
            {xuyan::domain::ErrorCode::storage_error, "工作区数据库初始化失败，详情已隐藏", true,
             "检查工作区路径、访问权限及数据库版本"});
    }
}

} // namespace xuyan::application
