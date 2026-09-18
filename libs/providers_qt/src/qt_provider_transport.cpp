#include "xuyan/providers/qt_provider_transport.h"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace xuyan::providers {

xuyan::domain::Result<xuyan::application::ProviderTransportResponse> QtProviderTransport::send(
    const ProviderHttpRequest& request, const std::string& credential, int timeout_ms) {
    const QUrl url(QString::fromStdString(request.url));
    if (!url.isValid() || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed, "模型请求 URL 无效", false, "修正提供商端点"});

    QNetworkAccessManager manager;
    QNetworkRequest network_request(url);
    network_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::NoLessSafeRedirectPolicy);
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

    QNetworkReply* reply = manager.sendCustomRequest(
        network_request, QByteArray::fromStdString(request.method), QByteArray::fromStdString(request.body));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray response_body;
    bool timed_out = false;
    bool too_large = false;
    constexpr qsizetype maximum_response_bytes = 2 * 1024 * 1024;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        response_body += reply->readAll();
        if (response_body.size() > maximum_response_bytes) {
            too_large = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        timed_out = true;
        reply->abort();
    });
    timer.start(timeout_ms);
    loop.exec();
    response_body += reply->readAll();
    timer.stop();
    const auto http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    delete reply;
    if (too_large || response_body.size() > maximum_response_bytes)
        return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::failure(
            {xuyan::domain::ErrorCode::validation_failed, "提供商响应超过 2 MiB 安全上限", false, "降低输出上限"});
    xuyan::application::ProviderTransportResponse result;
    result.http_status = http_status;
    result.timed_out = timed_out;
    result.body.assign(response_body.constData(), static_cast<std::size_t>(response_body.size()));
    return xuyan::domain::Result<xuyan::application::ProviderTransportResponse>::success(std::move(result));
}

} // namespace xuyan::providers
