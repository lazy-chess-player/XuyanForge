#include "xuyan/package/json.h"
#include "xuyan/domain/source_document.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>

namespace xuyan::package {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

Error jsonError(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "修正 JSON 后重试"};
}

void appendUtf8(std::string& output, std::uint32_t codepoint) {
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

class Parser {
public:
    Parser(std::string_view input, std::size_t depth, std::size_t nodes)
        : input_(input), maximum_depth_(depth), maximum_nodes_(nodes) {}

    xuyan::domain::Result<JsonValue> run() {
        auto result = value(0);
        if (!result.ok()) return result;
        whitespace();
        if (position_ != input_.size()) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 根值后存在多余内容"));
        return result;
    }

private:
    void whitespace() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n'
               || input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
    }

    xuyan::domain::Result<JsonValue> value(std::size_t depth) {
        whitespace();
        if (++nodes_ > maximum_nodes_) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 节点数量超过上限"));
        if (depth > maximum_depth_) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 嵌套深度超过上限"));
        if (position_ >= input_.size()) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 意外结束"));
        const auto token = input_[position_];
        if (token == '"') {
            auto text = string();
            if (!text.ok()) return xuyan::domain::Result<JsonValue>::failure(*text.error);
            return xuyan::domain::Result<JsonValue>::success(JsonValue(std::move(*text.value)));
        }
        if (token == '{') return object(depth + 1);
        if (token == '[') return array(depth + 1);
        if (input_.substr(position_, 4) == "true") { position_ += 4; return xuyan::domain::Result<JsonValue>::success(JsonValue(true)); }
        if (input_.substr(position_, 5) == "false") { position_ += 5; return xuyan::domain::Result<JsonValue>::success(JsonValue(false)); }
        if (input_.substr(position_, 4) == "null") { position_ += 4; return xuyan::domain::Result<JsonValue>::success(JsonValue(nullptr)); }
        if (token == '-' || std::isdigit(static_cast<unsigned char>(token))) return integer();
        return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 包含无效值"));
    }

    xuyan::domain::Result<std::string> string() {
        if (input_[position_++] != '"') return xuyan::domain::Result<std::string>::failure(jsonError("需要 JSON 字符串"));
        std::string output;
        while (position_ < input_.size()) {
            const auto character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return xuyan::domain::Result<std::string>::success(std::move(output));
            if (character < 0x20) return xuyan::domain::Result<std::string>::failure(jsonError("JSON 字符串包含控制字符"));
            if (character != '\\') { output.push_back(static_cast<char>(character)); continue; }
            if (position_ >= input_.size()) return xuyan::domain::Result<std::string>::failure(jsonError("JSON 转义被截断"));
            const auto escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break; case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break; case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break; case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break; case 't': output.push_back('\t'); break;
            case 'u': {
                auto unicode = hexCodepoint();
                if (!unicode.ok()) return xuyan::domain::Result<std::string>::failure(*unicode.error);
                std::uint32_t codepoint = *unicode.value;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_.substr(position_, 2) != "\\u")
                        return xuyan::domain::Result<std::string>::failure(jsonError("高代理项缺少低代理项"));
                    position_ += 2;
                    auto low = hexCodepoint();
                    if (!low.ok() || *low.value < 0xdc00 || *low.value > 0xdfff)
                        return xuyan::domain::Result<std::string>::failure(jsonError("无效 UTF-16 代理项"));
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (*low.value - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    return xuyan::domain::Result<std::string>::failure(jsonError("孤立的低代理项"));
                }
                appendUtf8(output, codepoint); break;
            }
            default: return xuyan::domain::Result<std::string>::failure(jsonError("未知 JSON 转义"));
            }
        }
        return xuyan::domain::Result<std::string>::failure(jsonError("JSON 字符串未闭合"));
    }

    xuyan::domain::Result<std::uint32_t> hexCodepoint() {
        if (position_ + 4 > input_.size()) return xuyan::domain::Result<std::uint32_t>::failure(jsonError("Unicode 转义被截断"));
        std::uint32_t result = 0;
        for (int index = 0; index < 4; ++index) {
            const auto character = input_[position_++];
            result <<= 4;
            if (character >= '0' && character <= '9') result |= character - '0';
            else if (character >= 'a' && character <= 'f') result |= character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') result |= character - 'A' + 10;
            else return xuyan::domain::Result<std::uint32_t>::failure(jsonError("无效 Unicode 转义"));
        }
        return xuyan::domain::Result<std::uint32_t>::success(result);
    }

    xuyan::domain::Result<JsonValue> integer() {
        const auto begin = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_])))
            return xuyan::domain::Result<JsonValue>::failure(jsonError("无效 JSON 数字"));
        if (input_[position_] == '0') ++position_;
        else while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        if (position_ < input_.size() && (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E'))
            return xuyan::domain::Result<JsonValue>::failure(jsonError("协议字段不接受浮点数"));
        std::int64_t parsed = 0;
        const auto result = std::from_chars(input_.data() + begin, input_.data() + position_, parsed);
        if (result.ec != std::errc{}) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 整数超出范围"));
        return xuyan::domain::Result<JsonValue>::success(JsonValue(parsed));
    }

    xuyan::domain::Result<JsonValue> array(std::size_t depth) {
        ++position_;
        JsonValue::Array result;
        whitespace();
        if (position_ < input_.size() && input_[position_] == ']') { ++position_; return xuyan::domain::Result<JsonValue>::success(JsonValue(std::move(result))); }
        for (;;) {
            auto child = value(depth);
            if (!child.ok()) return child;
            result.push_back(std::move(*child.value));
            whitespace();
            if (position_ >= input_.size()) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 数组未闭合"));
            if (input_[position_] == ']') { ++position_; break; }
            if (input_[position_++] != ',') return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 数组缺少逗号"));
        }
        return xuyan::domain::Result<JsonValue>::success(JsonValue(std::move(result)));
    }

    xuyan::domain::Result<JsonValue> object(std::size_t depth) {
        ++position_;
        JsonValue::Object result;
        whitespace();
        if (position_ < input_.size() && input_[position_] == '}') { ++position_; return xuyan::domain::Result<JsonValue>::success(JsonValue(std::move(result))); }
        for (;;) {
            whitespace();
            if (position_ >= input_.size() || input_[position_] != '"')
                return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 对象键必须是字符串"));
            auto key = string();
            if (!key.ok()) return xuyan::domain::Result<JsonValue>::failure(*key.error);
            whitespace();
            if (position_ >= input_.size() || input_[position_++] != ':')
                return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 对象键后缺少冒号"));
            auto child = value(depth);
            if (!child.ok()) return child;
            if (!result.emplace(std::move(*key.value), std::move(*child.value)).second)
                return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 对象包含重复键"));
            whitespace();
            if (position_ >= input_.size()) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 对象未闭合"));
            if (input_[position_] == '}') { ++position_; break; }
            if (input_[position_++] != ',') return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 对象缺少逗号"));
        }
        return xuyan::domain::Result<JsonValue>::success(JsonValue(std::move(result)));
    }

    std::string_view input_;
    std::size_t position_{0};
    std::size_t maximum_depth_;
    std::size_t maximum_nodes_;
    std::size_t nodes_{0};
};

