#pragma once

#include "xuyan/domain/scenario.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace xuyan::package {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    /** @brief 构造空值节点。 */
    JsonValue() : value_(nullptr) {}
    /** @brief 从显式空值构造节点。 */
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    /** @brief 从布尔值构造节点。 */
    JsonValue(bool value) : value_(value) {}
    /** @brief 从普通整数构造 64 位整数节点。 */
    JsonValue(int value) : value_(static_cast<std::int64_t>(value)) {}
    /** @brief 从 64 位整数构造节点。 */
    JsonValue(std::int64_t value) : value_(value) {}
    /** @brief 从实数构造节点。 */
    JsonValue(double value) : value_(value) {}
    /** @brief 接收字符串所有权并构造节点。 */
    JsonValue(std::string value) : value_(std::move(value)) {}
    /** @brief 复制以零结尾的字符串并构造节点。 */
    JsonValue(const char* value) : value_(std::string(value)) {}
    /** @brief 接收数组所有权并构造节点。 */
    JsonValue(Array value) : value_(std::move(value)) {}
    /** @brief 接收对象所有权并构造节点。 */
    JsonValue(Object value) : value_(std::move(value)) {}

    /** @brief 判断节点是否为空值。 */
    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    /** @brief 判断节点是否为布尔值。 */
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(value_); }
    /** @brief 判断节点是否为 64 位整数。 */
    [[nodiscard]] bool isInteger() const { return std::holds_alternative<std::int64_t>(value_); }
    /** @brief 判断节点是否为实数。 */
    [[nodiscard]] bool isReal() const { return std::holds_alternative<double>(value_); }
    /** @brief 判断节点是否为整数或实数。 */
    [[nodiscard]] bool isNumber() const { return isInteger() || isReal(); }
    /** @brief 判断节点是否为字符串。 */
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(value_); }
    /** @brief 判断节点是否为数组。 */
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(value_); }
    /** @brief 判断节点是否为对象。 */
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(value_); }

    /** @brief 获取布尔值引用；调用前需确认节点类型。 */
    const bool& boolean() const { return std::get<bool>(value_); }
    /** @brief 获取整数引用；调用前需确认节点类型。 */
    const std::int64_t& integer() const { return std::get<std::int64_t>(value_); }
    /** @brief 获取实数引用；调用前需确认节点类型。 */
    const double& real() const { return std::get<double>(value_); }
    /** @brief 获取字符串引用；调用前需确认节点类型。 */
    const std::string& string() const { return std::get<std::string>(value_); }
    /** @brief 获取数组只读引用；调用前需确认节点类型。 */
    const Array& array() const { return std::get<Array>(value_); }
    /** @brief 获取对象只读引用；调用前需确认节点类型。 */
    const Object& object() const { return std::get<Object>(value_); }
    /** @brief 获取数组可写引用；调用前需确认节点类型。 */
    Array& array() { return std::get<Array>(value_); }
    /** @brief 获取对象可写引用；调用前需确认节点类型。 */
    Object& object() { return std::get<Object>(value_); }

    /** @brief 在对象中查找指定键，不存在或节点非对象时返回空指针。 */
    const JsonValue* find(std::string_view key) const;

private:
    Storage value_;
};

/** @brief 在深度与节点数量上限内解析 JSON 文本。 */
xuyan::domain::Result<JsonValue> parseJson(std::string_view input,
                                           std::size_t maximum_depth = 64,
                                           std::size_t maximum_nodes = 100000);
/** @brief 将 JSON 节点序列化为紧凑文本。 */
std::string writeJson(const JsonValue& value);
/** @brief 转义文本并生成包含两端引号的 JSON 字符串字面量。 */
std::string jsonEscape(std::string_view value);

} // namespace xuyan::package
