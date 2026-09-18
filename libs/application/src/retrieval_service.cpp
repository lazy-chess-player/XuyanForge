#include "xuyan/application/retrieval_service.h"

#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

RetrievalService::RetrievalService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> RetrievalService::saveScope(
    const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).saveEntityRetrievalScope(
        command_id, std::move(scope), expected_revision); }
    catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::EntityRetrievalScope>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> RetrievalService::retrieve(
    xuyan::domain::RetrievalRequest request) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).retrieveEntities(std::move(request)); }
    catch (const std::exception& exception) { return xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
