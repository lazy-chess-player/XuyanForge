#include "provider_view_model.h"

#include "xuyan/application/provider_connection_service.h"
#include "xuyan/application/provider_generation_service.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"
#include "xuyan/storage/workspace_repository.h"

#include <QMetaObject>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>

namespace {
QVariantMap toMap(const xuyan::domain::ProviderConnection& value, bool configured) {
    return {{"id", QString::fromStdString(value.id)}, {"revision", value.revision},
            {"name", QString::fromStdString(value.name)}, {"kind", QString::fromStdString(value.kind)},
            {"endpoint", QString::fromStdString(value.endpoint)}, {"model", QString::fromStdString(value.default_model)},
            {"dataPolicy", QString::fromStdString(value.data_policy)}, {"enabled", value.enabled},
            {"credentialConfigured", configured}};
}
}

ProviderViewModel::ProviderViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) { refresh(); }

void ProviderViewModel::finish(QVariantList values, QString status, QString error) {
    connections_ = std::move(values); status_text_ = std::move(status); error_text_ = std::move(error);
    busy_ = false; emit changed();
}

void ProviderViewModel::refresh() {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    const auto database = database_path_; QPointer<ProviderViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database] {
        QVariantList values; QString error;
        xuyan::platform::SystemCredentialStore credentials;
        xuyan::application::ProviderConnectionService service(database, credentials);
        auto result = service.list();
        if (!result.ok()) error = QString::fromStdString(result.error->message);
        else for (const auto& item : *result.value) {
            const auto configured = service.hasCredential(item.id);
            values.push_back(toMap(item, configured.ok() && *configured.value));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, values = std::move(values), error] () mutable {
            if (self) {
                const auto count = values.size();
                self->finish(std::move(values), error.isEmpty() ? QStringLiteral("已加载 %1 个模型连接").arg(count) : QString{}, error);
            }
        }, Qt::QueuedConnection);
    });
}

void ProviderViewModel::saveConnection(QString id, int revision, QString name, QString kind,
                                       QString endpoint, QString model, QString data_policy,
                                       bool enabled, QString api_key) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在安全保存连接…"); emit changed();
    const auto database = database_path_; QPointer<ProviderViewModel> self(this);
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, database, command, id, revision, name, kind, endpoint, model, data_policy, enabled, api_key] {
        QVariantList values; QString error;
        xuyan::platform::SystemCredentialStore credentials;
        xuyan::application::ProviderConnectionService service(database, credentials);
        xuyan::domain::ProviderConnection connection;
        connection.id = id.toStdString(); connection.name = name.toStdString(); connection.kind = kind.toStdString();
        connection.endpoint = endpoint.toStdString(); connection.default_model = model.toStdString();
        connection.data_policy = data_policy.toStdString(); connection.enabled = enabled;
        std::optional<std::string> secret;
        if (!api_key.isEmpty()) secret = api_key.toStdString();
        auto saved = service.save(command, std::move(connection), revision, std::move(secret));
        if (!saved.ok()) error = QString::fromStdString(saved.error->message);
        auto result = service.list();
        if (result.ok()) for (const auto& item : *result.value) {
            const auto configured = service.hasCredential(item.id);
            values.push_back(toMap(item, configured.ok() && *configured.value));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, values = std::move(values), error] () mutable {
            if (self) self->finish(std::move(values), error.isEmpty() ? QStringLiteral("连接已保存；API Key 未写入工作区") : QString{}, error);
        }, Qt::QueuedConnection);
    });
}

void ProviderViewModel::removeConnection(QString id, int revision) {
    if (busy_ || id.isEmpty()) return;
    busy_ = true; error_text_.clear(); emit changed();
    const auto database = database_path_; QPointer<ProviderViewModel> self(this);
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QThreadPool::globalInstance()->start([self, database, command, id, revision] {
        QVariantList values; QString error;
        xuyan::platform::SystemCredentialStore credentials;
        xuyan::application::ProviderConnectionService service(database, credentials);
        auto removed = service.remove(command, id.toStdString(), revision);
        if (!removed.ok()) error = QString::fromStdString(removed.error->message);
        auto result = service.list();
        if (result.ok()) for (const auto& item : *result.value) {
            const auto configured = service.hasCredential(item.id);
            values.push_back(toMap(item, configured.ok() && *configured.value));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, values = std::move(values), error] () mutable {
            if (self) self->finish(std::move(values), error.isEmpty() ? QStringLiteral("连接和系统凭据已删除") : QString{}, error);
        }, Qt::QueuedConnection);
    });
}

