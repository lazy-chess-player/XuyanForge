#include "xuyan/providers/qt_provider_transport.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <exception>
#include <memory>

namespace xuyan::providers {

xuyan::domain::Result<xuyan::application::ProviderTransportResponse> QtProviderTransport::send(
    const ProviderHttpRequest& request, const std::string& credential, int timeout_ms) {
    const QUrl url(QString::fromStdString(request.url));
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed,
             QCoreApplication::translate("QtProviderTransport", "模型请求地址无效或包含用户信息").toStdString(), false,
             QCoreApplication::translate("QtProviderTransport", "修正提供商端点").toStdString()});
    /* 功能：检查单个报文标识/值是否含换行或零字节。
     * 参数：value 为调用内借用的字节串，可空。返回：含非法字节为真。
     * 失败：无。副作用：只读；lambda 不保存、不跨线程。 */
    const auto unsafe_header = [](const std::string& value) {
        return value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos;
    };
    bool invalid_header = request.method.empty() || unsafe_header(request.method)
        || unsafe_header(credential) || unsafe_header(request.credential_header);
    for (const auto& [name, value] : request.headers)
        invalid_header = invalid_header || name.empty() || unsafe_header(name) || unsafe_header(value);
    if (timeout_ms <= 0 || invalid_header)
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed,
             QCoreApplication::translate("QtProviderTransport", "模型请求头、方法或超时时间无效").toStdString(), false,
             QCoreApplication::translate("QtProviderTransport", "检查请求配置后重试").toStdString()});

    QNetworkAccessManager manager;
    QNetworkRequest network_request(url);
    // 同源限制同时保护 Authorization 与厂商自定义密钥头，避免跳转到另一主机泄露凭据。
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::SameOriginRedirectPolicy);
    network_request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XuyanForge/0.0.1 provider-client"));
    for (const auto& [name, value] : request.headers)
        network_request.setRawHeader(QByteArray::fromStdString(name), QByteArray::fromStdString(value));
    if (!credential.empty()) {
        if (request.credential_header == "Authorization: Bearer")
            network_request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(credential));
        else if (!request.credential_header.empty())
            network_request.setRawHeader(QByteArray::fromStdString(request.credential_header),
                                         QByteArray::fromStdString(credential));
    }

    // 回复也由局部 RAII 管理：异常展开时先销毁回复，再销毁其父管理器，不留下在途任务。
    std::unique_ptr<QNetworkReply> reply(manager.sendCustomRequest(
        network_request, QByteArray::fromStdString(request.method), QByteArray::fromStdString(request.body)));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray response_body;
    bool timed_out = false;
    bool too_large = false;
    // Qt 不支持异常穿过事件派发栈；接收分配失败先标记并中止，返回事件循环后再包装错误。
    bool receive_failed = false;
    constexpr qsizetype maximum_response_bytes = 2 * 1024 * 1024;
    reply->setReadBufferSize(maximum_response_bytes + 1);
    /* 功能：消费本次已到达字节，在分配前限制正文，超限立即中止。
     * 参数：无；借用 reply、response_body、too_large、receive_failed，均只在 send 栈帧内有效。
     * 返回：无。失败：超限设置 too_large；接收异常设置 receive_failed，不抛过 Qt 事件派发栈。
     * 副作用：读取回复并可能 abort；在调用线程同步/由 readyRead 调用，loop 为连接上下文。
     * 生命周期：不逃逸 send；函数退出时上下文断开连接，完成后的尾部读取复用同一边界。 */
    const auto receive = [&reply, &response_body, &too_large, &receive_failed, maximum_response_bytes] {
        if (too_large || receive_failed) return;
        try {
            response_body += reply->read(maximum_response_bytes - response_body.size() + 1);
        } catch (const std::exception&) {
            receive_failed = true;
            reply->abort();
            return;
        }
        if (response_body.size() > maximum_response_bytes) {
            too_large = true;
            reply->abort();
        }
    };
    QObject::connect(reply.get(), &QNetworkReply::readyRead, &loop, receive);
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    /* 功能：将超时结算为可能已发送的未知结果并中止等待。
     * 参数/返回：无；借用局部超时标志和回复。失败：不保证厂商撤销或退款。
     * 副作用：更新 timed_out 并 abort；调用线程执行，连接随 loop 销毁，不触发重发。 */
    QObject::connect(&timer, &QTimer::timeout, &loop, [&timed_out, &reply] {
        timed_out = true;
        reply->abort();
    });
    timer.start(timeout_ms);
    // 本地事件循环只等待本次请求，超时后主动中止网络回复。
    if (!reply->isFinished()) loop.exec();
    receive();
    timer.stop();
    const auto http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (receive_failed)
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed,
             QCoreApplication::translate("QtProviderTransport", "接收提供商响应失败，已停止请求").toStdString(), false,
             QCoreApplication::translate("QtProviderTransport", "检查可用内存，不要自动重试未知调用").toStdString()});
    if (too_large || response_body.size() > maximum_response_bytes)
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed,
             QCoreApplication::translate("QtProviderTransport", "提供商响应超过 2 MiB 安全上限").toStdString(), false,
             QCoreApplication::translate("QtProviderTransport", "降低输出上限").toStdString()});
    xuyan::application::ProviderTransportResponse result;
    result.http_status = http_status;
    result.timed_out = timed_out;
    result.body.assign(response_body.constData(), static_cast<std::size_t>(response_body.size()));
    return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::success(std::move(result));
}

} // namespace xuyan::providers
