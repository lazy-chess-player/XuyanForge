#pragma once

#include <QObject>
#include <QVariantList>

#include <filesystem>

class ProviderViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit ProviderViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList connections() const { return connections_; }
    bool busy() const noexcept { return busy_; }
    QString statusText() const { return status_text_; }
    QString errorText() const { return error_text_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void saveConnection(QString id, int revision, QString name, QString kind,
                                    QString endpoint, QString model, QString data_policy,
                                    bool enabled, QString api_key);
    Q_INVOKABLE void removeConnection(QString id, int revision);
    Q_INVOKABLE void probeConnection(QString id);

signals:
    void changed();

private:
    void finish(QVariantList values, QString status, QString error);
    std::filesystem::path database_path_;
    QVariantList connections_;
    bool busy_{false};
    QString status_text_{QStringLiteral("凭据由 Windows 凭据管理器保护")};
    QString error_text_;
};
