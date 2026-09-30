#include "world_views_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/character_instance_service.h"
#include "xuyan/application/world_graph_service.h"
#include "xuyan/application/world_version_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

namespace {
/*
 * 功能：将领域层 UTF-8 字符串转为界面 QString。
 * 参数：value 为调用期间借用的文本，允许空值。
 * 返回：拥有自身存储的 Qt 字符串，空输入返回空串。
 * 失败：内存分配异常可传播；不额外校验文本编码。
 * 副作用：只构造返回值，不修改世界资料。
 */
QString q(const std::string& value) { return QString::fromStdString(value); }
/*
 * 功能：为一次显式世界视图写操作生成幂等命令标识。
 * 参数：无。
 * 返回：新生成的不带花括号 UUID 字符串。
 * 失败：UUID/字符串构造异常可传播。
 * 副作用：消耗本机随机 UUID 状态，不读写世界或网络。
 */
std::string command() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
/*
 * 功能：将可选故事时间文本解析为整数；缺失或无法解析时保持未知。
 * 参数：value 为调用期间借用的输入文本；空白字符串表示未知。
 * 返回：有效整数返回 int64 值，空白或解析失败返回 nullopt；单位由世界故事时间基准定义。
 * 失败：解析失败不抛业务异常，但不能区分非法文本和显式未知。
 * 副作用：只读输入，不推断章节顺序或日期。
 */
std::optional<std::int64_t> optionalNumber(const QString& value) {
    if (value.trimmed().isEmpty()) return std::nullopt;
    bool ok = false; const auto number = value.toLongLong(&ok); return ok ? std::optional<std::int64_t>{number} : std::nullopt;
}
}

WorldViewsViewModel::WorldViewsViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {}

void WorldViewsViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++generation_;
    // 先清空旧世界的可见数据，让正在运行的旧查询在回调时自行失效。
    versions_.clear(); timeline_.clear(); relations_.clear(); locations_.clear(); routes_.clear(); instances_.clear();
    latest_snapshot_id_.clear(); busy_ = false; error_text_.clear();
    status_text_ = world_id_.isEmpty() ? tr("请选择世界以查看世界视图")
                                        : tr("正在读取当前世界的视图…");
    emit changed();
    if (!world_id_.isEmpty()) refresh();
}

