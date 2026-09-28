#pragma once

#include "xuyan/package/json.h"

#include <string>
#include <string_view>
#include <vector>

namespace xuyan::application {

inline constexpr std::string_view typedCandidateSchemaVersion = "candidate-v2";
inline constexpr std::string_view typedCandidatePromptVersion = "extract-v2";

/** @brief 模型提出的类型化候选；持有字段值，不保留指向响应缓冲区的引用。 */
struct TypedExtractionCandidate {
    std::string type;
    std::string name;
    std::string quote;
    xuyan::package::JsonValue fields;
};

/** @brief 生成四个分类数组的封闭输出Schema；数量、长度及证据仍由本地校验。 */
std::string typedExtractionResponseSchema();
/** @brief 生成中文提取指令，将传入小说片段编码为不可信JSON数据，不内置素材。 */
std::string typedExtractionPrompt(std::string_view fragment);
/** @brief 验证类型字段、长度、额外属性及明确标识的引文支持，不宣称语义正确。 */
xuyan::domain::Result<bool> validateTypedCandidateFields(
    std::string_view type, const xuyan::package::JsonValue& fields,
    std::string_view quote, std::string_view name);
/** @brief 在有界解析后验证版本、四类字段及总量，整份拒绝非法输出，不产生存储副作用。 */
xuyan::domain::Result<std::vector<TypedExtractionCandidate>> parseTypedExtractionResponse(std::string_view text);

} // namespace xuyan::application
