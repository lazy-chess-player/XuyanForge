#include "xuyan/application/source_import_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"
#include "xuyan/application/source_text_decoder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace xuyan::application {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

/*
 * 功能：构造来源文件或原文范围无效的中文校验错误。
 * 参数：message 为按值取得的原因；action 为按值取得的修复建议，省略时提示检查路径和权限。
 * 返回：不建议自动重试的错误值。失败：字符串构造异常可传播。
 * 副作用：仅内存构造，不读取文件；线程：同步，不保存输入引用。
 */
Error fileError(std::string message, std::string action = "检查文件路径与访问权限") {
    return Error{ErrorCode::validation_failed, std::move(message), false, std::move(action)};
}

/*
 * 功能：从已扫描锚点向前计算指定 Unicode 码点对应的 UTF-8 字节偏移。
 * 参数：utf8 为调用期间有效的已验证正文；wanted 为目标绝对码点；byte、codepoint 为输入/输出游标，
 * 分别以字节和码点计，调用前二者须对应同一文本位置且 wanted 不早于 codepoint。
 * 返回：目标字节偏移，同时更新两个游标。失败：倒退或超出正文时抛异常。
 * 副作用：仅改变游标，不改正文；线程：同步，不保存视图。
 */
std::size_t advanceByteOffsetForCodepoint(std::string_view utf8, std::size_t wanted,
                                          std::size_t& byte, std::size_t& codepoint) {
    if (wanted < codepoint) throw std::runtime_error("章节码点边界顺序无效");
    while (byte < utf8.size() && codepoint < wanted) {
        const auto lead = static_cast<unsigned char>(utf8[byte]);
        byte += lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        ++codepoint;
    }
    if (codepoint != wanted) throw std::runtime_error("章节码点边界超出原文");
    return byte;
}

/*
 * 功能：从最近章节锚点顺序读取标准化资产的原文码点半开区间。
 * 参数：path 为只读资产路径；anchor_byte 为文件中字节起点；anchor_codepoint 为同一位置的绝对码点；
 * start_codepoint、end_codepoint 为请求的绝对半开范围，须满足锚点不晚于起点且起点不晚于终点。
 * 返回：原样 UTF-8 字节；等长区间返回空串。失败：范围、打开、截断或字符无效返回 Result。
 * 副作用：只读文件，流于返回关闭；线程：同步，不保存路径或字节视图。
 */
