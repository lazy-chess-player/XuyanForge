#include "xuyan/domain/hash.h"
#include "xuyan/domain/scenario.h"
#include "xuyan/domain/source_document.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
/* 功能：检查领域边界的回归条件，失败时停止当前用例。
 * 参数：condition为应成立的断言；message为借用的中文失败说明，调用期间有效。
 * 返回：无。失败：断言不成立抛runtime_error。副作用：不写外部资源。
 */
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

/* 功能：验证章节检测和零长度切片均拒绝非法编码，合法空正文仍可读取。
 * 参数：无。返回：无。失败：契约偏离时抛断言异常。
 * 副作用：仅生成测试内存字节，不导入小说或创建工作区。
 */
void testInvalidUtf8Boundaries() {
    const std::string invalid(1, static_cast<char>(0xff));
    require(!xuyan::domain::detectChapters(invalid, "boundary-document").ok(), "非法编码不能生成章节索引");
    require(!xuyan::domain::codepointSlice(invalid, 0, 0).ok(), "非法编码的空范围也必须失败");
    const auto empty = xuyan::domain::codepointSlice({}, 0, 0);
    require(empty.ok() && empty.value->empty(), "合法空正文必须保留空范围语义");
}

/* 功能：检查章前正文、中文与四字节码点构成的连续双锚点，不把字节当成字符。
 * 参数：无。返回：无。失败：范围断裂或计数错误时抛断言异常。
 * 副作用：只在本测试函数内构造短文本，不链接到正式程序。
 */
void testContiguousChapterAnchors() {
    const std::string text = "前文\n# 段落\n文😀\n## 后段\n结尾";
    const auto chapters = xuyan::domain::detectChapters(text, "anchor-document");
    require(chapters.ok() && chapters.value->size() == 3, "章前正文与两个标题须分成三个连续区间");
    std::size_t next_byte = 0;
    std::size_t next_codepoint = 0;
    for (const auto& chapter : *chapters.value) {
        require(chapter.start_byte == next_byte && chapter.start_codepoint == next_codepoint, "章节锚点必须连续");
        const auto slice = xuyan::domain::codepointSlice(text, chapter.start_codepoint, chapter.end_codepoint);
        require(slice.ok() && *slice.value == text.substr(chapter.start_byte, chapter.end_byte - chapter.start_byte),
                "字节与码点定位必须回到相同原文");
        next_byte = chapter.end_byte;
        next_codepoint = chapter.end_codepoint;
    }
    require(next_byte == text.size() && next_codepoint == xuyan::domain::utf8CodepointCount(text), "索引必须覆盖全文");
}

/* 功能：阻止未知操作和有符号计数溢出产生成功状态，输入状态始终不变。
 * 参数：无。返回：无。失败：错误操作被接受或输入变化时抛断言异常。
 * 副作用：仅使用内存中的显式测试人物；无数据库、文件或模型请求。
 */
void testOperationBoundaries() {
    xuyan::domain::ScenarioState state;
    state.characters.push_back({"boundary-actor", "行动者", false, true, 0});
    state.characters.push_back({"boundary-target", "接收者", false, false, std::numeric_limits<int>::max()});
    const auto before = xuyan::domain::stateHash(state);
    auto operation = xuyan::domain::ProposedOperation{
        static_cast<xuyan::domain::OperationType>(100), "boundary-actor", "boundary-target", false};
    require(!xuyan::domain::applyOperation(state, operation).ok(), "未知操作不能推进修订");
    operation.type = xuyan::domain::OperationType::reveal_seal_forgery;
    require(!xuyan::domain::applyOperation(state, operation).ok(), "信任计数上溢必须拒绝");
    require(before == xuyan::domain::stateHash(state), "失败操作不能修改输入状态");
    state.revision = std::numeric_limits<int>::max();
    require(!xuyan::domain::applyOperation(state, operation).ok(), "修订上溢必须拒绝");
}
} // namespace

/* 功能：执行不依赖存储的领域边界回归。
 * 参数：无。返回：全部通过为0，任何标准异常为1。
 * 失败：捕获异常并显示中文原因。副作用：只向控制台输出，不访问用户数据。
 */
int main() {
    try {
        testInvalidUtf8Boundaries();
        testContiguousChapterAnchors();
        testOperationBoundaries();
        std::cout << "领域边界回归通过\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "领域边界回归失败：" << exception.what() << '\n';
        return 1;
    }
}