void writeValue(const JsonValue& value, std::string& output) {
    if (value.isNull()) output += "null";
    else if (value.isBool()) output += value.boolean() ? "true" : "false";
    else if (value.isInteger()) output += std::to_string(value.integer());
    else if (value.isString()) output += jsonEscape(value.string());
    else if (value.isArray()) {
        output.push_back('[');
        bool first = true;
        for (const auto& item : value.array()) { if (!first) output.push_back(','); first = false; writeValue(item, output); }
        output.push_back(']');
    } else {
        output.push_back('{');
        bool first = true;
        for (const auto& [key, child] : value.object()) {
            if (!first) output.push_back(',');
            first = false;
            output += jsonEscape(key);
            output.push_back(':');
            writeValue(child, output);
        }
        output.push_back('}');
    }
}

} // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    const auto iterator = object().find(key);
    return iterator == object().end() ? nullptr : &iterator->second;
}

xuyan::domain::Result<JsonValue> parseJson(std::string_view input, std::size_t maximum_depth,
                                           std::size_t maximum_nodes) {
    auto utf8 = xuyan::domain::normalizeUtf8Text(input);
    if (!utf8.ok()) return xuyan::domain::Result<JsonValue>::failure(*utf8.error);
    Parser parser(input, maximum_depth, maximum_nodes);
    return parser.run();
}

std::string jsonEscape(std::string_view value) {
    std::string output{"\""};
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"': output += "\\\""; break; case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break; case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break; case '\r': output += "\\r"; break; case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                output += "\\u00"; output.push_back(hex[character >> 4]); output.push_back(hex[character & 0xf]);
            } else output.push_back(static_cast<char>(character));
        }
    }
    output.push_back('"');
    return output;
}

std::string writeJson(const JsonValue& value) {
    std::string output;
    writeValue(value, output);
    return output;
}

} // namespace xuyan::package