xuyan::domain::Result<std::string> readCodepointRange(const std::filesystem::path& path,
                                                      std::size_t anchor_byte,
                                                      std::size_t anchor_codepoint,
                                                      std::size_t start_codepoint,
                                                      std::size_t end_codepoint) {
    if (end_codepoint < start_codepoint || anchor_codepoint > start_codepoint) {
        return xuyan::domain::Result<std::string>::failure(
            fileError("证据码点区间超出标准化文本", "重新定位来源证据"));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return xuyan::domain::Result<std::string>::failure(fileError("无法打开标准化来源文件"));
    input.seekg(static_cast<std::streamoff>(anchor_byte));
    if (!input) return xuyan::domain::Result<std::string>::failure(fileError("章节索引超出标准化来源文件"));

    std::string result;
    for (std::size_t codepoint = anchor_codepoint; codepoint < end_codepoint; ++codepoint) {
        std::array<char, 4> bytes{};
        if (!input.get(bytes[0])) {
            return xuyan::domain::Result<std::string>::failure(
                fileError("证据码点区间超出标准化文本", "重新定位来源证据"));
        }
        const auto lead = static_cast<unsigned char>(bytes[0]);
        const std::size_t width = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2
            : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        if (width == 0) return xuyan::domain::Result<std::string>::failure(fileError("标准化来源文件包含无效字符"));
        if (width > 1 && !input.read(bytes.data() + 1, static_cast<std::streamsize>(width - 1))) {
            return xuyan::domain::Result<std::string>::failure(fileError("标准化来源文件在字符中途结束"));
        }
        if (xuyan::domain::utf8CodepointCount(std::string_view(bytes.data(), width)) != 1) {
            return xuyan::domain::Result<std::string>::failure(fileError("标准化来源文件包含无效字符"));
        }
        if (codepoint >= start_codepoint) result.append(bytes.data(), width);
    }
    return xuyan::domain::Result<std::string>::success(std::move(result));
}

/*
 * 主干候选句段的临时范围对象，仅在一次预览构造中存活，不持有或修改正文。
 * 每个半开边界都有字节与 Unicode 码点两套单位，初值为零，由顺序扫描填充。
 */
struct PreviewUnit {
    /* 句段的 UTF-8 起始字节偏移，初始 0，扫描器写入，筛选器读取。 */
    std::size_t start_byte{0};
    /* 句段的 UTF-8 结束字节偏移，初始 0，半开终点。 */
    std::size_t end_byte{0};
    /* 句段的片内起始 Unicode 码点偏移，初始 0。 */
    std::size_t start_codepoint{0};
    /* 句段的片内结束 Unicode 码点偏移，初始 0，半开终点。 */
    std::size_t end_codepoint{0};
};

/*
 * 功能：从句段视图两端剔除 ASCII 控制和空格字节，不复制正文。
 * 参数：text 为调用期间有效的 UTF-8 字节视图，允许为空。
 * 返回：指向原有存储的子视图，寿命不得超过输入正文。
 * 失败：无主动错误；副作用：仅修改局部视图边界；线程：同步，不保留视图。
 */
std::string_view trimAsciiSpace(std::string_view text) {
    while (!text.empty() && static_cast<unsigned char>(text.front()) <= 0x20) text.remove_prefix(1);
    while (!text.empty() && static_cast<unsigned char>(text.back()) <= 0x20) text.remove_suffix(1);
    return text;
}

/*
 * 功能：检查句段是否出现任一关键词的完整字节序列。
 * 参数：text 为借用的句段视图；terms 为调用期间有效的关键词视图列表，可为空。
 * 返回：命中任一词返回真；空词表或未命中返回假。失败：无主动错误。
 * 副作用：只读输入；线程：在调用线程同步遍历，捕获视图不逃逸。
 */
bool containsAny(std::string_view text, std::initializer_list<std::string_view> terms) {
    /*
     * 功能：判断当前关键词是否在本句段的字节序列中出现。
     * 参数：term 为本次遍历借用的关键词；text 按值捕获视图，仅在同步算法调用期间有效。
     * 返回：找到时为真。失败：无主动错误；副作用：只读，不保留捕获。
     */
    return std::any_of(terms.begin(), terms.end(), [text](std::string_view term) {
        return text.find(term) != std::string_view::npos;
    });
}

/*
 * 功能：按 backbone-v1 将单片原文切成可回查句段，并生成非权威主干预览。
 * 参数：source_text 为按值取得的有效 UTF-8 原文；base_codepoint 为来源绝对起点；
 * density 为保留密度档位，不表示事实正确率。
 * 返回：拥有原文、派生预览及逐段字节/码点映射；正文非法时返回 Result 错误。
 * 失败：非法 UTF-8 返回错误，容器分配异常可传播；不对算法事实召回作保证。
 * 副作用：仅内存构造，不修改原文资产或候选；线程：同步，临时 string_view 均不逃逸返回。
 */
xuyan::domain::Result<NarrativePreview> buildNarrativePreview(std::string source_text,
                                                               std::size_t base_codepoint,
                                                               NarrativePreviewDensity density) {
    NarrativePreview preview;
    preview.source_text = std::move(source_text);
    preview.source_codepoints = xuyan::domain::utf8CodepointCount(preview.source_text);
    if (!preview.source_text.empty() && preview.source_codepoints == 0)
        return xuyan::domain::Result<NarrativePreview>::failure(
            fileError("标准化原文包含无效字符", "重新导入来源文件"));

    const std::string_view source = preview.source_text;
    std::vector<PreviewUnit> units;
    std::size_t unit_start_byte = 0;
    std::size_t unit_start_codepoint = 0;
    std::size_t byte = 0;
    std::size_t codepoint = 0;
    // 句段只记录原文区间；UTF-8 字节偏移用于截取，Unicode 码点偏移用于证据锚点。
    while (byte < source.size()) {
        const auto lead = static_cast<unsigned char>(source[byte]);
        const auto width = lead < 0x80 ? 1U : lead < 0xe0 ? 2U : lead < 0xf0 ? 3U : 4U;
        const auto current = source.substr(byte, width);
        auto end_byte = byte + width;
        auto end_codepoint = codepoint + 1;
        const bool sentence_end = containsAny(current, {"。", "！", "？", "；", "!", "?", ";"});
        const bool boundary = sentence_end || current == "\n";
        if (sentence_end) {
            while (end_byte < source.size()) {
                const auto remaining = source.substr(end_byte);
                if (remaining.starts_with("”") || remaining.starts_with("’")
                    || remaining.starts_with("」") || remaining.starts_with("』")
                    || remaining.starts_with("）")) end_byte += 3;
                else if (remaining.starts_with('"') || remaining.starts_with(')')) ++end_byte;
                else break;
                ++end_codepoint;
            }
            if (end_byte < source.size() && source[end_byte] == '\n') {
                ++end_byte;
                ++end_codepoint;
            }
        }
        if (boundary) {
            units.push_back({unit_start_byte, end_byte, unit_start_codepoint, end_codepoint});
            unit_start_byte = end_byte;
            unit_start_codepoint = end_codepoint;
        }
        byte = end_byte;
        codepoint = end_codepoint;
    }
    if (unit_start_byte < source.size())
        units.push_back({unit_start_byte, source.size(), unit_start_codepoint, codepoint});

    // 首尾非空句段保留为上下文锚点，其他句段才按词表与密度档位做可审阅筛选。
    std::size_t first_content = units.size();
    std::size_t last_content = units.size();
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        if (trimAsciiSpace(source.substr(unit.start_byte, unit.end_byte - unit.start_byte)).empty()) continue;
        if (first_content == units.size()) first_content = index;
        last_content = index;
    }

    // 视图仅指向 preview.source_text，整个分类阶段都不会再修改该字符串。
    std::unordered_set<std::string_view> seen;
    preview.segments.reserve(units.size());
    std::size_t previous_retained_end = 0;
    std::size_t context_ordinal = 0;
    bool has_retained = false;
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto& unit = units[index];
        const auto content = trimAsciiSpace(source.substr(unit.start_byte, unit.end_byte - unit.start_byte));
        const bool duplicate = !content.empty() && !seen.insert(content).second;
        const bool heading = content.starts_with('#') || (content.starts_with("第")
            && content.size() <= 96 && containsAny(content, {"章", "回", "节", "卷"}));
        const bool dialogue = containsAny(content, {"“", "”", "「", "」", "『", "』"});
        // 词表只用于只读输入筛选，不是事实判定器或完整性保证；未知句段由密度决定是否保留。
        const bool action = containsAny(content, {
            "发现", "决定", "告诉", "承诺", "答应", "拒绝", "攻击", "战斗", "杀死", "死亡",
            "受伤", "逃走", "出发", "抵达", "到达", "进入", "离开", "拿到", "获得", "失去",
            "夺走", "交给", "救出", "解开", "打开", "关闭", "醒来", "开始", "结束", "出现",
            "改变", "成为", "建立", "毁灭", "背叛", "怀疑", "得知", "知道", "看到", "听到",
            "发生", "揭露", "隐藏", "找到", "确认", "调查", "宣布", "命令", "计划", "行动",
            "封印", "解除", "突破", "晋级", "击败", "胜利", "失败"});
        const bool description = containsAny(content, {
            "微风", "阳光", "月光", "晚霞", "景色", "天空", "云朵", "树影", "花香", "香气",
            "美丽", "静谧", "朦胧", "柔和", "蔚蓝", "缓缓", "仿佛", "似乎", "风景"});
        bool retained = false;
        auto reason = NarrativeSelectionReason::context;
        if (content.empty()) { retained = true; reason = NarrativeSelectionReason::blank; }
        else if (index == first_content || index == last_content || heading) {
            retained = true;
            reason = heading ? NarrativeSelectionReason::heading : NarrativeSelectionReason::context;
        } else if (duplicate) reason = NarrativeSelectionReason::duplicate;
        else if (dialogue) { retained = true; reason = NarrativeSelectionReason::dialogue; }
        else if (action) { retained = true; reason = NarrativeSelectionReason::action; }
        else if (description) reason = NarrativeSelectionReason::description;
        else {
            ++context_ordinal;
            retained = density == NarrativePreviewDensity::conservative
                || (density == NarrativePreviewDensity::balanced && context_ordinal % 2 == 1);
            if (!retained) reason = NarrativeSelectionReason::context_reduced;
        }

        // 无论是否保留，都记录完整原文区间；展示用省略标记不参与证据定位。
        preview.segments.push_back({base_codepoint + unit.start_codepoint,
                                    base_codepoint + unit.end_codepoint,
                                    unit.start_byte, unit.end_byte, retained, reason});
        if (!retained) continue;
        if (has_retained && previous_retained_end != unit.start_byte) preview.preview_text += "\n〔省略〕\n";
        preview.preview_text.append(source.substr(unit.start_byte, unit.end_byte - unit.start_byte));
        preview.retained_codepoints += unit.end_codepoint - unit.start_codepoint;
        previous_retained_end = unit.end_byte;
        has_retained = true;
    }
    return xuyan::domain::Result<NarrativePreview>::success(std::move(preview));
}

} // namespace

