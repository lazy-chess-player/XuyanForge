#include "xuyan/package/json.h"
#include "xuyan/domain/source_document.h"

#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace xuyan::package {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;

/* 功能：包装不包含输入正文的 JSON 错误。参数：message 为按值接收的中文原因。
 * 返回：不可自动重试的校验错误。失败：分配异常传播。
 * 副作用：只分配错误值，不记录正文，在调用线程执行。 */
Error jsonError(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "修正 JSON 后重试"};
}

/* 功能：把已确认合法的 Unicode 标量追加为 UTF-8。
 * 参数：output 为输出字符串引用，原内容保留；codepoint 为 0—0x10ffff 的非代理项标量。
 * 返回：无。失败：分配异常传播；不再次校验标量范围。
 * 副作用：增加 output 的字节内容，可能使其旧视图失效；不保存引用，同步执行。 */
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

/* 单份 JSON 的同步递归下降解析器，借用输入并拥有位置/资源计数。
 * 仅由 parseJson 在编码预检后创建，运行一次后销毁；失败不回滚游标，不用于恢复或并发解析。 */
class Parser {
public:
    /* 功能：建立一次解析状态。参数：input 为编码已校验且存活至 run 返回的视图；
     *       depth 为从根 0 起的递归上限；nodes 为包含根的值节点上限，0 拒绝根。
     * 返回：完成初始化。失败：无校验。副作用：不复制输入，游标/计数从 0 开始，无 I/O。 */
    Parser(std::string_view input, std::size_t depth, std::size_t nodes)
        : input_(input), maximum_depth_(depth), maximum_nodes_(nodes) {}

