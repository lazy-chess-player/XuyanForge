#pragma once

#include "xuyan/domain/branch_outcome.h"

#include <filesystem>

namespace xuyan::application {

class BranchOutcomeService {
public:
    explicit BranchOutcomeService(std::filesystem::path database_path);

    xuyan::domain::Result<xuyan::domain::BranchComparison> compare(
        const std::string& left_branch_id, const std::string& right_branch_id);
    xuyan::domain::Result<std::string> exportBranch(
        const std::string& branch_id, const std::filesystem::path& destination,
        const std::string& format, bool include_technical_log);
    xuyan::domain::Result<std::string> exportDiagnostics(const std::filesystem::path& destination);
    xuyan::domain::Result<xuyan::domain::WorldVersion> adoptAsWorldVersion(
        const std::string& command_id, const std::string& branch_id,
        const std::string& world_id, const std::string& title);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
