#include "xuyan/application/source_import_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"
#include "xuyan/application/source_text_decoder.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace xuyan::application {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

Error fileError(std::string message, std::string action = "检查文件路径与访问权限") {
    return Error{ErrorCode::validation_failed, std::move(message), false, std::move(action)};
}

std::size_t byteOffsetForCodepoint(std::string_view utf8, std::size_t wanted) {
    std::size_t offset = 0; std::size_t count = 0;
    while (offset < utf8.size() && count < wanted) {
        const auto lead = static_cast<unsigned char>(utf8[offset]);
        offset += lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        ++count;
    }
    if (count != wanted) throw std::runtime_error("章节码点边界超出原文");
    return offset;
}

} // namespace

SourceImportService::SourceImportService(std::filesystem::path database_path)
    : database_path_(std::move(database_path)), workspace_root_(database_path_.parent_path()) {}

xuyan::domain::Result<std::string> SourceImportService::readBounded(const std::filesystem::path& path) const {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return xuyan::domain::Result<std::string>::failure(fileError("无法读取来源文件大小"));
    constexpr std::uintmax_t maximum_bytes = 64ULL * 1024 * 1024;
    if (size > maximum_bytes) {
        return xuyan::domain::Result<std::string>::failure(
            fileError("首轮文本导入限制为 64 MiB", "拆分文件或等待长篇分批导入功能"));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return xuyan::domain::Result<std::string>::failure(fileError("无法打开来源文件"));
    std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.good() && !input.eof()) return xuyan::domain::Result<std::string>::failure(fileError("读取来源文件失败"));
    return xuyan::domain::Result<std::string>::success(std::move(content));
}

xuyan::domain::Result<std::string> SourceImportService::storeAsset(const std::string& hash,
                                                                  std::string_view suffix,
                                                                  std::string_view bytes) const {
    try {
        const auto relative = std::filesystem::path("assets") / hash.substr(0, 2) / (hash + std::string(suffix));
        const auto absolute = workspace_root_ / relative;
        std::filesystem::create_directories(absolute.parent_path());
        if (!std::filesystem::exists(absolute)) {
            const auto temporary = absolute.string() + ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("无法创建资产临时文件");
                output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!output) throw std::runtime_error("写入资产临时文件失败");
            }
            std::filesystem::rename(temporary, absolute);
        }
        return xuyan::domain::Result<std::string>::success(relative.generic_string());
    } catch (const std::exception& exception) {
        return xuyan::domain::Result<std::string>::failure(
            Error{ErrorCode::storage_error, exception.what(), true, "检查工作区磁盘空间和权限"});
    }
}

xuyan::domain::Result<xuyan::domain::SourceDocument> SourceImportService::importTextFile(
    const std::string& command_id, const std::filesystem::path& source_path, const std::string& edition,
    const std::string& world_id) {
    if (world_id.empty()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
        fileError("导入前必须选择世界", "先创建或选择世界模板"));
    auto extension = source_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension != ".txt" && extension != ".md" && extension != ".markdown") {
        return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            fileError("首版来源导入仅支持 TXT、Markdown", "选择受支持的文本文件"));
    }
    auto original = readBounded(source_path);
    if (!original.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*original.error);
    auto decoded = decodeSourceText(*original.value);
    if (!decoded.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*decoded.error);

    const auto original_hash = xuyan::domain::sha256(*original.value);
    const auto normalized_hash = xuyan::domain::sha256(decoded.value->normalized_utf8);
    auto original_ref = storeAsset(original_hash, ".source", *original.value);
    if (!original_ref.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*original_ref.error);
    auto normalized_ref = storeAsset(normalized_hash, ".txt", decoded.value->normalized_utf8);
    if (!normalized_ref.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*normalized_ref.error);

    xuyan::domain::SourceDocument document;
    document.id = "source-" + xuyan::domain::sha256(world_id + "|" + original_hash).substr(0, 24);
    document.world_id = world_id;
    const auto filename = source_path.filename().u8string();
    document.name = filename.empty()
        ? "未命名来源"
        : std::string(reinterpret_cast<const char*>(filename.data()), filename.size());
    document.sha256 = original_hash;
    document.original_asset_ref = *original_ref.value;
    document.normalized_asset_ref = *normalized_ref.value;
    document.edition = edition.empty() ? "1" : edition;
    document.detected_encoding = decoded.value->encoding;
    auto chapters = xuyan::domain::detectChapters(decoded.value->normalized_utf8, document.id);
    if (!chapters.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*chapters.error);
    document.chapters = std::move(*chapters.value);
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.saveSource(command_id, document);
}

xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> SourceImportService::list() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listSources();
}

xuyan::domain::Result<std::string> SourceImportService::loadNormalizedText(const std::string& source_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto document = repository.loadSource(source_id);
    if (!document.ok()) return xuyan::domain::Result<std::string>::failure(*document.error);
    return readBounded(workspace_root_ / std::filesystem::path(document.value->normalized_asset_ref));
}

xuyan::domain::Result<std::string> SourceImportService::evidenceText(const std::string& source_id,
                                                                    std::size_t start_codepoint,
                                                                    std::size_t end_codepoint) {
    auto text = loadNormalizedText(source_id);
    if (!text.ok()) return text;
    return xuyan::domain::codepointSlice(*text.value, start_codepoint, end_codepoint);
}

xuyan::domain::Result<xuyan::domain::SourceDocument> SourceImportService::saveChapters(
    const std::string& command_id, const std::string& source_id, int expected_revision,
    std::vector<xuyan::domain::SourceChapter> chapters) {
    if (chapters.empty() || chapters.size() > 100000) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
        fileError("章节布局不能为空或超过数量上限", "保留至少一个章节"));
    auto text = loadNormalizedText(source_id);
    if (!text.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*text.error);
    const auto total = xuyan::domain::utf8CodepointCount(*text.value);
    std::sort(chapters.begin(), chapters.end(), [](const auto& left, const auto& right) {
        return left.start_codepoint < right.start_codepoint;
    });
    std::size_t previous_end = 0;
    for (std::size_t index = 0; index < chapters.size(); ++index) {
        auto& chapter = chapters[index];
        if (chapter.title.empty() || chapter.title.size() > 512 || chapter.start_codepoint >= chapter.end_codepoint
            || chapter.end_codepoint > total || (index > 0 && chapter.start_codepoint < previous_end))
            return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
                fileError("章节标题、范围或顺序无效；章节不能重叠", "修正码点半开区间后重试"));
        chapter.ordinal = static_cast<int>(index + 1);
        if (chapter.id.empty()) chapter.id = source_id + "-manual-" + std::to_string(index + 1);
        chapter.start_byte = byteOffsetForCodepoint(*text.value, chapter.start_codepoint);
        chapter.end_byte = byteOffsetForCodepoint(*text.value, chapter.end_codepoint);
        previous_end = chapter.end_codepoint;
    }
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.replaceSourceChapters(command_id, source_id, expected_revision, std::move(chapters));
}

} // namespace xuyan::application
