#pragma once

#include "xuyan/domain/source_document.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

class SourceImportService {
public:
    explicit SourceImportService(std::filesystem::path database_path);

    xuyan::domain::Result<xuyan::domain::SourceDocument> importTextFile(
        const std::string& command_id, const std::filesystem::path& source_path,
        const std::string& edition = "1", const std::string& world_id = {});
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> list();
    xuyan::domain::Result<std::string> loadNormalizedText(const std::string& source_id);
    xuyan::domain::Result<std::string> evidenceText(const std::string& source_id,
                                                    std::size_t start_codepoint,
                                                    std::size_t end_codepoint);
    xuyan::domain::Result<xuyan::domain::SourceDocument> saveChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

private:
    xuyan::domain::Result<std::string> readBounded(const std::filesystem::path& path) const;
    xuyan::domain::Result<std::string> storeAsset(const std::string& hash, std::string_view suffix,
                                                 std::string_view bytes) const;

    std::filesystem::path database_path_;
    std::filesystem::path workspace_root_;
};

} // namespace xuyan::application
