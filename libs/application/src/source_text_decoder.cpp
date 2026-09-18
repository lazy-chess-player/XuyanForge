#include "xuyan/application/source_text_decoder.h"

#include "xuyan/domain/source_document.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace xuyan::application {
namespace {

using xuyan::domain::ErrorCode;
using xuyan::domain::Result;

void appendUtf8(std::string& output, char32_t codepoint) {
    if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

Result<DecodedSourceText> decodeUtf16(std::string_view bytes, bool little_endian) {
    if (bytes.size() < 2 || (bytes.size() - 2) % 2 != 0) return Result<DecodedSourceText>::failure(
        {ErrorCode::validation_failed, "UTF-16 来源字节数无效", false, "检查文件是否完整"});
    std::string utf8;
    for (std::size_t offset = 2; offset < bytes.size(); offset += 2) {
        const auto first = static_cast<unsigned char>(bytes[offset]);
        const auto second = static_cast<unsigned char>(bytes[offset + 1]);
        std::uint16_t unit = little_endian ? static_cast<std::uint16_t>(first | (second << 8))
                                           : static_cast<std::uint16_t>((first << 8) | second);
        char32_t codepoint = unit;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (offset + 3 >= bytes.size()) return Result<DecodedSourceText>::failure(
                {ErrorCode::validation_failed, "UTF-16 高代理项缺少低代理项", false, "修复来源编码"});
            offset += 2;
            const auto low_first = static_cast<unsigned char>(bytes[offset]);
            const auto low_second = static_cast<unsigned char>(bytes[offset + 1]);
            const std::uint16_t low = little_endian ? static_cast<std::uint16_t>(low_first | (low_second << 8))
                                                    : static_cast<std::uint16_t>((low_first << 8) | low_second);
            if (low < 0xdc00 || low > 0xdfff) return Result<DecodedSourceText>::failure(
                {ErrorCode::validation_failed, "UTF-16 代理项无效", false, "修复来源编码"});
            codepoint = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
        } else if (unit >= 0xdc00 && unit <= 0xdfff) return Result<DecodedSourceText>::failure(
            {ErrorCode::validation_failed, "UTF-16 存在孤立低代理项", false, "修复来源编码"});
        appendUtf8(utf8, codepoint);
    }
    auto normalized = xuyan::domain::normalizeUtf8Text(utf8);
    if (!normalized.ok()) return Result<DecodedSourceText>::failure(*normalized.error);
    return Result<DecodedSourceText>::success({std::move(*normalized.value), little_endian ? "utf-16le" : "utf-16be"});
}

#ifdef _WIN32
Result<DecodedSourceText> decodeGb18030(std::string_view bytes) {
    const auto wide_count = MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_count <= 0) return Result<DecodedSourceText>::failure(
        {ErrorCode::validation_failed, "无法识别文本编码；支持 UTF-8、带 BOM 的 UTF-16 和 GB18030", false, "另存为 UTF-8 后重试"});
    std::wstring wide(static_cast<std::size_t>(wide_count), L'\0');
    MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), wide.data(), wide_count);
    const auto utf8_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_count, nullptr, 0, nullptr, nullptr);
    if (utf8_count <= 0) return Result<DecodedSourceText>::failure(
        {ErrorCode::validation_failed, "GB18030 转换失败", false, "另存为 UTF-8 后重试"});
    std::string utf8(static_cast<std::size_t>(utf8_count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), wide_count, utf8.data(), utf8_count, nullptr, nullptr);
    auto normalized = xuyan::domain::normalizeUtf8Text(utf8);
    if (!normalized.ok()) return Result<DecodedSourceText>::failure(*normalized.error);
    return Result<DecodedSourceText>::success({std::move(*normalized.value), "gb18030"});
}
#endif

} // namespace

Result<DecodedSourceText> decodeSourceText(std::string_view bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe)
        return decodeUtf16(bytes, true);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff)
        return decodeUtf16(bytes, false);
    auto utf8 = xuyan::domain::normalizeUtf8Text(bytes);
    if (utf8.ok()) return Result<DecodedSourceText>::success({std::move(*utf8.value), "utf-8"});
#ifdef _WIN32
    return decodeGb18030(bytes);
#else
    return Result<DecodedSourceText>::failure(
        {ErrorCode::validation_failed, "无法识别文本编码；当前平台支持 UTF-8 与带 BOM 的 UTF-16", false, "另存为 UTF-8 后重试"});
#endif
}

} // namespace xuyan::application