void WorldViewsViewModel::refresh() {
    if (busy_ || world_id_.isEmpty()) return;
    const auto generation = ++generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_; const auto latest_snapshot = latest_snapshot_id_;
    const auto selected_world = world_id_.toStdString();
    /*
     * 功能：在工作线程读取世界版本、时间线、关系、地图与最新版本人物实例摘要。
     * 参数：闭包按值持有路径、最近快照、选定世界及代次；self 为非拥有式弱引用。
     * 返回：无；六类列表与错误经 GUI 排队回调交付。
     * 失败：任一服务错误记入安全中文错误；意外异常不暴露路径或用户数据。
     * 副作用：只读世界资料，不创建样例，也不发送模型请求。
     * 线程与生命周期：全局线程池运行，服务与数据库连接为工作线程局部对象。
     */
    QThreadPool::globalInstance()->start([self, path, latest_snapshot, selected_world, generation] {
        QVariantList version_items, timeline_items, relation_items, location_items, route_items, instance_items;
        QString error;
        try {
        xuyan::application::WorldVersionService version_service(path);
        xuyan::application::WorldGraphService graph(path);
        auto versions = version_service.list(selected_world); auto timeline = graph.listTimeline(selected_world, false);
        auto relations = graph.listRelations(selected_world, {}, std::nullopt, {}, true); auto map = graph.loadMap(selected_world);
        if (!versions.ok()) error = view_model_text::errorText(*versions.error);
        else for (const auto& value : *versions.value) version_items.push_back(QVariantMap{{"id", q(value.id)}, {"parent", q(value.parent_id)}, {"hash", q(value.content_hash)}, {"members", static_cast<int>(value.members.size())}, {"publishedAt", q(value.published_at)}});
        if (error.isEmpty() && !timeline.ok()) error = view_model_text::errorText(*timeline.error);
        else if (timeline.ok()) for (const auto& value : *timeline.value) timeline_items.push_back(QVariantMap{{"id", q(value.id)}, {"name", q(value.name)}, {"storyTime", value.story_time ? QString::number(*value.story_time) : tr("未知")}, {"narrativeOrder", value.narrative_order}, {"relativeTime", q(value.relative_time)}, {"truthStatus", q(value.truth_status)}, {"causes", static_cast<int>(value.causes.size())}, {"results", static_cast<int>(value.results.size())}});
        if (error.isEmpty() && !relations.ok()) error = view_model_text::errorText(*relations.error);
        else if (relations.ok()) for (const auto& value : *relations.value) relation_items.push_back(QVariantMap{{"id", q(value.id)}, {"from", q(value.from_entity_id)}, {"to", q(value.to_entity_id)}, {"dimension", q(value.dimension)}, {"strength", value.strength ? QVariant(*value.strength) : QVariant{}}, {"visibility", q(value.visibility)}, {"evidenceStatus", q(value.evidence_status)}, {"bidirectional", value.bidirectional}, {"truthStatus", q(value.truth_status)}});
        if (error.isEmpty() && !map.ok()) error = view_model_text::errorText(*map.error);
        else if (map.ok()) {
            for (const auto& value : map.value->locations) location_items.push_back(QVariantMap{{"id", q(value.location_id)}, {"parent", q(value.parent_location_id)}, {"x", value.image_x ? QString::number(*value.image_x) : tr("未知")}, {"y", value.image_y ? QString::number(*value.image_y) : tr("未知")}, {"evidenceStatus", q(value.evidence_status)}, {"truthStatus", q(value.truth_status)}});
            for (const auto& value : map.value->routes) route_items.push_back(QVariantMap{{"id", q(value.id)}, {"from", q(value.from_location_id)}, {"to", q(value.to_location_id)}, {"minutes", value.travel_minutes ? QString::number(*value.travel_minutes) : tr("未知")}, {"bidirectional", value.bidirectional}, {"evidenceStatus", q(value.evidence_status)}});
        }
        if (error.isEmpty() && versions.ok() && !versions.value->empty()) {
            xuyan::application::CharacterInstanceService instance_service(path);
            auto instances = instance_service.list(versions.value->back().id);
            if (!instances.ok()) error = view_model_text::errorText(*instances.error);
            else for (const auto& value : *instances.value) instance_items.push_back(QVariantMap{{"id", q(value.id)}, {"name", q(value.name)}, {"card", tr("%1 · 版本 %2").arg(q(value.blueprint_id)).arg(value.blueprint_version)}, {"policy", q(value.knowledge_policy)}, {"status", q(value.status)}, {"statusLabel", view_model_text::stateLabel(value.status)}, {"conflicts", static_cast<int>(value.conflicts.size())}});
        }
        } catch (...) { error = tr("世界视图读取发生内部错误，请检查工作区后重试"); }
        if (!self) return;
        /*
         * 功能：仅当世界代次未改变时，将已读取的六类摘要替换到 GUI 快照。
         * 参数：捕获弱引用、错误文字、六个拥有列表、最近快照 ID 与请求代次。
         * 返回：无；过期或对象已销毁时放弃回填。
         * 失败：错误写 errorText，部分列表仍反映本次实际读取结果，不冒充全部成功。
         * 副作用：复位忙碌态、替换列表并发 changed；不写存储。
         * 线程与生命周期：Qt 排队到 GUI 线程，捕获值不依赖工作线程栈。
         */
        QMetaObject::invokeMethod(self, [self, error, version_items, timeline_items, relation_items, location_items, route_items, instance_items, latest_snapshot, generation] {
            if (!self || generation != self->generation_) return;
            self->busy_ = false; self->error_text_ = error; self->versions_ = version_items;
            self->timeline_ = timeline_items; self->relations_ = relation_items; self->locations_ = location_items;
            self->routes_ = route_items; self->instances_ = instance_items; self->latest_snapshot_id_ = latest_snapshot; emit self->changed();
        }, Qt::QueuedConnection);
    });
}

