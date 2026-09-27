#pragma once

#include "xuyan/domain/source_document.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

enum class NarrativePreviewDensity { conservative, balanced, compact };
enum class NarrativeSelectionReason {
    heading, dialogue, action, context, context_reduced, description, duplicate, blank
};

/** 每段保留原文绝对码点范围，以及本次预览 source_text 内的相对 UTF-8 字节范围。 */
struct NarrativePreviewSegment {
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
    std::size_t start_byte{0};
    std::size_t end_byte{0};
    bool retained{false};
    NarrativeSelectionReason reason{NarrativeSelectionReason::context};
};

/** 只读启发式预览；source_text 始终是未删改的原文，preview_text 不能直接提交为世界事实。 */
struct NarrativePreview {
    std::string source_text;
    std::string preview_text;
    std::size_t source_codepoints{0};
    std::size_t retained_codepoints{0};
    std::vector<NarrativePreviewSegment> segments;
};

struct SourceTextRange {
    std::size_t start_codepoint{0};
    std::size_t end_codepoint{0};
};

/** @brief 在连续保留的原文中唯一定位逐字引文；跨省略段、重复或无效引文返回错误。 */
xuyan::domain::Result<SourceTextRange> locateNarrativeQuote(const NarrativePreview& preview,
                                                             std::string_view quote);

class SourceImportService {
public:
    /** @brief 绑定工作区数据库，并以其父目录作为不可变来源资产的存储根目录。 */
    explicit SourceImportService(std::filesystem::path database_path);

    /** @brief 解码并导入本地 TXT/Markdown，保存原始与标准化资产及章节索引。 */
    xuyan::domain::Result<xuyan::domain::SourceDocument> importTextFile(
        const std::string& command_id, const std::filesystem::path& source_path,
        const std::string& edition = "1", const std::string& world_id = {});
    /** @brief 列出工作区内已导入的来源及其章节布局。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> list();
    /** @brief 仅列出选定世界的已导入来源，避免读取其他世界的章节索引。 */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listForWorld(const std::string& world_id);
    /** @brief 按来源 ID 读取完整标准化正文；仅适用于导入大小上限内的文件。 */
    xuyan::domain::Result<std::string> loadNormalizedText(const std::string& source_id);
    /** @brief 按原文绝对码点半开区间读取证据，优先从章节字节锚点顺序扫描。 */
    xuyan::domain::Result<std::string> evidenceText(const std::string& source_id,
                                                    std::size_t start_codepoint,
                                                    std::size_t end_codepoint);
    /** @brief 生成最多 50000 码点的只读主干预览；不调用模型、不改来源或候选。 */
    xuyan::domain::Result<NarrativePreview> previewBackbone(const std::string& source_id,
                                                              std::size_t start_codepoint,
                                                              std::size_t end_codepoint,
                                                              NarrativePreviewDensity density = NarrativePreviewDensity::balanced);
    /** @brief 按期望修订保存人工校正的章节布局，并重算每章的原文字节锚点。 */
    xuyan::domain::Result<xuyan::domain::SourceDocument> saveChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

private:
    /** @brief 在 64 MiB 导入上限内读取文件，拒绝无权限或不完整的读取。 */
    xuyan::domain::Result<std::string> readBounded(const std::filesystem::path& path) const;
    /** @brief 按内容哈希写入一次性资产文件，并返回工作区相对路径。 */
    xuyan::domain::Result<std::string> storeAsset(const std::string& hash, std::string_view suffix,
                                                 std::string_view bytes) const;

    std::filesystem::path database_path_;
    std::filesystem::path workspace_root_;
};

} // namespace xuyan::application
