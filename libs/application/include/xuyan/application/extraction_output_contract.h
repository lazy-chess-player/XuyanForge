#pragma once

#include "xuyan/package/json.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::application {

inline constexpr std::string_view previousTypedCandidateSchemaVersion = "candidate-v2";
inline constexpr std::string_view previousTypedCandidatePromptVersion = "extract-v2";
inline constexpr std::string_view typedCandidateSchemaVersion = "candidate-v3";
inline constexpr std::string_view typedCandidatePromptVersion = "extract-v3";
inline constexpr std::size_t typedCandidateGroupMaximum = 24;
inline constexpr std::size_t typedCandidateMaximum = 48;

/** @brief 判断Schema与提示词是否构成仍可审核的类型化候选协议组合。 */
inline constexpr bool isSupportedTypedCandidateProtocol(
    std::string_view schema_version, std::string_view prompt_version) noexcept {
    return (schema_version == previousTypedCandidateSchemaVersion
            && prompt_version == previousTypedCandidatePromptVersion)
        || (schema_version == typedCandidateSchemaVersion
            && prompt_version == typedCandidatePromptVersion);
}

/** @brief 返回指定类型化协议的单步候选上限，不支持的组合返回零。 */
inline constexpr std::size_t typedCandidateMaximumFor(
    std::string_view schema_version, std::string_view prompt_version) noexcept {
    if (schema_version == previousTypedCandidateSchemaVersion
        && prompt_version == previousTypedCandidatePromptVersion) return 5;
    if (schema_version == typedCandidateSchemaVersion
        && prompt_version == typedCandidatePromptVersion) return typedCandidateMaximum;
    return 0;
}

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
/** @brief 验证根协议和总量，隔离无效单项；全部单项无效时失败，不产生存储副作用。 */
xuyan::domain::Result<std::vector<TypedExtractionCandidate>> parseTypedExtractionResponse(
    std::string_view text, std::size_t* rejected_candidates = nullptr);

} // namespace xuyan::application
