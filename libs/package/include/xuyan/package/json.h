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
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object>;

    JsonValue() : value_(nullptr) {}
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    JsonValue(bool value) : value_(value) {}
    JsonValue(int value) : value_(static_cast<std::int64_t>(value)) {}
    JsonValue(std::int64_t value) : value_(value) {}
    JsonValue(std::string value) : value_(std::move(value)) {}
    JsonValue(const char* value) : value_(std::string(value)) {}
    JsonValue(Array value) : value_(std::move(value)) {}
    JsonValue(Object value) : value_(std::move(value)) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(value_); }
    [[nodiscard]] bool isInteger() const { return std::holds_alternative<std::int64_t>(value_); }
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(value_); }
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(value_); }
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(value_); }

    const bool& boolean() const { return std::get<bool>(value_); }
    const std::int64_t& integer() const { return std::get<std::int64_t>(value_); }
    const std::string& string() const { return std::get<std::string>(value_); }
    const Array& array() const { return std::get<Array>(value_); }
    const Object& object() const { return std::get<Object>(value_); }
    Array& array() { return std::get<Array>(value_); }
    Object& object() { return std::get<Object>(value_); }

    const JsonValue* find(std::string_view key) const;

private:
    Storage value_;
};

xuyan::domain::Result<JsonValue> parseJson(std::string_view input,
                                           std::size_t maximum_depth = 64,
                                           std::size_t maximum_nodes = 100000);
std::string writeJson(const JsonValue& value);
std::string jsonEscape(std::string_view value);

} // namespace xuyan::package

