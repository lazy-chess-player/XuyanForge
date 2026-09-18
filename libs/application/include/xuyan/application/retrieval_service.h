#pragma once

#include "xuyan/domain/retrieval.h"

#include <filesystem>

namespace xuyan::application {

class RetrievalService {
public:
    explicit RetrievalService(std::filesystem::path database_path);
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieve(
        xuyan::domain::RetrievalRequest request);
private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
