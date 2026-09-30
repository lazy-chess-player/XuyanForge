#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace xuyan::package {

/* 拥有型 JSON 树节点，保留整数与实数类型，供包格式及厂商协议共用。
 * 默认为空值；字符串可含零字节，容器递归拥有子节点，无文件/网络句柄。
 * 未设置单节点容量上限，外部输入须经 parseJson 的深度/数量校验。
 * 可在调用线程使用；同一树并发修改须同步，访问器借用不能跨越节点重赋值或销毁。 */
class JsonValue {
public:
    /* 按输入顺序拥有子节点；解析数量受 maximum_nodes 约束，手工构造无独立上限。 */
    using Array = std::vector<JsonValue>;
    /* 按键字节排序的唯一键对象，透明比较允许 string_view 查找；解析拒绝重复键。 */
    using Object = std::map<std::string, JsonValue, std::less<>>;
    /* 七种互斥节点类型的拥有型存储，类型随构造/赋值改变。 */
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    /* 功能：构造 JSON null。参数：无。返回：完成空节点初始化，无返回值。
     * 失败：无。副作用：不分配容器或执行 I/O；节点由调用方拥有，无线程绑定。 */
    JsonValue() : value_(nullptr) {}
    /* 功能：将显式 nullptr 转为 JSON null。参数：匿名 nullptr_t 仅选择空值重载。
     * 返回：完成初始化。失败：无。副作用：无 I/O，无指针拥有关系。 */
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    /* 功能：构造布尔节点。参数：value 为 true/false，复制保存。
     * 返回：完成初始化。失败：无。副作用：无 I/O，独立值可在调用线程使用。 */
    JsonValue(bool value) : value_(value) {}
    /* 功能：将 int 精确扩展为整数节点。参数：value 为任意 int，不赋予业务单位。
     * 返回：完成 64 位整数初始化。失败：无溢出。副作用：无 I/O 或外部引用。 */
    JsonValue(int value) : value_(static_cast<std::int64_t>(value)) {}
    /* 功能：构造有符号整数节点。参数：value 为任意 int64_t，单位由业务协议定义。
     * 返回：完成初始化。失败：无。副作用：无 I/O、无线程绑定。 */
    JsonValue(std::int64_t value) : value_(value) {}
    /* 功能：构造实数节点。参数：value 为 double；非有限值可暂存但 writeJson 拒绝输出。
     * 返回：完成初始化。失败：无校验。副作用：不进行数值转换或 I/O。 */
    JsonValue(double value) : value_(value) {}
    /* 功能：构造拥有型字符串节点。参数：value 按值接收 UTF-8 字节，可空/含零字节，编码不在此校验。
     * 返回：完成初始化。失败：实参复制可能分配失败。副作用：移入本节点，不保留输入引用。 */
    JsonValue(std::string value) : value_(std::move(value)) {}
    /* 功能：复制 C 字符串为节点。参数：value 必须非 nullptr、以零结尾，允许指向空串；借用至构造结束，首个零结束复制。
     * 返回：完成字符串初始化。失败：违反指针前置条件为未定义行为；分配异常传播。
     * 副作用：分配独立内容，不获取原指针所有权。 */
    JsonValue(const char* value) : value_(std::string(value)) {}
    /* 功能：构造数组节点。参数：value 按值接收有序子节点，可空，不限制深度。
     * 返回：完成初始化。失败：实参复制可能抛分配异常。
     * 副作用：转入容器所有权；不保留调用方引用，不执行 I/O。 */
    JsonValue(Array value) : value_(std::move(value)) {}
    /* 功能：构造对象节点。参数：value 按值接收唯一键映射，可空，键编码由调用方保证。
     * 返回：完成初始化。失败：实参复制可能抛分配异常。
     * 副作用：转入映射所有权；不保留外部引用，无线程切换。 */
    JsonValue(Object value) : value_(std::move(value)) {}

