#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::providers {

/* 一帧 SSE 的拥有型值对象；来源为已校验 UTF-8 的报文，不持有输入视图。 */
struct SseEvent {
    /* event 字段原始协议值，默认空；解析器设置，调用方映射显示标签。 */
    std::string event;
    /* 多个 data 行以换行连接的正文，默认空；字节长度受解析器单批缓冲限制。 */
    std::string data;
    /* 当前帧 id 字段，默认空；不跨帧继承、不用于自动重连。 */
    std::string id;
    /* data 是否等于协议终结标记，默认否；终结后拒绝额外数据。 */
    bool done{false};
};

/* 单流增量解析器，独占未完成帧缓存，无网络资源；调用方在同一线程串行使用。
 * 输入视图只在调用内借用，返回事件独立持有内容；销毁时释放缓存。 */
class SseParser {
public:
    /* 功能：初始化空流解析器。
     * 参数：maximum_buffer_bytes 为缓存及单次输入合计字节上限，默认 1 MiB，0 仅接受空输入。
     * 返回：完成初始化，无返回值。失败：无校验失败。副作用：不发送请求、不读取文件。
     * 线程与生命周期：无线程绑定，调用方拥有对象；一次流内须串行调用，销毁释放字节缓存。 */
    explicit SseParser(std::size_t maximum_buffer_bytes = 1024 * 1024);

    /* 功能：追加字节并提取以空行结束的完整帧。
     * 参数：bytes 为调用内有效的输入视图，可为空，允许 UTF-8 字符跨批次拆开。
     * 返回：成功为本批完整事件；只有 id 或空业务字段的帧被消费但不输出，空列表也可能表示已跳过这些帧。
     *       event 或 data 非空以及终结帧才输出；失败为校验错误。
     * 失败：缓存超限、帧编码无效、终结后有数据；分配异常可传播。解析错误后应丢弃解析器。
     * 副作用：更新缓存和终结状态；同批次失败不返回前序事件，不支持并发调用。
     * 线程与生命周期：同步执行；终结后任何 feed（包括空输入）均失败，输入视图不被保存。 */
    xuyan::domain::Result<std::vector<SseEvent>> feed(std::string_view bytes);
    /* 功能：核对输入是否在帧边界结束，不将半帧强行补成事件。
     * 参数：无。返回：成功为剩余完整帧列表，通常为空；失败为残留半帧错误。
     * 失败：缓存非空或帧解析失败；分配异常可传播。副作用：消费完整缓存，不要求已收到终结标记。
     * 线程与生命周期：与 feed 串行，同步返回；重复 finish 在缓存为空时成功，不触发重连或销毁对象。
     *       finish 本身不设置终结状态，未收到终结标记时后续 feed 仍可追加字节。 */
    xuyan::domain::Result<std::vector<SseEvent>> finish();
    /* 功能：查询是否已解析终结帧。参数：无。返回：已终结为真。
     * 失败：无。副作用：只读状态；需与 feed/finish 串行调用。 */
    [[nodiscard]] bool terminated() const noexcept { return terminated_; }

private:
    /* 功能：消费缓存中的完整帧并检查终结后残留。
     * 参数：end_of_stream 为真时拒绝最后半帧。返回：完整事件列表或校验错误。
     * 失败：编码、半帧或终结后数据错误；分配异常可传播。
     * 副作用：删除已检查帧并更新终结状态，失败不回滚缓存；调用线程同步执行，不保存参数。 */
    xuyan::domain::Result<std::vector<SseEvent>> parseAvailable(bool end_of_stream);
    /* 功能：解析单帧字段，忽略注释及未知字段，识别终结标记。
     * 参数：frame 为调用期间有效且不含分隔空行的字节视图，可空；同名 event/id 取最后一行，data 按行连接。
     * 返回：拥有内容的事件或编码错误；无业务字段的帧返回空事件。
     * 失败：最终保存的 event/data/id 字段 UTF-8 无效；忽略字段不检查编码，分配异常可传播。
     * 副作用：只读帧，不修改解析器缓存；data 为终结标记时将 event 设为内部 done 值。
     * 线程与生命周期：同步解析，字段立即复制为独立字符串，不保留缓存视图。 */
    xuyan::domain::Result<SseEvent> parseFrame(std::string_view frame);

    /* 独占尚未完成的帧字节，初始空；feed 追加，parseAvailable 消费。 */
    std::string buffer_;
    /* 构造时确定的缓存与本次输入合计字节上限，默认 1 MiB；feed 读取，生命周期内不变，不限制累计流长度。 */
    std::size_t maximum_buffer_bytes_;
    /* 是否已消费终结帧，初始否；只允许从否变为是，不自动重置。 */
    bool terminated_{false};
};

} // namespace xuyan::providers
