#pragma once

#include "xuyan/application/credential_store.h"
#include "xuyan/providers/model_protocol.h"

#include <filesystem>
#include <string>

namespace xuyan::application {

/* 单次传输返回值；拥有响应体，只在请求链路内使用，HTTP结果不等于业务生成成功，不应写日志。 */
struct ProviderTransportResponse {
    /* HTTP 状态码；默认 0 表示尚无状态，传输填写，由网关按成功范围/失败类别解释。 */
    int http_status{0};
    /* 超时标志，默认 false；传输设置，true 时结果可能已在服务端产生，禁止推断未计费。 */
    bool timed_out{false};
    /* 传输取消标志，默认 false；不保证请求未送达或费用撤销，由网关分类。 */
    bool cancelled{false};
    /* 原始响应体，默认空，传输填入、协议解析器读取，随结果持有，可能含私有模型输出。 */
    std::string body;
};

/* 同步传输端口；请求与凭据由调用方持有，适配器可内部使用事件循环，但不得在返回后借用参数。 */
class IProviderTransport {
public:
    /*
     * 功能：通过基类安全销毁传输适配器。参数：无。返回：完成资源释放。
     * 失败：析构不得抛出；副作用：释放派生传输资源。
     * 线程与生命周期：调用方须先结束在途 send，再按适配器所属线程销毁。
     */
    virtual ~IProviderTransport() = default;
    /*
     * 功能：执行已获授权的单次请求。参数：request 为借用 HTTP 请求；credential 为借用秘密，本地提供商允许空串；
     * timeout_ms 为毫秒超时，网关限制 1000—300000，直接调用者遵循适配器约束。
     * 返回：响应值，包含状态、超时/取消标记及私有响应体。失败：传输设施错误返回 Result，不能据失败断定请求未送达。
     * 副作用：可能发送小说并计费，不自动重试，不记录秘密/正文；线程：同步，参数及适配器必须存活至返回。
     */
    virtual xuyan::domain::Result<ProviderTransportResponse> send(
        const xuyan::providers::ProviderHttpRequest& request,
        const std::string& credential, int timeout_ms) = 0;
};

/* 用户显式连接自检报告；只保存元数据、内部状态和用量，不保存提示词、秘密或模型响应。 */
struct ProviderTestReport {
    /* 被测试连接标识；默认空，由自检填写，界面用它关联元数据。 */
    std::string connection_id;
    /* 提供商内部类型；默认空，自检后尽力查询，查询失败可留空，界面映射中文。 */
    std::string provider_kind;
    /* 真实模型标识；默认空，自检后尽力读取连接，不保证期间连接未变，显示时保留原值。 */
    std::string model_id;
    /* 生成状态内部值；默认空，由请求结果填写，结构不合格改为 error，界面需中文映射。 */
    std::string status;
    /* 失败分类内部值；默认空表示未分类/无失败，由网关或自检填写，不直接作界面提示。 */
    std::string failure_kind;
    /* 厂商报告的本次输入词元数，初始 0，不表示账单最终金额，由生成结果填写。 */
    int input_tokens{0};
    /* 厂商报告的本次输出词元数，初始 0，由生成结果填写，随报告持有。 */
    int output_tokens{0};
    /* generate 调用耗时，单位毫秒，以 steady_clock 测量，初始 0，不含随后元数据查询。 */
    int elapsed_ms{0};
    /* 响应含 true 的 ok 与非空 provider 字段时为 true；默认 false，不证明厂商身份或完整语义正确。 */
    bool json_valid{false};
};

/* 单次结构化生成网关；持有路径、借用凭据和传输端口，不拥有线程，不自动发送或重试。 */
class ProviderGenerationService {
public:
    /*
     * 功能：绑定请求依赖。参数：database_path 为按值路径；credentials、transport 为非拥有端口引用，须长于所有调用。
     * 返回：完成初始化。失败：路径分配异常传播，不测试连接。
     * 副作用：无网络或凭据访问；线程：同步，调用方满足两端口线程约束。
     */
    ProviderGenerationService(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);

    /*
     * 功能：检查连接策略、冻结指纹和请求字段后发送一次结构化请求，须由调用方明确授权。
     * 参数：connection_id 为连接；prompt 为待发送文本；json_schema 为输出约束；max_output_tokens 默认 1024，合法范围由协议校验；
     * timeout_ms 默认 30000 毫秒，限 1000—300000；expected_connection_fingerprint 默认空不校验指纹，有值必须匹配；
     * generation_config 为模型思考配置，默认使用厂商默认值，由提供商能力校验。
     * 返回：厂商生成结果，Result 成功仍可能包含失败状态，调用方必须检查 status。
     * 失败：连接/策略/指纹/凭据/构造请求/解析错误返回 Result；标准异常被转为隐藏详情的存储错误，未知异常可传播。
     * 副作用：读取秘密并显式发送，可能计费；尽力清除本地秘密，不跨网络等待持有写事务，不自动重发。
     * 线程与生命周期：调用线程同步等待，端口及输入须有效至返回；不支持此接口直接传入停止令牌。
     */
    xuyan::domain::Result<xuyan::providers::ProviderGenerationResult> generate(
        const std::string& connection_id, const std::string& prompt,
        const std::string& json_schema, int max_output_tokens = 1024,
        int timeout_ms = 30000, const std::string& expected_connection_fingerprint = {},
        const xuyan::domain::ProviderGenerationConfig& generation_config = {});
    /*
     * 功能：显式发起不含小说/世界资料的连接自检，仍可能计费。
     * 参数：connection_id 为连接；timeout_ms 默认 30000 毫秒，合法范围沿用 generate。
     * 返回：不含输出正文的测试报告，结构失败作为报告状态返回。失败：generate 的 Result 错误传播，分配异常可能传播。
     * 副作用：发送一次合成自检请求、读取连接元数据，不保存测试资料；线程：同步等待，不自动重试。
     */
    xuyan::domain::Result<ProviderTestReport> testStructuredGeneration(
        const std::string& connection_id, int timeout_ms = 30000);

private:
    /* 连接元数据路径；构造后只读，与服务同寿命，不缓存连接配置或秘密。 */
    std::filesystem::path database_path_;
    /* 非拥有凭据端口；生成期间读取，须在服务和全部调用期间存活，线程约束由实现定义。 */
    ICredentialStore& credentials_;
    /* 非拥有传输端口；send 同步使用，调用方管理线程与销毁，不与凭据共享所有权。 */
    IProviderTransport& transport_;
};

} // namespace xuyan::application