void ProviderViewModel::probeConnection(QString id) {
    if (busy_ || id.isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在后台验证端点…"); emit changed();
    const auto database = database_path_; QPointer<ProviderViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database, id] {
        QString status; QString error;
        std::string secret;
        try {
        xuyan::storage::WorkspaceRepository repository(database);
        auto loaded = repository.loadProviderConnection(id.toStdString());
        if (!loaded.ok()) error = QString::fromStdString(loaded.error->message);
        if (error.isEmpty() && loaded.value->kind != "local") {
            xuyan::platform::SystemCredentialStore credentials;
            auto credential = credentials.get(loaded.value->credential_ref);
            if (!credential.ok()) error = QStringLiteral("缺少系统凭据，请先输入 API Key 并保存");
            else secret = std::move(*credential.value);
        }
        if (error.isEmpty()) {
            QString url = QString::fromStdString(loaded.value->endpoint);
            while (url.endsWith('/')) url.chop(1);
            if (!url.endsWith(QStringLiteral("/models"))) url += QStringLiteral("/models");
            QNetworkAccessManager manager;
            QNetworkRequest request{QUrl(url)};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XuyanForge/0.0.1 endpoint-probe"));
            if (loaded.value->kind == "anthropic") {
                request.setRawHeader("x-api-key", QByteArray::fromStdString(secret));
                request.setRawHeader("anthropic-version", "2023-06-01");
            } else if (loaded.value->kind == "gemini") {
                request.setRawHeader("x-goog-api-key", QByteArray::fromStdString(secret));
            } else if (!secret.empty()) {
                request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(secret));
            }
            QEventLoop loop; QTimer timer; timer.setSingleShot(true);
            QNetworkReply* reply = manager.get(request);
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
            timer.start(10000); loop.exec();
            const auto http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (!timer.isActive()) error = QStringLiteral("端点验证超时（10 秒），未修改连接配置");
            else if (http >= 200 && http < 300) status = QStringLiteral("端点验证成功（HTTP %1）；具体模型能力仍需逐项验证").arg(http);
            else if (http == 401 || http == 403) error = QStringLiteral("端点可达，但凭据被拒绝（HTTP %1）").arg(http);
            else if (http > 0) error = QStringLiteral("端点返回 HTTP %1；请检查 base URL 和协议类型").arg(http);
            else error = QStringLiteral("无法连接端点：%1").arg(reply->errorString());
            reply->deleteLater();
        }
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        }
        secret.assign(secret.size(), '\0');
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, status, error] {
            if (!self) return;
            self->busy_ = false; self->status_text_ = status; self->error_text_ = error; emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void ProviderViewModel::testStructuredGeneration(QString id) {
    if (busy_ || id.isEmpty()) return;
    busy_ = true; error_text_.clear(); status_text_ = QStringLiteral("正在执行真实结构化生成自检…"); emit changed();
    const auto database = database_path_; QPointer<ProviderViewModel> self(this);
    QThreadPool::globalInstance()->start([self, database, id] {
        QString status; QString error;
        try {
            xuyan::platform::SystemCredentialStore credentials;
            xuyan::providers::QtProviderTransport transport;
            xuyan::application::ProviderGenerationService service(database, credentials, transport);
            auto report = service.testStructuredGeneration(id.toStdString(), 45000);
            if (!report.ok()) error = QString::fromStdString(report.error->message);
            else if (report.value->status != "completed" || !report.value->json_valid) {
                error = QStringLiteral("结构化生成失败：%1；未推进任何推演分支")
                    .arg(QString::fromStdString(report.value->failure_kind));
            } else {
                status = QStringLiteral("真实生成成功 · %1 · JSON 已验证 · %2/%3 tokens · %4 ms")
                    .arg(QString::fromStdString(report.value->model_id))
                    .arg(report.value->input_tokens).arg(report.value->output_tokens).arg(report.value->elapsed_ms);
            }
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, status, error] {
            if (!self) return;
            self->busy_ = false; self->status_text_ = status; self->error_text_ = error; emit self->changed();
        }, Qt::QueuedConnection);
    });
}
