#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>

namespace xuyan::domain {


/*
 * 职责：不含凭据的模型生成语义，与任务一起冻结。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ProviderGenerationConfig {
    // 提供商思考强度内部值；不支持的组合明确拒绝。默认"provider_default"。
    std::string reasoning_effort{"provider_default"};
    // 结构化输出适配模式内部值，和适配器契约一致。默认"provider_schema_v1"。
    std::string output_format{"provider_schema_v1"};
};

/*
 * 功能：校验生成配置；显式思考强度目前只对 DeepSeek Responses 开放，其他厂商不能静默忽略。
 * 参数：
 *   config：按值持有的生成配置，不含密钥；值须在已支持思考和输出集合内。
 *   provider_kind：提供商内部类别，默认空值仅检查通用结构；显式值同时检查非默认思考档位支持情况。
 * 返回：合法配置值；未知值或不支持组合返回validation_failed，不静默降级。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ProviderGenerationConfig> validateProviderGenerationConfig(
    ProviderGenerationConfig config, std::string_view provider_kind = {});

/*
 * 职责：不含明文密钥的本地模型连接配置。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ProviderConnection {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 提供商协议类别内部值，不是实体类型，界面提供中文标签。默认空串。
    std::string kind;
    // 模型接口地址，不包含明文密钥。默认空串。
    std::string endpoint;
    // 连接默认使用的真实模型标识。默认空串。
    std::string default_model;
    // 系统凭据的引用标识，仅保存引用，不保存明文密钥。默认空串。
    std::string credential_ref;
    // 允许本地或远程数据传输的内部策略值。默认"remote_allowed"。
    std::string data_policy{"remote_allowed"};
    // 连接是否启用；停用配置仍可读取但不应发送请求。默认true。
    bool enabled{true};
    // 软删除标志，写入接口维护，已发布历史不因此丢失。默认false。
    bool deleted{false};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 功能：校验模型连接配置，拒绝无效提供商及不安全地址。
 * 参数：
 *   connection：待校验的ProviderConnection值，按值持有，不修改调用者原对象。名称1—256字节、端点最多2048字节且无空白；远程使用安全地址、本地允许普通地址；提供商/数据策略受支持，模型标识最多256字节；仅本地策略须为回环地址；不实际连接网络或读取凭据。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ProviderConnection> validateProviderConnection(ProviderConnection connection);
/*
 * 功能：计算不含密钥的连接配置指纹，用于识别任务执行期间的配置变化。
 * 参数：
 *   connection：借用连接快照，只读取配置及凭据引用，不读取明文凭据。
 * 返回：长度前缀区分字段边界的64字符小写SHA-256配置摘要。
 * 失败：缓冲分配失败可传播标准异常，无错误结果容器。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
std::string providerConnectionFingerprint(const ProviderConnection& connection);

} // namespace xuyan::domain
