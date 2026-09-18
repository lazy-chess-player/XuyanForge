#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

struct PackageReport {
    std::string package_id;
    std::string kind;
    std::filesystem::path path;
    int entity_count{0};
};

class PackageService {
public:
    explicit PackageService(std::filesystem::path database_path);

    xuyan::domain::Result<PackageReport> exportWorld(const std::filesystem::path& destination,
                                                      const std::string& title,
                                                      const std::string& author);
    xuyan::domain::Result<PackageReport> importWorld(const std::string& command_id,
                                                      const std::filesystem::path& source);
    xuyan::domain::Result<PackageReport> exportCharacter(const std::string& blueprint_id,
                                                          const std::filesystem::path& destination,
                                                          const std::string& author,
                                                          bool include_private_notes = true);
    xuyan::domain::Result<PackageReport> importCharacter(const std::string& command_id,
                                                          const std::filesystem::path& source);

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
