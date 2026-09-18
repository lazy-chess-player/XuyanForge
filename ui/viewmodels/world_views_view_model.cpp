#include "world_views_view_model.h"

#include "xuyan/application/character_instance_service.h"
#include "xuyan/application/world_graph_service.h"
#include "xuyan/application/world_version_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
std::string command() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
std::optional<std::int64_t> optionalNumber(const QString& value) {
    if (value.trimmed().isEmpty()) return std::nullopt;
    bool ok = false; const auto number = value.toLongLong(&ok); return ok ? std::optional<std::int64_t>{number} : std::nullopt;
}
}

WorldViewsViewModel::WorldViewsViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) { refresh(); }

void WorldViewsViewModel::refresh() {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_; const auto latest_snapshot = latest_snapshot_id_;
    QThreadPool::globalInstance()->start([self, path, latest_snapshot] {
        xuyan::application::WorldVersionService version_service(path);
        xuyan::application::WorldGraphService graph(path);
        auto versions = version_service.list("world-grey-harbor"); auto timeline = graph.listTimeline("world-grey-harbor", false);
        auto relations = graph.listRelations("world-grey-harbor", {}, std::nullopt, {}, true); auto map = graph.loadMap("world-grey-harbor");
        QVariantList version_items, timeline_items, relation_items, location_items, route_items, instance_items; QString error;
        if (!versions.ok()) error = q(versions.error->message);
        else for (const auto& value : *versions.value) version_items.push_back(QVariantMap{{"id", q(value.id)}, {"parent", q(value.parent_id)}, {"hash", q(value.content_hash)}, {"members", static_cast<int>(value.members.size())}, {"publishedAt", q(value.published_at)}});
        if (error.isEmpty() && !timeline.ok()) error = q(timeline.error->message);
        else if (timeline.ok()) for (const auto& value : *timeline.value) timeline_items.push_back(QVariantMap{{"id", q(value.id)}, {"name", q(value.name)}, {"storyTime", value.story_time ? QString::number(*value.story_time) : QStringLiteral("未知")}, {"narrativeOrder", value.narrative_order}, {"relativeTime", q(value.relative_time)}, {"truthStatus", q(value.truth_status)}, {"causes", static_cast<int>(value.causes.size())}, {"results", static_cast<int>(value.results.size())}});
        if (error.isEmpty() && !relations.ok()) error = q(relations.error->message);
        else if (relations.ok()) for (const auto& value : *relations.value) relation_items.push_back(QVariantMap{{"id", q(value.id)}, {"from", q(value.from_entity_id)}, {"to", q(value.to_entity_id)}, {"dimension", q(value.dimension)}, {"strength", value.strength}, {"visibility", q(value.visibility)}, {"evidenceStatus", q(value.evidence_status)}});
        if (error.isEmpty() && !map.ok()) error = q(map.error->message);
        else if (map.ok()) {
            for (const auto& value : map.value->locations) location_items.push_back(QVariantMap{{"id", q(value.location_id)}, {"parent", q(value.parent_location_id)}, {"x", value.image_x ? QString::number(*value.image_x) : QStringLiteral("未知")}, {"y", value.image_y ? QString::number(*value.image_y) : QStringLiteral("未知")}, {"evidenceStatus", q(value.evidence_status)}});
            for (const auto& value : map.value->routes) route_items.push_back(QVariantMap{{"id", q(value.id)}, {"from", q(value.from_location_id)}, {"to", q(value.to_location_id)}, {"minutes", value.travel_minutes ? QString::number(*value.travel_minutes) : QStringLiteral("未知")}, {"bidirectional", value.bidirectional}, {"evidenceStatus", q(value.evidence_status)}});
        }
        if (error.isEmpty() && versions.ok() && !versions.value->empty()) {
            xuyan::application::CharacterInstanceService instance_service(path);
            auto instances = instance_service.list(versions.value->back().id);
            if (!instances.ok()) error = q(instances.error->message);
            else for (const auto& value : *instances.value) instance_items.push_back(QVariantMap{{"id", q(value.id)}, {"name", q(value.name)}, {"card", q(value.blueprint_id) + " v" + QString::number(value.blueprint_version)}, {"policy", q(value.knowledge_policy)}, {"status", q(value.status)}, {"conflicts", static_cast<int>(value.conflicts.size())}});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, error, version_items, timeline_items, relation_items, location_items, route_items, instance_items, latest_snapshot] {
            if (!self) return;
            self->busy_ = false; self->error_text_ = error; self->versions_ = version_items;
            self->timeline_ = timeline_items; self->relations_ = relation_items; self->locations_ = location_items;
            self->routes_ = route_items; self->instances_ = instance_items; self->latest_snapshot_id_ = latest_snapshot; emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void WorldViewsViewModel::run(std::function<StringResult(const std::filesystem::path&)> work) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_;
    QThreadPool::globalInstance()->start([self, path, work = std::move(work)]() mutable {
        auto result = work(path); if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = q(result.error->message); emit self->changed(); return; }
            self->status_text_ = q(*result.value); emit self->changed(); self->refresh();
        }, Qt::QueuedConnection);
    });
}