/*
 * 功能：为当前世界的显式写操作统一安排工作线程和安全结果回显。
 * 参数：work 为按值拥有的同步业务回调；只在工作线程执行期间借用路径参数，返回中文状态或失败 Result。
 * 返回：无；忙碌时忽略，无世界时直接显示中文错误。
 * 失败：回调异常转存储错误，服务失败不刷新成功列表。
 * 副作用：可能由 work 写世界数据，成功后更新状态并重新读取世界视图。
 * 线程与生命周期：GUI 发起、线程池执行业务、GUI 按代次回填；弱引用失效时丢弃通知但不撤销事务。
 */
void WorldViewsViewModel::run(std::function<StringResult(const std::filesystem::path&)> work) {
    if (busy_) return;
    if (world_id_.isEmpty()) {
        error_text_ = tr("请先选择世界"); emit changed(); return;
    }
    const auto generation = ++generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_;
    /*
     * 功能：在线程池同步调用已捕获的世界业务回调。
     * 参数：闭包拥有路径、代次和可移动 work；self 为弱引用；无调用参数。
     * 返回：无；Result 排队交 GUI。
     * 失败：回调异常转为安全中文错误，异常详情不展示。
     * 副作用：work 可能写世界资料及命令记录；本层不自行决定事务。
     * 线程与生命周期：工作线程执行；路径引用只在 work 调用期间有效。
     */
    QThreadPool::globalInstance()->start([self, path, generation, work = std::move(work)]() mutable {
        StringResult result;
        try { result = work(path); }
        catch (...) { result = StringResult::failure({xuyan::domain::ErrorCode::storage_error,
            "世界视图操作发生内部错误", true, "检查工作区后重试"}); }
        if (!self) return;
        /*
         * 功能：在仍有效的世界代次中显示写操作结果并刷新六类摘要。
         * 参数：捕获弱引用、拥有的 Result 及操作代次。
         * 返回：无；过期回调不修改新世界界面。
         * 失败：Result 错误转中文提示，成功才发起刷新。
         * 副作用：复位忙碌态、更新状态或错误，发 changed；成功时投递读取。
         * 线程与生命周期：Qt 排队到 GUI 线程，对象销毁后不会访问其成员。
         */
        QMetaObject::invokeMethod(self, [self, result = std::move(result), generation]() mutable {
            if (!self || generation != self->generation_) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = view_model_text::errorText(*result.error); emit self->changed(); return; }
            self->status_text_ = q(*result.value); emit self->changed(); self->refresh();
        }, Qt::QueuedConnection);
    });
}

/*
 * 工作回调契约：按值持有父版本 ID、命令 ID 与启动世界 ID；只在 run 的工作线程中借用 path。
 * 返回发布成功的中文提示及版本 ID，或原样传播服务 Result 错误；发布不可变版本是唯一写入副作用。
 * 界面对象销毁或切世界仅放弃回显，不撤销已经提交的世界版本。
 */
