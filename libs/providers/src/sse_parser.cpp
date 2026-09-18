#include "xuyan/providers/sse_parser.h"

#include <algorithm>

namespace xuyan::providers {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

Error incomplete(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "保留原始响应并将请求标记为输出不完整"};
}

bool validUtf8(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) {
            length = 1;
            codepoint = first;
        } else if ((first & 0xe0) == 0xc0) {
            length = 2;
            codepoint = first & 0x1f;
        } else if ((first & 0xf0) == 0xe0) {
            length = 3;
            codepoint = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            length = 4;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (index + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800)
            || (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
        index += length;
    }
    return true;
}

} // namespace

SseParser::SseParser(std::size_t maximum_buffer_bytes) : maximum_buffer_bytes_(maximum_buffer_bytes) {}

xuyan::domain::Result<std::vector<SseEvent>> SseParser::feed(std::string_view bytes) {
    if (terminated_) {
        return xuyan::domain::Result<std::vector<SseEvent>>::failure(incomplete("终结事件之后收到了额外数据"));
    }
    if (buffer_.size() + bytes.size() > maximum_buffer_bytes_) {
        return xuyan::domain::Result<std::vector<SseEvent>>::failure(incomplete("SSE 增量缓冲超过配置上限"));
    }
    buffer_.append(bytes);
    return parseAvailable(false);
}

xuyan::domain::Result<std::vector<SseEvent>> SseParser::finish() {
    return parseAvailable(true);
}

xuyan::domain::Result<std::vector<SseEvent>> SseParser::parseAvailable(bool end_of_stream) {
    std::vector<SseEvent> events;
    for (;;) {
        const auto lf = buffer_.find("\n\n");
        const auto crlf = buffer_.find("\r\n\r\n");
        std::size_t boundary = std::string::npos;
        std::size_t delimiter = 0;
        if (lf != std::string::npos && (crlf == std::string::npos || lf < crlf)) {
            boundary = lf;
            delimiter = 2;
        } else if (crlf != std::string::npos) {
            boundary = crlf;
            delimiter = 4;
        }
        if (boundary == std::string::npos) break;

        auto parsed = parseFrame(std::string_view(buffer_).substr(0, boundary));
        buffer_.erase(0, boundary + delimiter);
        if (!parsed.ok()) return xuyan::domain::Result<std::vector<SseEvent>>::failure(*parsed.error);
        if (!parsed.value->event.empty() || !parsed.value->data.empty() || parsed.value->done) {
            terminated_ = terminated_ || parsed.value->done;
            events.push_back(std::move(*parsed.value));
        }
    }

    if (end_of_stream && !buffer_.empty()) {
        return xuyan::domain::Result<std::vector<SseEvent>>::failure(incomplete("SSE 响应在事件边界之前结束"));
    }
    return xuyan::domain::Result<std::vector<SseEvent>>::success(std::move(events));
}

xuyan::domain::Result<SseEvent> SseParser::parseFrame(std::string_view frame) {
    SseEvent result;
    std::size_t cursor = 0;
    bool first_data = true;
    while (cursor <= frame.size()) {
        const auto newline = frame.find('\n', cursor);
        const auto end = newline == std::string::npos ? frame.size() : newline;
        auto line = frame.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        cursor = newline == std::string::npos ? frame.size() + 1 : newline + 1;
        if (line.empty() || line.front() == ':') continue;

        const auto colon = line.find(':');
        const auto field = line.substr(0, colon);
        auto value = colon == std::string::npos ? std::string_view{} : line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        if (field == "event") result.event.assign(value);
        else if (field == "id") result.id.assign(value);
        else if (field == "data") {
            if (!first_data) result.data.push_back('\n');
            result.data.append(value);
            first_data = false;
        }
    }
    if (!validUtf8(result.data) || !validUtf8(result.event) || !validUtf8(result.id)) {
        return xuyan::domain::Result<SseEvent>::failure(incomplete("SSE 事件包含无效或被截断的 UTF-8"));
    }
    if (result.data == "[DONE]") {
        result.done = true;
        result.event = "done";
    }
    return xuyan::domain::Result<SseEvent>::success(std::move(result));
}

} // namespace xuyan::providers

