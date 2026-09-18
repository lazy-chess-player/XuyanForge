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
    explicit SseParser(std::size_t maximum_buffer_bytes = 1024 * 1024);

    xuyan::domain::Result<std::vector<SseEvent>> feed(std::string_view bytes);
    xuyan::domain::Result<std::vector<SseEvent>> finish();
    [[nodiscard]] bool terminated() const noexcept { return terminated_; }

private:
    xuyan::domain::Result<std::vector<SseEvent>> parseAvailable(bool end_of_stream);
    xuyan::domain::Result<SseEvent> parseFrame(std::string_view frame);

    std::string buffer_;
    std::size_t maximum_buffer_bytes_;
    bool terminated_{false};
};

} // namespace xuyan::providers

