#pragma once

#include "xuyan/domain/source_document.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::application {

/* 主干启发式密度，不保证事实保真率；由界面映射中文。 */
enum class NarrativePreviewDensity {
    /* 保留未分类上下文。 */
    conservative,
    /* 交替保留未分类上下文。 */
    balanced,
    /* 仅保留规则命中的段及首尾锚点。 */
    compact
};
/* 句段保留/省略原因，仅解释启发式筛选，不判断事实性质。 */
enum class NarrativeSelectionReason {
    /* 章节或标题锚点。 */
    heading,
    /* 含对话符号。 */
    dialogue,
    /* 命中动作词表。 */
    action,
    /* 首尾锚点或未分类上下文。 */
    context,
    /* 密度规则省略的上下文。 */
    context_reduced,
    /* 描写词表命中而省略。 */
    description,
    /* 重复句段而省略。 */
    duplicate,
    /* 保留的排版空白。 */
    blank
};

/* 全段原文映射值；码点为来源绝对范围，字节为本片相对范围，与预览同寿命，不借用正文。 */
struct NarrativePreviewSegment {
    /* 来源绝对起始码点，初始 0，由构建/定位器填写，半开区间起点，供证据回查。 */
    std::size_t start_codepoint{0};
    /* 来源绝对结束码点，初始 0，半开区间终点，不是字节偏移，由构建/定位器填写。 */
    std::size_t end_codepoint{0};
    /* 相对本片原文的起始 UTF-8 字节，初始 0，构建器填写，须在字符边界。 */
    std::size_t start_byte{0};
    /* 相对本片原文的结束 UTF-8 字节，初始 0，半开终点，映射校验复核其码点跨度。 */
    std::size_t end_byte{0};
    /* 本段是否进入模型输入，默认 false，构建器分类设置；省略段隔断连续取证范围。 */
    bool retained{false};
    /* 筛选原因，默认上下文，由构建器填写，不代表事实置信度。 */
    NarrativeSelectionReason reason{NarrativeSelectionReason::context};
};

/* 只读派生输入值，拥有原文、预览和完整映射，不借用文件；预览不能直接作为世界事实。 */
struct NarrativePreview {
    /* 本片未删改的标准化原文，默认空，构建时拥有，所有映射/引文定位基于它。 */
    std::string source_text;
    /* 保留段拼成的输入，默认空，可含展示省略标记；省略标记不参与证据定位。 */
    std::string preview_text;
    /* 本片原文码点总数，初始 0，不含绝对基准偏移，用于校验映射完整覆盖。 */
    std::size_t source_codepoints{0};
    /* 保留原文码点数，初始 0，不计省略标记；压缩比例不等于事实保真度。 */
    std::size_t retained_codepoints{0};
    /* 按原文顺序覆盖完整输入的段映射，默认空，构建器写入、定位器只读，随预览拥有。 */
    std::vector<NarrativePreviewSegment> segments;
};

/* 唯一引文的绝对码点半开范围值，由定位器生成，不持有原文，可供候选再次校验。 */
struct SourceTextRange {
    /* 来源绝对起始码点，初始 0，由构建/定位器填写，半开区间起点，供证据回查。 */
    std::size_t start_codepoint{0};
    /* 来源绝对结束码点，初始 0，半开区间终点，不是字节偏移，由构建/定位器填写。 */
    std::size_t end_codepoint{0};
};

/*
 * 功能：按冻结配置构造单片原文或主干输入及映射。
 * 参数：source_text 为按值取得的合法 UTF-8 原文，1—50000 码点；base_codepoint 为来源绝对起点，不得与长度相加溢出；
 * config 为借用至返回的模式、密度及算法版本。
 * 返回：拥有正文和映射的预览。失败：配置、编码、长度或偏移无效返回 Result，分配异常可传播。
 * 副作用：仅内存构造，不读文件/联网；线程：同步，不保存输入引用。
 */
xuyan::domain::Result<NarrativePreview> buildExtractionInput(std::string source_text,
    std::size_t base_codepoint, const xuyan::domain::ExtractionInputConfig& config);

/*
 * 功能：验证全段映射后，在连续保留原文中唯一定位引文。
 * 参数：preview 为只读原文及映射；quote 为非空合法 UTF-8 引文视图，均借用至返回。
 * 返回：来源绝对码点范围。失败：映射不完整/错位、引文无效/重复/未出现或跨省略段返回 Result，分配异常可传播。
 * 副作用：只读内存，不以省略标记取证；线程：同步，结果不借用文本。
 */
xuyan::domain::Result<SourceTextRange> locateNarrativeQuote(const NarrativePreview& preview,
                                                             std::string_view quote);

/* 来源导入、原文读取与章节校正入口；持有数据库/资产根路径，调用线程使用局部仓储与文件流，不拥有线程。 */
class SourceImportService {
public:
    /*
     * 功能：绑定来源工作区及资产根。参数：database_path 为按值路径，父目录作为资产根。
     * 返回：完成初始化。失败：路径分配异常传播，不校验资产。
     * 副作用：无磁盘访问；线程：同步构造，不拥有连接/文件句柄。
     */
    explicit SourceImportService(std::filesystem::path database_path);