void WorldViewsViewModel::publishVersion(QString parent_id) { const auto id = command(); run([parent_id, id](const auto& path) { xuyan::application::WorldVersionService s(path); auto r = s.publish(id, "world-grey-harbor", parent_id.toStdString()); if (!r.ok()) return StringResult::failure(*r.error); return StringResult::success("已发布不可变世界版本 " + r.value->id); }); }
void WorldViewsViewModel::prepareSnapshot(QString version_id, QString story_time) {
    bool valid_time = false; const auto time = story_time.toLongLong(&valid_time);
    if (!valid_time || busy_) { if (!valid_time) { error_text_ = QStringLiteral("故事时间必须是整数"); emit changed(); } return; }
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_; const auto id = command();
    QThreadPool::globalInstance()->start([self, path, id, version_id, time] {
        xuyan::application::WorldVersionService service(path); auto result = service.prepareSnapshot(id, version_id.toStdString(), time);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = q(result.error->message); emit self->changed(); return; }
            self->latest_snapshot_id_ = q(result.value->id);
            self->status_text_ = QStringLiteral("已生成快照 %1，待补全 %2 项").arg(self->latest_snapshot_id_).arg(result.value->unresolved_entity_ids.size());
            emit self->changed(); self->refresh();
        }, Qt::QueuedConnection);
    });
}
void WorldViewsViewModel::addTimelineEvent(QString name, QString story_time, int order, QString relative, QString truth) { const auto id=command(); run([=](const auto& path){ xuyan::domain::TimelineEvent v; v.id="timeline-"+id.substr(0,24); v.name=name.toStdString(); v.story_time=optionalNumber(story_time); v.narrative_order=order; v.relative_time=relative.toStdString(); v.truth_status=truth.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveTimelineEvent(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("时间事件已保存"); }); }
void WorldViewsViewModel::addRelation(QString from, QString to, QString dimension, int strength, QString visibility, QString evidence) { const auto id=command(); run([=](const auto& path){ xuyan::domain::DirectedRelation v; v.id="relation-"+id.substr(0,24); v.from_entity_id=from.toStdString(); v.to_entity_id=to.toStdString(); v.dimension=dimension.toStdString(); v.strength=strength; v.visibility=visibility.toStdString(); v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveRelation(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("定向关系已保存"); }); }
void WorldViewsViewModel::addLocation(QString location, QString parent, QString x, QString y, QString evidence) { const auto id=command(); run([=](const auto& path){ xuyan::domain::LocationPlacement v; v.location_id=location.toStdString(); v.parent_location_id=parent.toStdString(); if(!x.trimmed().isEmpty()&&!y.trimmed().isEmpty()){bool ox=false,oy=false; auto xv=x.toInt(&ox),yv=y.toInt(&oy); if(ox&&oy){v.image_x=xv;v.image_y=yv;}} v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveLocation(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("地点标注已保存"); }); }
void WorldViewsViewModel::addRoute(QString from, QString to, QString minutes, QString evidence) { const auto id=command(); run([=](const auto& path){ xuyan::domain::TravelRoute v; v.id="route-"+id.substr(0,24); v.from_location_id=from.toStdString(); v.to_location_id=to.toStdString(); if(!minutes.trimmed().isEmpty()){bool ok=false;auto m=minutes.toInt(&ok);if(ok)v.travel_minutes=m;} v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveRoute(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("地点路线已保存"); }); }
void WorldViewsViewModel::instantiateCharacter(QString card, int version, QString world, QString snapshot, QString adaptation, QString policy) { const auto id=command(); run([=](const auto& path){ xuyan::application::CharacterInstanceService s(path); auto r=s.instantiate(id,card.toStdString(),version,world.toStdString(),snapshot.toStdString(),adaptation.toStdString(),policy.toStdString()); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success(r.value->status=="ready"?"人物实例已就绪":"人物实例已创建，但需要解决 "+std::to_string(r.value->conflicts.size())+" 项冲突"); }); }
void WorldViewsViewModel::bindBranch(QString branch, QString world, QString snapshot, QString mode, QString instance) { const auto id=command(); run([=](const auto& path){ xuyan::application::CharacterInstanceService s(path); auto r=s.bindBranchRoot(id,branch.toStdString(),world.toStdString(),snapshot.toStdString(),mode.toStdString(),{instance.toStdString()}); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("分支根已固定："+r.value->root_hash.substr(0,12)); }); }