xuyan::domain::Result<NarrativePreview> buildExtractionInput(std::string source_text,
    std::size_t base_codepoint, const xuyan::domain::ExtractionInputConfig& config) {
    auto valid = xuyan::domain::validateExtractionInputConfig(config);
    if (!valid.ok()) return xuyan::domain::Result<NarrativePreview>::failure(*valid.error);
    const auto count = xuyan::domain::utf8CodepointCount(source_text);
    if (count == 0 || count > 50000 || base_codepoint > std::numeric_limits<std::size_t>::max() - count)
        return xuyan::domain::Result<NarrativePreview>::failure(fileError("解析输入必须为有效的单片原文"));
    if (config.mode == "backbone") {
        const auto density = config.density == "compact" ? NarrativePreviewDensity::compact
            : config.density == "conservative" ? NarrativePreviewDensity::conservative : NarrativePreviewDensity::balanced;
        return buildNarrativePreview(std::move(source_text), base_codepoint, density);
    }
    NarrativePreview input;
    input.source_text = std::move(source_text);
    input.preview_text = input.source_text;
    input.source_codepoints = input.retained_codepoints = count;
    input.segments.push_back({base_codepoint, base_codepoint + count, 0, input.source_text.size(),
                              true, NarrativeSelectionReason::context});
    return xuyan::domain::Result<NarrativePreview>::success(std::move(input));
}