    /* 功能：从初始游标解析唯一根值，末尾仅允许空白。参数：无。
     * 返回：拥有型根节点或中文校验错误。失败：语法/上限/数字错误经 Result，分配异常传播。
     * 副作用：推进游标和累计节点；失败状态不重置，全部工作在调用线程。 */
    xuyan::domain::Result<JsonValue> run() {
        auto result = value(0);
        if (!result.ok()) return result;
        whitespace();
        if (position_ != input_.size()) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 根值后存在多余内容"));
        return result;
    }

private:
    /* 功能：跳过当前游标的空格、换行、回车及制表符。参数：无。返回：无。
     * 失败：无，到输入末尾即停止。副作用：只推进字节游标，不将其他 Unicode 空白当 JSON 分隔符。 */
    void whitespace() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n'
               || input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
    }

    /* 功能：核对资源额度并分派当前 JSON 值。
     * 参数：depth 为本值从根 0 起的递归层次，不是字节或容器容量。
     * 返回：拥有型值节点或错误。失败：超深度/节点数、输入结束或非法 token；分配异常传播。
     * 副作用：递增累计值数量、推进游标；失败不回滚，键字符串不单独计入节点数。 */
    xuyan::domain::Result<JsonValue> value(std::size_t depth) {
        whitespace();
        if (nodes_ >= maximum_nodes_) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 节点数量超过上限"));
        ++nodes_;
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
        if (token == '-' || std::isdigit(static_cast<unsigned char>(token))) return number();
        return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 包含无效值"));
    }

    /* 功能：解码引号内的字符串，包括转义与成对 UTF-16 代理项。
     * 参数：无，前置条件为游标指向已确认存在的双引号；原始非转义 UTF-8 已全局预检。
     * 返回：拥有型 UTF-8 字节串，可空或含解码后的零字节。
     * 失败：控制字节、未知/截断转义、代理项错误及未闭合引号；分配异常传播。
     * 副作用：消耗字符串字节，失败不恢复游标，不持有返回串的引用。 */
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

    /* 功能：解码紧随反斜杠 u 的四位十六进制单元。参数：无，游标已越过 u。
     * 返回：0—65535 的 UTF-16 单元，代理项合法性由 string 处理。
     * 失败：不足四字节或非十六进制返回校验错误。副作用：消耗字节，失败不回滚。 */
    xuyan::domain::Result<std::uint32_t> hexCodepoint() {
        if (input_.size() - position_ < 4) return xuyan::domain::Result<std::uint32_t>::failure(jsonError("Unicode 转义被截断"));
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

    /* 功能：解析负号、整数、小数及指数，严格区分整数/实数类型。
     * 参数：无，游标须指向负号或十进制数字。返回：int64_t 或有限 double 节点。
     * 失败：缺数字、小数/指数不完整、数字超范围返回错误；分配异常传播。
     * 副作用：推进字节游标；前导零后的多余数字由外层边界校验拒绝，不使用本地化数字格式。 */
    xuyan::domain::Result<JsonValue> number() {
        const auto begin = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_])))
            return xuyan::domain::Result<JsonValue>::failure(jsonError("无效 JSON 数字"));
        if (input_[position_] == '0') ++position_;
        else while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        bool is_real = false;
        if (position_ < input_.size() && input_[position_] == '.') {
            is_real = true; ++position_;
            const auto fraction_begin = position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
            if (position_ == fraction_begin) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 小数点后缺少数字"));
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            is_real = true; ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const auto exponent_begin = position_;
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
            if (position_ == exponent_begin) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 指数缺少数字"));
        }
        if (is_real) {
            double parsed = 0;
            const auto result = std::from_chars(input_.data() + begin, input_.data() + position_, parsed,
                                                std::chars_format::general);
            if (result.ec != std::errc{} || result.ptr != input_.data() + position_ || !std::isfinite(parsed))
                return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 浮点数超出范围"));
            return xuyan::domain::Result<JsonValue>::success(JsonValue(parsed));
        }
        std::int64_t parsed = 0;
        const auto result = std::from_chars(input_.data() + begin, input_.data() + position_, parsed);
        if (result.ec != std::errc{}) return xuyan::domain::Result<JsonValue>::failure(jsonError("JSON 整数超出范围"));
        return xuyan::domain::Result<JsonValue>::success(JsonValue(parsed));
    }

    /* 功能：解析当前方括号数组。参数：depth 为其直接子节点的层次，调用前游标指向 '['。
     * 返回：拥有型有序数组，空数组成功。失败：子节点错误、缺逗号/闭合符；分配异常传播。
     * 副作用：推进游标并累计子节点；半成品仅在本地，失败不会返回部分数组。 */
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

    /* 功能：解析当前花括号对象，保留唯一键。
     * 参数：depth 为直接成员值的层次，调用前游标指向 '{'。
     * 返回：拥有型排序映射，空对象成功；解码后相同键视为重复。
     * 失败：键/冒号/逗号/闭合错误、重复键或子节点错误；分配异常传播。
     * 副作用：推进游标和节点计数，不将半成品暴露给调用方。 */
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

    /* 借用编码已校验的原始字节，构造时绑定；parseJson 返回前必须持续有效，不被修改。 */
    std::string_view input_;
    /* 下一个待解析字节偏移，初始 0；各解析函数推进，始终不大于输入大小。 */
    std::size_t position_{0};
    /* 根从 0 起的允许层次，构造时设定；递归分派只读，不在失败后扩大。 */
    std::size_t maximum_depth_;
    /* 本次允许的总值节点数，构造时设定；value 读取，键不计数。 */
    std::size_t maximum_nodes_;
    /* 已开始解析的值数量，初始 0；value 递增，失败也保留已消耗额度，不回绕。 */
    std::size_t nodes_{0};
};

/* 功能：递归追加单个节点的紧凑 JSON 表示。
 * 参数：value 为调用期间只读树；output 为输出引用，保留已有前缀，不能别名 value 中的字符串。
 * 返回：无。失败：非有限数、格式化失败抛中文 runtime_error，分配异常传播；异常后 output 可能仅有前缀。
 * 副作用：追加字节并递归访问树，无独立深度上限，由调用方保证树有界；不执行 I/O。 */
void writeValue(const JsonValue& value, std::string& output) {
    if (value.isNull()) output += "null";
    else if (value.isBool()) output += value.boolean() ? "true" : "false";
    else if (value.isInteger()) output += std::to_string(value.integer());
    else if (value.isReal()) {
        if (!std::isfinite(value.real())) throw std::runtime_error("JSON 实数必须是有限值");
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.real(),
                                          std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (result.ec != std::errc{}) throw std::runtime_error("无法序列化 JSON 浮点数");
        output.append(buffer, result.ptr);
    }
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
