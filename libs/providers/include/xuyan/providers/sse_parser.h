#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::providers {

struct SseEvent {
    std::string event;
    std::string data;
    std::string id;
    bool done{false};
};

class SseParser {
public:
    /** @brief 创建具有缓存上限的增量事件流解析器。 */
    explicit SseParser(std::size_t maximum_buffer_bytes = 1024 * 1024);

    /** @brief 消费一段字节并返回已完整解析的事件。 */
    xuyan::domain::Result<std::vector<SseEvent>> feed(std::string_view bytes);
    /** @brief 结束输入并处理缓冲区中的最后一帧。 */
    xuyan::domain::Result<std::vector<SseEvent>> finish();
    /** @brief 判断流是否已收到终止标记。 */
    [[nodiscard]] bool terminated() const noexcept { return terminated_; }

private:
    /** @brief 从当前缓冲区提取完整事件，按需处理流结束。 */
    xuyan::domain::Result<std::vector<SseEvent>> parseAvailable(bool end_of_stream);
    /** @brief 解析单个事件帧的类型、标识与数据。 */
    xuyan::domain::Result<SseEvent> parseFrame(std::string_view frame);

    std::string buffer_;
    std::size_t maximum_buffer_bytes_;
    bool terminated_{false};
};

} // namespace xuyan::providers