xuyan::domain::Result<SourceTextRange> locateNarrativeQuote(const NarrativePreview& preview,
                                                             std::string_view quote) {
    const auto quote_codepoints = xuyan::domain::utf8CodepointCount(quote);
    if (quote_codepoints == 0) return xuyan::domain::Result<SourceTextRange>::failure(
        fileError("引文不能为空且必须是合法文本", "从保留的原文句段选择引文"));
    const std::string_view source = preview.source_text;
    /*
     * 连续保留的原文区间，仅在本次引文定位中存活；跳过的句段会断开区间。
     * 三个偏移均从已校验的预览映射读取，未提供默认值，由追加时完整初始化。
     */
    struct RetainedRun {
        /* 连续区间的 UTF-8 起始字节偏移，搜索与回查时只读。 */
        std::size_t start_byte;
        /* 连续区间的 UTF-8 半开结束字节偏移，相邻句段合并时更新。 */
        std::size_t end_byte;
        /* 与 start_byte 对应的来源绝对 Unicode 码点偏移，只读。 */
        std::size_t start_codepoint;
    };
    std::vector<RetainedRun> runs;
    std::size_t expected_byte = 0;
    std::size_t expected_codepoint = preview.segments.empty() ? 0 : preview.segments.front().start_codepoint;
    // 先验证整张映射表，再合并相邻保留句段；省略片段始终隔断可搜索区间。
    for (const auto& segment : preview.segments) {
        if (segment.start_byte != expected_byte || segment.start_codepoint != expected_codepoint
            || segment.end_byte < segment.start_byte || segment.end_byte > source.size()
            || segment.end_codepoint < segment.start_codepoint)
            return xuyan::domain::Result<SourceTextRange>::failure(
                fileError("主干预览的原文映射无效", "重新生成预览"));
        const auto segment_text = source.substr(segment.start_byte, segment.end_byte - segment.start_byte);
        const auto segment_codepoints = xuyan::domain::utf8CodepointCount(segment_text);
        if ((!segment_text.empty() && segment_codepoints == 0)
            || segment_codepoints != segment.end_codepoint - segment.start_codepoint)
            return xuyan::domain::Result<SourceTextRange>::failure(
                fileError("主干预览的原文映射无效", "重新生成预览"));
        if (segment.retained) {
            if (!runs.empty() && runs.back().end_byte == segment.start_byte)
                runs.back().end_byte = segment.end_byte;
            else runs.push_back({segment.start_byte, segment.end_byte, segment.start_codepoint});
        }
        expected_byte = segment.end_byte;
        expected_codepoint = segment.end_codepoint;
    }
    if (expected_byte != source.size() || (!preview.segments.empty()
        && expected_codepoint - preview.segments.front().start_codepoint != preview.source_codepoints))
        return xuyan::domain::Result<SourceTextRange>::failure(
            fileError("主干预览的原文映射无效", "重新生成预览"));

    std::optional<SourceTextRange> found;
    // 在连续保留的原文字节中搜索，因此允许引文跨句段，但绝不跨省略段。
    for (const auto& run : runs) {
        const auto text = source.substr(run.start_byte, run.end_byte - run.start_byte);
        for (auto position = text.find(quote); position != std::string_view::npos;
             position = text.find(quote, position + 1)) {
            if (found.has_value()) return xuyan::domain::Result<SourceTextRange>::failure(
                fileError("引文在保留片段中出现多次，无法唯一定位", "选择更长的逐字引文"));
            const auto start = run.start_codepoint
                + xuyan::domain::utf8CodepointCount(text.substr(0, position));
            found = SourceTextRange{start, start + quote_codepoints};
        }
    }
    if (!found.has_value()) return xuyan::domain::Result<SourceTextRange>::failure(
        fileError("引文不在保留的原文片段中", "检查原文或重新生成预览"));
    return xuyan::domain::Result<SourceTextRange>::success(*found);
}

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
        if (std::filesystem::is_symlink(absolute)) {
            throw std::runtime_error("来源资产路径不能是符号链接");
        }
        if (std::filesystem::exists(absolute)) {
            // 同名资产必须逐字节一致；不能因路径命中就信任旧文件，否则章节和证据会引用错误正文。
            if (!std::filesystem::is_regular_file(absolute)
                || std::filesystem::file_size(absolute) != bytes.size()) {
                throw std::runtime_error("已有来源资产与导入文本不一致");
            }
            std::ifstream existing(absolute, std::ios::binary);
            if (!existing) throw std::runtime_error("无法校验已有来源资产");
            std::array<char, 64 * 1024> buffer{};
            for (std::size_t offset = 0; offset < bytes.size();) {
                const auto count = std::min(buffer.size(), bytes.size() - offset);
                existing.read(buffer.data(), static_cast<std::streamsize>(count));
                if (existing.gcount() != static_cast<std::streamsize>(count)
                    || !std::equal(buffer.data(), buffer.data() + count, bytes.data() + offset)) {
                    throw std::runtime_error("已有来源资产与导入文本不一致");
                }
                offset += count;
            }
            if (existing.peek() != std::char_traits<char>::eof() || existing.bad()) {
                throw std::runtime_error("已有来源资产与导入文本不一致");
            }
        } else {
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
    /*
     * 功能：把扩展名的 ASCII 大写字节转为小写，以进行格式白名单比较。
     * 参数：value 为扩展名当前字节，按值传入，不借用原字符串。
     * 返回：转换后的字节。失败：区域设置转换无 Result 错误。
     * 副作用：lambda 本身无状态，外层 transform 原位更新 extension；线程：同步，不逃逸。
     */
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
    if (decoded.value->normalized_utf8.empty()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
        fileError("小说正文为空，无法建立章节", "选择包含正文的文本文件"));

    // 原始资产与标准化资产分别按内容寻址；校正章节不会覆盖任一版本的正文。
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
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        return repository.saveSource(command_id, document);
    } catch (const std::exception&) {
        // 数据库打不开时不让异常越过导入服务边界；已有资产保持原样，便于修复工作区后重试。
        return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            {ErrorCode::storage_error, "导入来源保存失败", true, "检查工作区文件与权限后重试"});
    }
}

xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> SourceImportService::list() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listSources();
}

xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> SourceImportService::listForWorld(
    const std::string& world_id) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    return repository.listSourcesForWorld(world_id);
}

xuyan::domain::Result<xuyan::domain::SourceDocument> SourceImportService::load(const std::string& source_id) {
    try {
        return xuyan::storage::WorkspaceRepository(database_path_).loadSource(source_id);
    } catch (...) {
        return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            {ErrorCode::storage_error, "无法读取来源资料，详情已隐藏", true, "检查工作区后重试"});
    }
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
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto document = repository.loadSource(source_id);
    if (!document.ok()) return xuyan::domain::Result<std::string>::failure(*document.error);
    // 从最近的章节锚点扫描，避免每次取证都从长篇文件开头遍历。
    std::size_t anchor_byte = 0;
    std::size_t anchor_codepoint = 0;
    for (const auto& chapter : document.value->chapters) {
        if (chapter.start_codepoint <= start_codepoint && chapter.start_codepoint >= anchor_codepoint) {
            anchor_byte = chapter.start_byte;
            anchor_codepoint = chapter.start_codepoint;
        }
    }
    return readCodepointRange(workspace_root_ / std::filesystem::path(document.value->normalized_asset_ref),
                              anchor_byte, anchor_codepoint, start_codepoint, end_codepoint);
}