    /* 功能：查询 null 类型。参数：无。返回：存储为空值时为真。
     * 失败：无。副作用：只读，不将空串/空容器视为空值；与修改串行。 */
    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    /* 功能：查询布尔类型。参数：无。返回：存储为 bool 时为真，不做数字转换。
     * 失败：无。副作用：只读节点；与修改串行。 */
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(value_); }
    /* 功能：查询整数类型。参数：无。返回：存储为 int64_t 时为真，整数形式的 double 仍为假。
     * 失败：无。副作用：只读；与修改串行。 */
    [[nodiscard]] bool isInteger() const { return std::holds_alternative<std::int64_t>(value_); }
    /* 功能：查询实数类型。参数：无。返回：存储为 double 时为真，不验证是否有限。
     * 失败：无。副作用：只读；与修改串行。 */
    [[nodiscard]] bool isReal() const { return std::holds_alternative<double>(value_); }
    /* 功能：查询两种数值类型。参数：无。返回：整数或实数为真，数字字符串为假。
     * 失败：无。副作用：只读，无隐式转换；与修改串行。 */
    [[nodiscard]] bool isNumber() const { return isInteger() || isReal(); }
    /* 功能：查询字符串类型。参数：无。返回：存储为字符串时为真，包括空串。
     * 失败：无。副作用：只读，不检查编码；与修改串行。 */
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(value_); }
    /* 功能：查询数组类型。参数：无。返回：存储为数组时为真，包括空数组。
     * 失败：无。副作用：只读，不递归遍历；与修改串行。 */
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(value_); }
    /* 功能：查询对象类型。参数：无。返回：存储为映射时为真，包括空对象。
     * 失败：无。副作用：只读，不校验键内容；与修改串行。 */
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(value_); }

    /* 功能：借用布尔值。参数：无，调用前需 isBool() 为真。返回：只读引用，随节点生命周期。
     * 失败：类型错误抛 bad_variant_access。副作用：只读；节点重赋值后引用失效。 */
    const bool& boolean() const { return std::get<bool>(value_); }
    /* 功能：借用整数值，不将实数截断。参数：无，需 isInteger() 为真。
     * 返回：只读 int64_t 引用。失败：类型错误抛 bad_variant_access。
     * 副作用：只读；引用不能跨越节点重赋值、移动或销毁。 */
    const std::int64_t& integer() const { return std::get<std::int64_t>(value_); }
    /* 功能：借用实数值，不将整数提升。参数：无，需 isReal() 为真。
     * 返回：只读 double 引用，有限性由调用方判定。失败：类型错误抛 bad_variant_access。
     * 副作用：只读；引用随节点有效，与修改串行。 */
    const double& real() const { return std::get<double>(value_); }
    /* 功能：借用完整字符串字节。参数：无，需 isString() 为真。
     * 返回：只读字符串引用，可空或含零字节。失败：类型错误抛 bad_variant_access。
     * 副作用：不复制；引用不能跨越节点重赋值/销毁，与修改串行。 */
    const std::string& string() const { return std::get<std::string>(value_); }
    /* 功能：借用有序子节点。参数：无，需 isArray() 为真。返回：只读数组引用，可空。
     * 失败：类型错误抛 bad_variant_access。副作用：不复制；引用随节点，元素引用另受容器失效规则限制。 */
    const Array& array() const { return std::get<Array>(value_); }
    /* 功能：借用唯一键映射。参数：无，需 isObject() 为真。返回：只读映射引用，可空。
     * 失败：类型错误抛 bad_variant_access。副作用：不复制、不插入键；节点重赋值后引用失效。 */
    const Object& object() const { return std::get<Object>(value_); }
    /* 功能：允许调用方编辑数组。参数：无，需 isArray() 为真。返回：本节点拥有的可写数组引用。
     * 失败：类型错误抛 bad_variant_access。副作用：后续编辑改变本树，扩容使元素引用失效；调用方负责同步/上限。 */
    Array& array() { return std::get<Array>(value_); }
    /* 功能：允许调用方编辑对象。参数：无，需 isObject() 为真。返回：本节点拥有的可写映射引用。
     * 失败：类型错误抛 bad_variant_access。副作用：编辑改变本树，删键使对应指针失效；调用方负责同步/编码。 */
    Object& object() { return std::get<Object>(value_); }

    /* 功能：精确查找对象键且不插入。参数：key 为调用内有效的键字节视图，可空。
     * 返回：子节点观察指针；非对象/键缺失为 nullptr，删键或父节点重赋值/销毁后失效。
     * 失败：无业务异常。副作用：只读映射；必须与修改串行。 */
    const JsonValue* find(std::string_view key) const;

private:
    /* 当前类型与其拥有的内容，默认 null；构造/赋值及可写访问器修改，全部访问器读取。 */
    Storage value_;
};

/* 功能：严格解析唯一 JSON 根值，拒绝重复键和非法 UTF-8。
 * 参数：input 为调用内有效的 UTF-8 字节视图，可空但空输入失败；
 *       maximum_depth 为根值从 0 起的最大递归层次，默认 64；空根容器允许深度 0；
 *       maximum_nodes 为包括根和所有值的最大节点数（不含对象键），默认 100000，0 拒绝任何值。
 * 返回：成功为独立拥有的树，JSON null 是成功；失败为中文领域错误。
 * 失败：编码、语法、重复键、数字溢出或资源限额错误；编码错误沿用来源文本校验错误；分配异常向外传播。
 * 副作用：只读输入；编码预检会分配临时规范化文本，未设置输入字节上限，调用方先限制报文大小。
 * 线程：调用线程同步解析，不保存输入视图；解析原始 input 而非规范化副本，开头 BOM 仍会被语法拒绝，
 *       字符串内原始 CR/LF 按控制字节拒绝，转义出的 CR/LF 保持原值；不要为外部输入无界放大深度限制。 */
xuyan::domain::Result<JsonValue> parseJson(std::string_view input,
                                           std::size_t maximum_depth = 64,
                                           std::size_t maximum_nodes = 100000);
/* 功能：按节点类型输出紧凑 JSON，对象键按字节排序，保留浮点往返精度。
 * 参数：value 为调用内借用的树，字符串/键需合法 UTF-8，递归深度由调用方限制。
 * 返回：拥有型 JSON 字节串。失败：非有限实数或格式化失败抛中文 runtime_error，分配异常传播；
 *       字符串/键编码不在本函数校验，违反编码前置条件可能输出非法 JSON。
 * 副作用：只读树、分配输出，无 I/O；无独立节点/字节上限，不跨线程保存引用。 */
std::string writeJson(const JsonValue& value);
/* 功能：转义双引号、反斜杠和控制字节并加两端引号。
 * 参数：value 为调用内有效的 UTF-8 字节视图，可空/含零字节，编码由调用方保证。
 * 返回：拥有型字符串字面量，空输入为两端引号。
 * 失败：分配异常传播，不主动校验编码。副作用：只读输入，无 I/O 或保留引用。 */
std::string jsonEscape(std::string_view value);

} // namespace xuyan::package
