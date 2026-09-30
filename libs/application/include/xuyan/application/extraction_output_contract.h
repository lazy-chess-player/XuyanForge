#pragma once

#include "xuyan/package/json.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xuyan::application {

/* 历史候选版本；仅支持旧资料审核，不用于新远程发送，视图指向静态字面量。 */
inline constexpr std::string_view previousTypedCandidateSchemaVersion = "candidate-v2";
/* 历史提示词版本，只与历史候选版本配对，不允许混用。 */
inline constexpr std::string_view previousTypedCandidatePromptVersion = "extract-v2";
/* 当前候选协议内部值，影响任务缓存身份，显示层须映射中文。 */
inline constexpr std::string_view typedCandidateSchemaVersion = "candidate-v3";
/* 当前提示词版本；随新任务冻结，不静默升级历史任务。 */
inline constexpr std::string_view typedCandidatePromptVersion = "extract-v3";
/* 单类上限，单位条，为 24；根响应超额拒绝而非截断。 */
inline constexpr std::size_t typedCandidateGroupMaximum = 24;
/* 单步合计上限，单位条，为 48；解析及提交共同使用。 */
inline constexpr std::size_t typedCandidateMaximum = 48;

/*
 * 功能：检查历史审核兼容的版本配对。
 * 参数：schema_version 为候选版本；prompt_version 为提示词版本，均仅同步借用。
 * 返回：旧配对或当前配对为 true，其他 false。失败：无异常路径。
 * 副作用：无；线程与生命周期：纯函数可并发，不保存视图。
 */
inline constexpr bool isSupportedTypedCandidateProtocol(
    std::string_view schema_version, std::string_view prompt_version) noexcept {
    return (schema_version == previousTypedCandidateSchemaVersion
            && prompt_version == previousTypedCandidatePromptVersion)
        || (schema_version == typedCandidateSchemaVersion
            && prompt_version == typedCandidatePromptVersion);
}

/*
 * 功能：返回协议数量限制。参数：schema_version、prompt_version 为同步借用的候选及提示词版本。
 * 返回：旧配对 5、当前配对 48、不支持 0，零不是允许无限制。失败：无异常路径。
 * 副作用：无；线程与生命周期：纯函数可并发，不保存视图。
 */
inline constexpr std::size_t typedCandidateMaximumFor(
    std::string_view schema_version, std::string_view prompt_version) noexcept {
    if (schema_version == previousTypedCandidateSchemaVersion
        && prompt_version == previousTypedCandidatePromptVersion) return 5;
    if (schema_version == typedCandidateSchemaVersion
        && prompt_version == typedCandidatePromptVersion) return typedCandidateMaximum;
    return 0;
}

/* 模型待审候选值；拥有字段/引文，不引用响应缓冲区，不自动成为世界事实，生命周期由调用方管理。 */
struct TypedExtractionCandidate {
    /* 内部候选类型，默认空，分类解析填写，界面映射中文。 */
    std::string type;
    /* 候选标题或实体逐字名称；默认空，校验后填入，最多 512 个 UTF-8 字节。 */
    std::string name;
    /* 模型提供的证据候选；默认空，最多 12000 个 UTF-8 字节，仍须唯一映射回原文。 */
    std::string quote;
    /* 按类型拥有的封闭字段对象；解析检查后保存，后续提交重验，不证明语义正确。 */
    xuyan::package::JsonValue fields;
};

/*
 * 功能：构造当前四分类封闭输出约束。参数：无。返回：拥有的 JSON 约束字符串。
 * 失败：分配异常可传播。副作用：仅内存构造，不发请求；数量、长度及证据仍由本地校验。
 * 线程与生命周期：同步纯函数，结果不借用局部对象。
 */
std::string typedExtractionResponseSchema();
/*
 * 功能：生成中文指令，将片段编码为不可信 JSON 数据。
 * 参数：fragment 为调用期间有效的 UTF-8 片段视图，长度与编码由上游单片输入校验保证。
 * 返回：拥有的提示词字符串。失败：分配异常可传播，函数不执行证据校验。
 * 副作用：仅内存构造，不发送、不写日志或文件；线程：同步，不保留视图。
 */
std::string typedExtractionPrompt(std::string_view fragment);
/*
 * 功能：验证封闭类型字段与逐字标识支持，不证明语义或引文实际来自原文。
 * 参数：type 为四分类内部值；fields 为字段对象；quote 为引文候选；name 为标题/名称；均只借用至返回。
 * 返回：合法时成功 true。失败：类型、属性、字节长度、编码、枚举或标识不合法返回校验错误，分配异常可传播。
 * 副作用：只读输入，不写存储；线程：同步纯函数，不保存引用。
 */
xuyan::domain::Result<bool> validateTypedCandidateFields(
    std::string_view type, const xuyan::package::JsonValue& fields,
    std::string_view quote, std::string_view name);
/*
 * 功能：验证当前响应根与分类数量，过滤无效/重复项，空分组合法。
 * 参数：text 为借用响应，最多 128 KiB；rejected_candidates 默认空指针，有值须指向调用期间可写计数。
 * 返回：合法候选列表及输出淘汰数。失败：根/结构/数量非法或非空响应全项无效返回错误；
 * 失败计数可能仅覆盖已处理项，不能用作完整统计，分配异常可传播。
 * 副作用：仅初始化并更新输出计数，不写数据库或联网；线程：同步，不保存响应引用。
 */
xuyan::domain::Result<std::vector<TypedExtractionCandidate>> parseTypedExtractionResponse(
    std::string_view text, std::size_t* rejected_candidates = nullptr);

} // namespace xuyan::application
