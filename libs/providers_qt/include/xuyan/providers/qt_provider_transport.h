#pragma once

#include "xuyan/application/provider_generation_service.h"

namespace xuyan::providers {

/* 单次同步 Qt HTTP 适配器，无跨调用保存的 QObject、凭据或响应缓存。
 * 每次 send 在调用线程创建并销毁网络管理器及回复；须有 QCoreApplication 和 Qt 事件分派器。
 * 宜在工作线程调用，嵌套事件循环会处理该线程其他事件，调用方须防止重入和持锁等待。
 * 没有外部取消接口；超时中止不能保证厂商未计费，不自动重发。 */
class QtProviderTransport final : public xuyan::application::IProviderTransport {
public:
    /*
     * 功能：显式发送一份 HTTP 请求，同步等待完成或超时，接收正文最多 2 MiB。
     * 参数：request 为调用期间有效的只读请求，URL 须为含主机的 http/https 地址，
     *       禁止 URL 用户信息；方法及普通头名不得为空；方法、普通头名/值、凭据头标记和凭据
     *       不得含回车、换行或零字节，完整 HTTP token 语法仍由 Qt 处理。
     *       credential 为借用密钥，可空；非空时按 credential_header 注入，空标记则不注入；
     *       注入覆盖同名普通头，本函数不写日志或工作区，不擦除调用方输入。
     *       timeout_ms 为正整数毫秒，整个本次等待共用该超时，不自动延长。
     * 返回：完成时返回 HTTP 状态（未知为 0）、超时标志及正文，cancelled 保持 false；
     *       HTTP 错误、Qt 网络错误或超时仍返回传输值，正文可能为空/不完整，由上层分类。
     * 失败：参数错误、响应超限或接收标准异常返回中文领域错误；事件回调内拦截接收标准异常，
     *       事件循环之外的请求/结果构造分配异常仍可传播。
     * 副作用：产生真实网络请求和可能的费用；仅允许同源重定向，结束后销毁本次网络资源。
     * 线程与生命周期：全部回调在调用线程、send 返回之前运行；不跨线程捕获对象，
     *       调用方必须保持参数及自身执行上下文有效，退出/切换任务不能视为撤销在途计费。
     */
    xuyan::domain::Result<xuyan::application::ProviderTransportResponse> send(
        const ProviderHttpRequest& request, const std::string& credential,
        int timeout_ms) override;
};

} // namespace xuyan::providers
