#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：不可变正文上的章节字节/Unicode码点双锚点。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct SourceChapter {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 从小说检测或人工校正的章节标题。默认空串。
    std::string title;
    // 来源中从1开始的章节顺序号，索引创建时填写；默认0表示未规划。
    int ordinal{0};
    // 规范化UTF-8原文中的零基字节起点，范围包含起点。默认0。
    std::size_t start_byte{0};
    // 规范化UTF-8原文中的零基字节终点，范围不包含终点。默认0。
    std::size_t end_byte{0};
    // 原文Unicode码点的零基起点，半开范围包含此位置。默认0。
    std::size_t start_codepoint{0};
    // 原文Unicode码点的零基终点，半开范围不包含此位置。默认0。
    std::size_t end_codepoint{0};
};

/*
 * 职责：小说资产引用、编码与可修订章节索引，不存整书正文。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct SourceDocument {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 原始输入文件字节的SHA-256摘要，默认空；与去文件头/换行及编码转换后的正文摘要不同。
    std::string sha256;
    // 原始输入文件的不可变资产引用。默认空串。
    std::string original_asset_ref;
    // 规范化UTF-8正文资产引用，不能用主干预览替代。默认空串。
    std::string normalized_asset_ref;
    // 来源版次字符串，用于区分不同导入版本。默认"1"。
    std::string edition{"1"};
    // 输入文件检测到的原始编码内部标识。默认"utf-8"。
    std::string detected_encoding{"utf-8"};
    // 人工章节布局修订号，保存校正时递增。默认1。
    int chapter_revision{1};
    // 按原文顺序排列的章节范围，字节/码点锚点同时保留。默认空集合，不预填资料。
    std::vector<SourceChapter> chapters;
};

/*
 * 功能：校验 UTF-8、移除 BOM，并将 CRLF/CR 统一为 LF；无效序列返回错误。
 * 参数：
 *   input：借用的UTF-8输入，允许文件头标记和不同换行，调用期间有效。
 * 返回：规范化为LF并移除文件头的正文；无效UTF-8返回错误，不猜测原始编码。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<std::string> normalizeUtf8Text(std::string_view input);
/*
 * 功能：扫描 Markdown 与中文章回标题，返回覆盖全文的章节字节/码点区间。
 * 参数：
 *   normalized_utf8：借用完整规范化UTF-8原文，不是主干。
 *   document_id：来源稳定标识，参与章节ID生成。
 * 返回：覆盖全文的有序章节，字节和码点为半开区间；无效正文返回错误。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<std::vector<SourceChapter>> detectChapters(std::string_view normalized_utf8,
                                                  std::string_view document_id);
/*
 * 功能：按 Unicode 码点半开区间截取 UTF-8 原文，越界时返回错误。
 * 参数：
 *   utf8：借用的UTF-8原文。
 *   start：Unicode码点零基起点，包含，须不大于end。
 *   end：Unicode码点终点，不包含，须不超正文长度。
 * 返回：区间正文副本，空范围成功返回空串；无效编码或越界返回错误。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<std::string> codepointSlice(std::string_view utf8, std::size_t start, std::size_t end);
/*
 * 功能：校验并计数 UTF-8 码点；非空无效文本返回零。
 * 参数：
 *   utf8：借用UTF-8文本，可为空。
 * 返回：有效文本码点数；空正文和非法非空正文均为0，不可单独作为合法性校验。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
std::size_t utf8CodepointCount(std::string_view utf8);

} // namespace xuyan::domain
