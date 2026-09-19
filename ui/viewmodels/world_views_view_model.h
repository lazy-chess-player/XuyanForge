#pragma once

#include "xuyan/domain/scenario.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>
#include <functional>

class WorldViewsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY changed)
    Q_PROPERTY(QVariantList relations READ relations NOTIFY changed)
    Q_PROPERTY(QVariantList locations READ locations NOTIFY changed)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    Q_PROPERTY(QVariantList instances READ instances NOTIFY changed)
    Q_PROPERTY(QString latestSnapshotId READ latestSnapshotId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:
    explicit WorldViewsViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    QVariantList versions() const { return versions_; }
    QVariantList timeline() const { return timeline_; }
    QVariantList relations() const { return relations_; }
    QVariantList locations() const { return locations_; }
    QVariantList routes() const { return routes_; }
    QVariantList instances() const { return instances_; }
    QString latestSnapshotId() const { return latest_snapshot_id_; }
    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QString statusText() const { return status_text_; }

    Q_INVOKABLE void refresh();
    void setWorldId(QString world_id);
    Q_INVOKABLE void publishVersion(QString parent_id);
    Q_INVOKABLE void prepareSnapshot(QString version_id, QString story_time);
    Q_INVOKABLE void addTimelineEvent(QString name, QString story_time, int narrative_order,
                                      QString relative_time, QString truth_status);
    Q_INVOKABLE void addRelation(QString from_id, QString to_id, QString dimension, int strength,
                                 QString visibility, QString evidence_status);
    Q_INVOKABLE void addLocation(QString location_id, QString parent_id, QString x, QString y, QString evidence_status);
    Q_INVOKABLE void addRoute(QString from_id, QString to_id, QString minutes, QString evidence_status);
    Q_INVOKABLE void instantiateCharacter(QString blueprint_id, int blueprint_version, QString world_version_id,
                                          QString snapshot_id, QString adaptation_json, QString knowledge_policy);
    Q_INVOKABLE void bindBranch(QString branch_id, QString world_version_id, QString snapshot_id,
                                QString history_mode, QString instance_id);
signals:
    void changed();
private:
    using StringResult = xuyan::domain::Result<std::string>;
    void run(std::function<StringResult(const std::filesystem::path&)> work);
    std::filesystem::path database_path_;
    QVariantList versions_, timeline_, relations_, locations_, routes_, instances_;
    QString latest_snapshot_id_;
    bool busy_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("世界版本、时间、关系与地图已就绪")};
    QString world_id_;
};