void WorldViewsViewModel::publishVersion(QString parent_id) { const auto id = command(); const auto world = world_id_.toStdString(); run([parent_id, id, world](const auto& path) { xuyan::application::WorldVersionService s(path); auto r = s.publish(id, world, parent_id.toStdString()); if (!r.ok()) return StringResult::failure(*r.error); return StringResult::success("已发布不可变世界版本 " + r.value->id); }); }
void WorldViewsViewModel::prepareSnapshot(QString version_id, QString story_time) {
    bool valid_time = false; const auto time = story_time.toLongLong(&valid_time);
    if (world_id_.isEmpty()) { error_text_ = tr("请先选择世界"); emit changed(); return; }
    if (!valid_time || busy_) { if (!valid_time) { error_text_ = tr("故事时间必须是整数"); emit changed(); } return; }
    const auto generation = ++generation_;
    busy_ = true; error_text_.clear(); emit changed();
    QPointer<WorldViewsViewModel> self(this); const auto path = database_path_; const auto id = command();
    /*
     * 功能：在工作线程按明确版本 ID 与整数故事时间生成历史快照。
     * 参数：闭包按值持有路径、命令 ID、版本 ID、故事时间及代次；self 为非拥有式弱引用。
     * 返回：无；快照 Result 排队交 GUI。
     * 失败：服务错误或异常转安全错误，不推断缺失故事时间。
     * 副作用：成功时写历史快照与命令记录；不发送模型请求。
     * 线程与生命周期：全局线程池运行，对象销毁后写入可能完成但无界面回填。
     */
    QThreadPool::globalInstance()->start([self, path, id, version_id, time, generation] {
        xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> result;
        try {
            xuyan::application::WorldVersionService service(path);
            result = service.prepareSnapshot(id, version_id.toStdString(), time);
        } catch (...) { result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
            "历史快照生成发生内部错误", true, "检查工作区后重试"}); }
        if (!self) return;
        /*
         * 功能：只为仍在当前世界代次的快照操作显示结果与待补全项数量。
         * 参数：捕获弱引用、拥有的快照 Result 和代次。
         * 返回：无；切世界或销毁后放弃回显。
         * 失败：Result 错误映射中文错误，不把失败快照当作最近快照。
         * 副作用：更新最近快照 ID、状态与忙碌标志，发 changed 并刷新世界摘要。
         * 线程与生命周期：Qt 排队到 GUI 线程；持久化快照不依赖回显成功。
         */
        QMetaObject::invokeMethod(self, [self, result = std::move(result), generation]() mutable {
            if (!self || generation != self->generation_) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = view_model_text::errorText(*result.error); emit self->changed(); return; }
            self->latest_snapshot_id_ = q(result.value->id);
            self->status_text_ = tr("已生成快照 %1，待补全 %2 项").arg(self->latest_snapshot_id_).arg(result.value->unresolved_entity_ids.size());
            emit self->changed(); self->refresh();
        }, Qt::QueuedConnection);
    });
}
/*
 * 工作回调契约：按值捕获名称、可空故事时间、叙事顺序、相对时间、真实性、世界和命令 ID；path 仅借用至回调返回。
 * 空白或非法故事时间均保留未知，不用叙事顺序推断日期；返回保存成功提示或服务错误。
 * 副作用：世界图服务保存新事件；回调由 run 在线程池同步执行，不访问 GUI 成员。
 */
