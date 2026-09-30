#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/provider_connection.h"

#include <map>
#include <string>
#include <string_view>

namespace xuyan::providers {

/* 厂商报文格式选择；仅作为内部协议标识，不承担界面标签或发送授权。 */
enum class ProviderProtocol {
    /* OpenAI Responses 格式，同时用于已适配的 DeepSeek 连接。 */
    openai_responses,
    /* Chat Completions 格式，供兼容服务及本地模型使用。 */
    openai_compatible,
    /* Anthropic Messages 格式。 */
    anthropic_messages,
    /* Gemini generateContent 格式。 */
    gemini_generate_content
};

/* 单次结构化生成的值对象；独占字符串，不持有凭据或网络资源。
 * 调用方冻结配置后交给协议适配器；同一对象并发修改需由调用方同步。 */
struct StructuredGenerationRequest {
    /* 输入端点地址，默认空；调用方设置，适配器追加协议路径，不含密钥。 */
    std::string endpoint;
    /* 厂商模型原始标识，默认空；适配器原样传入协议，不作为中文标签。 */
    std::string model_id;
    /* 本次有界提示词 UTF-8 正文，默认空；由调用方提供并拥有，不自动加载小说。 */
    std::string prompt;
    /* 结构化输出约束的 JSON 对象文本，默认空；构造请求时解析校验。 */
    std::string json_schema;
    /* 输出词元上限，默认 2048，合法范围 1—1000000；不代表费用授权。 */
    int max_output_tokens{2048};
    /* 是否请求流式报文，默认否；响应适配器本身只解析完整 JSON。 */
    bool stream{false};
    /* 提供商内部类型，默认空；用于校验思考配置，保持持久化协议值。 */
    std::string provider_kind;
    /* 值拥有的冻结生成配置；默认值由领域类型定义，适配器只读校验。 */
    xuyan::domain::ProviderGenerationConfig generation;
};

/* 不含明文凭据的 HTTP 请求值对象；协议层创建，传输层同步读取。
 * 字符串和头表由对象独占，生命周期不依赖原始生成请求。 */
struct ProviderHttpRequest {
    /* HTTP 方法协议值，默认 POST；由适配器设置，传输层读取。 */
    std::string method{"POST"};
    /* 完整请求地址，默认空；含厂商路径，不得嵌入凭据。 */
    std::string url;
    /* 普通头名到头值的有序表，默认空、无独立数量上限；不保存密钥。 */
    std::map<std::string, std::string, std::less<>> headers;
    /* 凭据注入位置，默认空；Authorization: Bearer 表示需加前缀，其余为头名。 */
    std::string credential_header;
    /* 紧凑 JSON 请求正文，默认空，单位为字节；传输层只读，不记录原文。 */
    std::string body;
};

/* 厂商响应的归一化值对象；仅报告模型输出和状态，不写世界事实。
 * 适配器填充，调用方负责正文保密和后续审核，内部状态值保持兼容。 */
struct ProviderGenerationResult {
    /* 内部完成/拒绝/截断/错误状态，默认空；显示层须单独映射中文。 */
    std::string status;
    /* 合并的模型文本，默认空；对象独占，失败也可能保留部分输出。 */
    std::string text;
    /* 内部失败分类或厂商停止原因，默认空；不得直接作为界面标签。 */
    std::string failure_kind;
    /* 是否属于可重试失败，默认否；仅分类提示，不触发自动重发。 */
    bool retryable{false};
    /* 响应报告的输入词元数，默认 0，缺失/非法按 0，最大归一到 20 亿。 */
    int input_tokens{0};
    /* 响应报告的输出词元数，默认 0，缺失/非法按 0，最大归一到 20 亿。 */
    int output_tokens{0};
};

/*
 * 功能：按厂商协议构造一次结构化 HTTP 请求，不执行发送。
 * 参数：protocol 为受支持的协议枚举；request 为只读生成配置，借用至调用结束，
 *       端点/模型/提示词/Schema 不得为空，输出上限须在 1—1000000。
 * 返回：成功为独立拥有的请求值，不含密钥；失败为领域错误。
 * 失败：配置、思考强度组合、协议或 Schema 无效时返回校验错误；正文组装异常转为固定中文错误，
 *       前置配置校验或错误对象自身的分配异常仍可向外传播。
 * 副作用：仅分配请求内存；不联网、不写数据库、不读取凭据。
 * 线程与生命周期：调用线程同步执行，不保存 request 引用；调用期间不得并发修改配置。
 */
xuyan::domain::Result<ProviderHttpRequest> buildProviderRequest(
    ProviderProtocol protocol, const StructuredGenerationRequest& request);
/*
 * 功能：解析一份完整的非流式厂商 JSON 响应，归一正文、结束原因和词元用量。
 * 参数：protocol 为受支持协议；response_json 为调用期间有效的 UTF-8 借用视图。
 * 返回：有效对象返回结果值；拒绝或截断也作为结果状态返回，不视为解析失败。
 * 失败：非法协议、JSON、非对象根值返回校验错误；分配异常可能向外传播。
 * 副作用：只读输入并分配结果，不记录模型正文，不提交世界资料。
 * 线程与生命周期：调用线程同步执行，返回值不依赖输入视图，不保存回调。
 */
xuyan::domain::Result<ProviderGenerationResult> parseProviderResponse(
    ProviderProtocol protocol, std::string_view response_json);
/*
 * 功能：为一次传输失败生成兼容的内部失败分类，取消优先于超时。
 * 参数：http_status 为 HTTP 状态码，0 表示未知；timed_out 表示超时；cancelled 表示已取消。
 * 返回：error 状态结果；超时为不可自动重试的 timeout_unknown，HTTP 临时失败带重试提示。
 * 失败：无参数校验错误，字符串分配异常可传播。
 * 副作用：无网络或存储操作，不因重试标志自动发出请求。
 * 线程与生命周期：同步纯值计算，不拥有任务或观察外部对象。
 */
ProviderGenerationResult classifyProviderFailure(int http_status, bool timed_out, bool cancelled);
/*
 * 功能：将连接中稳定的提供商类型映射为报文协议。
 * 参数：kind 为调用期间有效的内部类型视图；空值及未知值均不支持。
 * 返回：成功为协议枚举；失败为非重试校验错误。
 * 失败：类型没有映射时明确拒绝，不猜测协议。
 * 副作用：只读映射，不更改连接，不联网。
 * 线程与生命周期：调用线程同步执行，不保存 kind 视图，返回枚举独立有效。
 */
xuyan::domain::Result<ProviderProtocol> protocolForProviderKind(std::string_view kind);

} // namespace xuyan::providers
