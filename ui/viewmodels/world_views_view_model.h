#pragma once

#include "xuyan/domain/scenario.h"

#include <QObject>
#include <QVariantList>

#include <cstdint>
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
    /** @brief 创建世界视图状态；未选世界时不执行后台读取。 */
    explicit WorldViewsViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    /** @brief 返回当前世界的版本摘要。 */
    QVariantList versions() const { return versions_; }
    /** @brief 返回当前世界的时间线摘要。 */
    QVariantList timeline() const { return timeline_; }
    /** @brief 返回当前世界的关系摘要。 */
    QVariantList relations() const { return relations_; }
    /** @brief 返回当前世界的地点标注摘要。 */
    QVariantList locations() const { return locations_; }
    /** @brief 返回当前世界的路线摘要。 */
    QVariantList routes() const { return routes_; }
    /** @brief 返回当前世界版本的人物实例摘要。 */
    QVariantList instances() const { return instances_; }
    /** @brief 返回最近一次在当前世界生成的历史快照标识。 */
    QString latestSnapshotId() const { return latest_snapshot_id_; }
    /** @brief 返回当前世界视图是否正在后台读取或写入。 */
    bool busy() const noexcept { return busy_; }
    /** @brief 返回当前世界视图的错误说明。 */
    QString errorText() const { return error_text_; }
    /** @brief 返回当前世界视图的状态说明。 */
    QString statusText() const { return status_text_; }

    /** @brief 异步刷新当前世界；空世界时不读取任何其他世界。 */
    Q_INVOKABLE void refresh();
    /** @brief 切换当前世界并作废旧异步结果，清空旧列表后读取新世界。 */
    void setWorldId(QString world_id);
    /** @brief 发布当前世界的新版本。 */
    Q_INVOKABLE void publishVersion(QString parent_id);
    /** @brief 根据选定版本和故事时间生成当前世界的历史快照。 */
    Q_INVOKABLE void prepareSnapshot(QString version_id, QString story_time);
    /** @brief 为当前世界写入时间线事件。 */
    Q_INVOKABLE void addTimelineEvent(QString name, QString story_time, int narrative_order,
                                      QString relative_time, QString truth_status);
    /** @brief 为当前世界写入带可见范围的定向关系。 */
    Q_INVOKABLE void addRelation(QString from_id, QString to_id, QString dimension, int strength,
                                 QString visibility, QString evidence_status);
    /** @brief 保存地点在地图中的可选坐标和父地点。 */
    Q_INVOKABLE void addLocation(QString location_id, QString parent_id, QString x, QString y, QString evidence_status);
    /** @brief 保存地点之间的行程路线。 */
    Q_INVOKABLE void addRoute(QString from_id, QString to_id, QString minutes, QString evidence_status);
    /** @brief 将人物卡实例投放到选定世界版本和历史快照。 */
    Q_INVOKABLE void instantiateCharacter(QString blueprint_id, int blueprint_version, QString world_version_id,
                                          QString snapshot_id, QString adaptation_json, QString knowledge_policy);
    /** @brief 将人物实例绑定到分支根。 */
    Q_INVOKABLE void bindBranch(QString branch_id, QString world_version_id, QString snapshot_id,
                                QString history_mode, QString instance_id);
signals:
    /** @brief 通知界面刷新世界视图列表、状态或错误。 */
    void changed();
private:
    using StringResult = xuyan::domain::Result<std::string>;
    /** @brief 在后台执行一次当前世界写操作并丢弃切换后的迟到回调。 */
    void run(std::function<StringResult(const std::filesystem::path&)> work);
    std::filesystem::path database_path_;
    QVariantList versions_, timeline_, relations_, locations_, routes_, instances_;
    QString latest_snapshot_id_;
    bool busy_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("请选择世界以查看世界视图")};
    QString world_id_;
    std::uint64_t generation_{0};
};