xuyan::domain::Result<NarrativePreview> SourceImportService::previewBackbone(
    const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
    NarrativePreviewDensity density) {
    if (end_codepoint <= start_codepoint || end_codepoint - start_codepoint > 50000)
        return xuyan::domain::Result<NarrativePreview>::failure(
            fileError("主干预览范围必须为 1—50000 码点", "选择单个解析切片进行预览"));
    auto source = evidenceText(source_id, start_codepoint, end_codepoint);
    if (!source.ok()) return xuyan::domain::Result<NarrativePreview>::failure(*source.error);
    return buildNarrativePreview(std::move(*source.value), start_codepoint, density);
}

xuyan::domain::Result<xuyan::domain::SourceDocument> SourceImportService::saveChapters(
    const std::string& command_id, const std::string& source_id, int expected_revision,
    std::vector<xuyan::domain::SourceChapter> chapters) {
    try {
        if (chapters.empty() || chapters.size() > 100000) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            fileError("章节布局不能为空或超过数量上限", "保留至少一个章节"));
        auto text = loadNormalizedText(source_id);
        if (!text.ok()) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(*text.error);
        const auto total = xuyan::domain::utf8CodepointCount(*text.value);
        /*
         * 功能：按章节原文起始码点排序，以便随后验证连续覆盖。
         * 参数：left、right 为排序期间借用的章节对象，均不允许在比较中修改。
         * 返回：left 起点早于 right 时为真。失败：比较本身无业务错误。
         * 副作用：lambda 只读；外层 sort 重排按值取得的 chapters；线程：同步，引用不逃逸。
         */
        std::sort(chapters.begin(), chapters.end(), [](const auto& left, const auto& right) {
            return left.start_codepoint < right.start_codepoint;
        });
        // 只顺序扫描一次标准化正文；章节必须连续覆盖全文，不能悄悄漏掉待解析原文。
        std::size_t previous_end = 0;
        std::size_t scanned_byte = 0;
        std::size_t scanned_codepoint = 0;
        for (std::size_t index = 0; index < chapters.size(); ++index) {
            auto& chapter = chapters[index];
            if (chapter.title.empty() || chapter.title.size() > 512 || chapter.start_codepoint >= chapter.end_codepoint
                || chapter.end_codepoint > total || chapter.start_codepoint != previous_end)
                return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
                    fileError("章节标题或范围无效；章节必须连续覆盖原文", "修正相邻章节的码点边界后重试"));
            chapter.ordinal = static_cast<int>(index + 1);
            if (chapter.id.empty()) chapter.id = source_id + "-manual-" + std::to_string(index + 1);
            chapter.start_byte = advanceByteOffsetForCodepoint(*text.value, chapter.start_codepoint,
                                                                scanned_byte, scanned_codepoint);
            chapter.end_byte = advanceByteOffsetForCodepoint(*text.value, chapter.end_codepoint,
                                                              scanned_byte, scanned_codepoint);
            previous_end = chapter.end_codepoint;
        }
        if (previous_end != total) return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            fileError("章节布局没有覆盖原文末尾", "将末章终点设为原文总码点数"));
        xuyan::storage::WorkspaceRepository repository(database_path_);
        return repository.replaceSourceChapters(command_id, source_id, expected_revision, std::move(chapters));
    } catch (const std::exception&) {
        // 存储构造或文件系统异常仍通过服务的 Result 契约返回，不让后台任务异常终止。
        return xuyan::domain::Result<xuyan::domain::SourceDocument>::failure(
            {ErrorCode::storage_error, "章节布局保存失败", true, "检查工作区文件与权限后重试"});
    }
}

} // namespace xuyan::application