void WorldViewsViewModel::addTimelineEvent(QString name, QString story_time, int order, QString relative, QString truth) { const auto id=command(); const auto world=world_id_.toStdString(); run([=](const auto& path){ xuyan::domain::TimelineEvent v; v.id="timeline-"+id.substr(0,24); v.world_id=world; v.name=name.toStdString(); v.story_time=optionalNumber(story_time); v.narrative_order=order; v.relative_time=relative.toStdString(); v.truth_status=truth.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveTimelineEvent(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("时间事件已保存"); }); }
/*
 * 工作回调契约：按值捕获两端实体 ID、维度、已知整数强度、可见性、证据状态、世界和命令 ID；path 仅临时借用。
 * 返回保存成功提示或服务错误；端点、字段及世界归属由服务校验，异常交 run 统一处理。
 * 副作用：保存一条有方向的新关系，未知强度不会由本入口表达；工作线程不访问 GUI 成员。
 */
void WorldViewsViewModel::addRelation(QString from, QString to, QString dimension, int strength, QString visibility, QString evidence) { const auto id=command(); const auto world=world_id_.toStdString(); run([=](const auto& path){ xuyan::domain::DirectedRelation v; v.id="relation-"+id.substr(0,24); v.world_id=world; v.from_entity_id=from.toStdString(); v.to_entity_id=to.toStdString(); v.dimension=dimension.toStdString(); v.strength=strength; v.visibility=visibility.toStdString(); v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveRelation(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("定向关系已保存"); }); }
/*
 * 工作回调契约：按值捕获地点/父地点 ID、可选 x/y 整数文本、证据状态及命令 ID；path 只在调用中有效。
 * 两个坐标均非空且均可解析时保存已知图像坐标，否则保留未知；返回成功提示或服务错误。
 * 副作用：保存地点标注；服务失败由 run 呈现，工作线程不触碰 GUI 对象。
 */
void WorldViewsViewModel::addLocation(QString location, QString parent, QString x, QString y, QString evidence) { const auto id=command(); run([=](const auto& path){ xuyan::domain::LocationPlacement v; v.location_id=location.toStdString(); v.parent_location_id=parent.toStdString(); if(!x.trimmed().isEmpty()&&!y.trimmed().isEmpty()){bool ox=false,oy=false; auto xv=x.toInt(&ox),yv=y.toInt(&oy); if(ox&&oy){v.image_x=xv;v.image_y=yv;}} v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveLocation(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("地点标注已保存"); }); }
/*
 * 工作回调契约：按值捕获起终点、可空行程分钟文本、证据状态和命令 ID；path 仅临时借用。
 * 空白或非法分钟数保留未知，已知数值由服务继续校验；返回中文成功提示或服务错误。
 * 副作用：保存有向路线，不推断反向路线；工作线程与 GUI 状态隔离。
 */
void WorldViewsViewModel::addRoute(QString from, QString to, QString minutes, QString evidence) { const auto id=command(); run([=](const auto& path){ xuyan::domain::TravelRoute v; v.id="route-"+id.substr(0,24); v.from_location_id=from.toStdString(); v.to_location_id=to.toStdString(); if(!minutes.trimmed().isEmpty()){bool ok=false;auto m=minutes.toInt(&ok);if(ok)v.travel_minutes=m;} v.evidence_status=evidence.toStdString(); xuyan::application::WorldGraphService s(path); auto r=s.saveRoute(id,std::move(v),0); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("地点路线已保存"); }); }
/*
 * 工作回调契约：按值捕获人物卡 ID/版本、世界版本 ID、快照 ID、适配 JSON、知识策略和命令 ID；path 仅临时借用。
 * 返回就绪或冲突数量的中文提示，服务拒绝则传播错误；不把存在冲突说成已就绪。
 * 副作用：实例服务可能持久化新人物实例；run 在线程池调用，回调不访问 GUI 成员。
 */
void WorldViewsViewModel::instantiateCharacter(QString card, int version, QString world, QString snapshot, QString adaptation, QString policy) { const auto id=command(); run([=](const auto& path){ xuyan::application::CharacterInstanceService s(path); auto r=s.instantiate(id,card.toStdString(),version,world.toStdString(),snapshot.toStdString(),adaptation.toStdString(),policy.toStdString()); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success(r.value->status=="ready"?"人物实例已就绪":"人物实例已创建，但需要解决 "+std::to_string(r.value->conflicts.size())+" 项冲突"); }); }
/*
 * 工作回调契约：按值捕获分支 ID、世界版本 ID、快照 ID、历史模式、人物实例 ID 和命令 ID；path 临时借用。
 * 返回分支根摘要的中文提示或服务错误，错误不自动重试；只绑定明确选定的单个实例。
 * 副作用：实例服务可能持久化分支根；回调在线程池运行，不读取后续 GUI 世界选择。
 */
void WorldViewsViewModel::bindBranch(QString branch, QString world, QString snapshot, QString mode, QString instance) { const auto id=command(); run([=](const auto& path){ xuyan::application::CharacterInstanceService s(path); auto r=s.bindBranchRoot(id,branch.toStdString(),world.toStdString(),snapshot.toStdString(),mode.toStdString(),{instance.toStdString()}); if(!r.ok()) return StringResult::failure(*r.error); return StringResult::success("分支根已固定："+r.value->root_hash.substr(0,12)); }); }