    /*
     * 功能：导入本地文本，解码并保存不可变资产和章节。
     * 参数：command_id 为幂等命令；source_path 为 TXT/Markdown 文件，最多 64 MiB；edition 默认 1，空串归一为 1；
     * world_id 虽默认空，但实际须非空有效世界，否则拒绝。
     * 返回：来源及识别章节。失败：世界/扩展名/大小/编码/空正文/资产或仓储错误返回 Result，未捕获异常可传播。
     * 副作用：按哈希写原始/标准化资产后提交元数据；元数据失败可能留下未关联资产，不复制进仓库/发行包。
     * 线程：同步读取与写入，无网络/线程；调用方串行协调同工作区导入，章节编辑不覆盖正文。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> importTextFile(
        const std::string& command_id, const std::filesystem::path& source_path,
        const std::string& edition = "1", const std::string& world_id = {});
    /*
     * 功能：查询全部来源及章节。参数：无。返回：列表，空库成功为空。
     * 失败：仓储错误返回 Result，构造异常可传播。副作用：只读元数据，打开数据库可能迁移；线程：同步，不加载正文。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> list();
    /*
     * 功能：按世界查询来源及章节。参数：world_id 为世界稳定标识。
     * 返回：列表，无来源成功为空。失败：世界/存储错误返回 Result，构造异常可传播。
     * 副作用：只读元数据，不加载正文；线程：调用线程同步查询。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listForWorld(const std::string& world_id);
    /*
     * 功能：按稳定标识读取单个来源的元数据和章节，供候选审核核对世界归属；不加载小说正文。
     * 参数：source_id 为非空来源标识，仅在本次调用期间借用，不作为用户可见名称。
     * 返回：成功时返回独立拥有的来源及章节；不存在时返回失败，不伪造空来源。
     * 失败：标识缺失、数据库读取或迁移失败经 Result 报告；异常转换为不含路径和原文的中文错误。
     * 副作用：只读来源记录；打开数据库时可能执行结构迁移，不发送网络请求。
     * 线程：在调用线程同步完成，不保存调用方字符串或跨线程共享数据库连接。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> load(const std::string& source_id);
    /*
     * 功能：读取完整标准化资产。参数：source_id 为来源标识。
     * 返回：拥有的文件字节，空文件可成功，读取上限 64 MiB，此处不核对内容哈希。
     * 失败：来源/大小/权限/读错误返回 Result，构造异常可传播。
     * 副作用：只读数据库/文件，全文驻留本次内存；线程：同步，长任务优先 evidenceText 分片。
     */
    xuyan::domain::Result<std::string> loadNormalizedText(const std::string& source_id);
    /*
     * 功能：从最近章节锚点逐字符读取证据范围。
     * 参数：source_id 为来源；start_codepoint、end_codepoint 为绝对码点半开范围，须有序且可读，等值可返回空串。
     * 返回：保持 UTF-8 字节的原文。失败：来源/锚点/范围/编码/读取错误返回 Result，构造异常可传播。
     * 副作用：只读元数据/资产，不改原文；线程：同步，局部文件流于返回关闭。
     */
    xuyan::domain::Result<std::string> evidenceText(const std::string& source_id,
                                                    std::size_t start_codepoint,
                                                    std::size_t end_codepoint);
    /*
     * 功能：按范围生成非权威主干预览。
     * 参数：source_id 为来源；start_codepoint、end_codepoint 为绝对半开码点范围，长度须 1—50000；
     * density 默认 balanced，为保留密度，不是事实保真级别。
     * 返回：拥有原文、派生输入及映射的预览。失败：范围/读文件/编码错误返回 Result，构造/分配异常可传播。
     * 副作用：只读，不改来源/候选、不发送；线程：同步，无后台任务。
     */
    xuyan::domain::Result<NarrativePreview> previewBackbone(const std::string& source_id,
                                                              std::size_t start_codepoint,
                                                              std::size_t end_codepoint,
                                                              NarrativePreviewDensity density = NarrativePreviewDensity::balanced);
    /*
     * 功能：校验人工章节并重算字节锚点。
     * 参数：command_id 为幂等命令；source_id 为来源；expected_revision 为当前来源修订；chapters 为 1—100000 章，
     * 标题 1—512 字节、码点范围连续覆盖全文，排序后重编号，空 ID 自动生成。
     * 返回：新修订来源。失败：字段、范围、覆盖、修订或存储错误返回 Result。
     * 副作用：读完整正文、事务写章节，不修改不可变正文；线程：同步，按值容器不借用调用方。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> saveChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

private:
    /*
     * 功能：大小检查后读取有界文件。参数：path 为只读路径。
     * 返回：拥有文件字节，空文件成功。失败：大小/打开/流错误返回 Result，分配异常可传播。
     * 副作用：只读文件，上限按读取前大小检查，调用方须保证读取期间源不变；线程：同步，流于返回关闭。
     */
    xuyan::domain::Result<std::string> readBounded(const std::filesystem::path& path) const;
    /*
     * 功能：内容寻址保存资产，复用时逐块校验完全相同。
     * 参数：hash 为内部 SHA-256 内容摘要；suffix 为内部固定后缀；bytes 为借用至返回的资产字节。
     * 返回：工作区相对资产路径。失败：链接、内容不符、目录/读写/重命名错误返回 Result。
     * 副作用：写临时文件后重命名，失败可能留临时文件，不覆盖原文、不记录内容；线程：同步，调用方串行同哈希写入。
     */
    xuyan::domain::Result<std::string> storeAsset(const std::string& hash, std::string_view suffix,
                                                 std::string_view bytes) const;

    /* 来源元数据路径；构造后只读，与服务同寿命，不持有常驻连接。 */
    std::filesystem::path database_path_;
    /* 数据库父目录资产根；构造推导、之后只读，用于解析来源相对路径，与服务同寿命。 */
    std::filesystem::path workspace_root_;
};

} // namespace xuyan::application
