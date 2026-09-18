#include "xuyan/application/demo_world_service.h"

#include "xuyan/application/character_instance_service.h"
#include "xuyan/application/character_service.h"
#include "xuyan/application/evidence_service.h"
#include "xuyan/application/simulation_service.h"
#include "xuyan/application/source_import_service.h"
#include "xuyan/application/workspace_service.h"
#include "xuyan/application/world_graph_service.h"
#include "xuyan/application/world_version_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/storage/workspace_repository.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace xuyan::application {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;
using xuyan::domain::Result;

constexpr auto kWorldId = "world-grey-harbor";
constexpr auto kVersionCommand = "demo-grey-harbor-publish-v1";
constexpr auto kSnapshotCommand = "demo-grey-harbor-snapshot-evening";
constexpr auto kInstanceCommand = "demo-grey-harbor-linzhou-instance";
constexpr auto kBindingCommand = "demo-grey-harbor-bind-main";

const std::array<const char*, 14> kEntityIds{
    "entity-xucheng", "entity-shentang", "entity-linzhou", "entity-seal", "entity-grey-harbor",
    "entity-north-gate", "entity-ferry", "entity-council-hall", "entity-no-teleport",
    "entity-transfer-consent", "entity-echo-focus", "entity-ferry-storm",
    "entity-negotiation-breaks", "entity-council-entry"
};

std::string derivedId(std::string_view prefix, std::string_view command) {
    return std::string(prefix) + xuyan::domain::sha256(command).substr(0, 24);
}

Error installError(std::string message, std::string action = "检查工作区后重试") {
    return Error{ErrorCode::storage_error, std::move(message), true, std::move(action)};
}

template <typename T>
Result<bool> requireResult(Result<T> result) {
    if (!result.ok()) return Result<bool>::failure(*result.error);
    return Result<bool>::success(true);
}

std::pair<std::size_t, std::size_t> codepointSpan(std::string_view text, std::string_view quote) {
    const auto offset = text.find(quote);
    if (offset == std::string_view::npos) return {0, 0};
    const auto start = xuyan::domain::utf8CodepointCount(text.substr(0, offset));
    return {start, start + xuyan::domain::utf8CodepointCount(quote)};
}

Result<bool> ensureEntity(WorkspaceService& workspace, xuyan::domain::WorldEntity entity) {
    auto existing = workspace.load(entity.id);
    if (existing.ok() && !existing.value->deleted) return Result<bool>::success(false);
    const auto command_id = "demo-grey-harbor-entity-" + entity.id;
    auto created = workspace.create(command_id, std::move(entity));
    if (!created.ok()) return Result<bool>::failure(*created.error);
    return Result<bool>::success(true);
}

std::vector<xuyan::domain::WorldEntity> demoEntities() {
    return {
        {"entity-xucheng", kWorldId, "character", "许澄", {"许代表"}, {"城卫署", "谈判"},
         "城卫署谈判代表，重视秩序；在场景开始时知道北门今夜封闭。", "{\"role\":\"negotiator\"}", "accepted"},
        {"entity-shentang", kWorldId, "character", "沈棠", {"沈代表"}, {"盐运商会", "谈判"},
         "盐运商会代表，重视交易信誉；不知道封门计划，担心暴雨造成货物滞留。", "{\"role\":\"merchant\"}", "accepted"},
        {"entity-linzhou", kWorldId, "character", "林舟", {"遗物调查者"}, {"自创人物", "调查"},
         "谨慎、重承诺的遗物调查者；残响能力消耗专注，入场时专注为 3 点。", "{\"role\":\"investigator\",\"focus\":3}", "accepted"},
        {"entity-seal", kWorldId, "item", "议和印章", {"印章"}, {"唯一物品", "议和"},
         "谈判凭证，初始由沈棠持有。检查后可能发现伪造迹象。", "{\"unique\":true,\"holder\":\"entity-shentang\"}", "accepted"},
        {"entity-grey-harbor", kWorldId, "location", "灰港", {}, {"港口", "暴雨"},
         "城卫署与盐运商会准备谈判的港城；暴雨期间渡口停航。", "{\"weather\":\"storm\"}", "accepted"},
        {"entity-north-gate", kWorldId, "location", "北门", {}, {"城门", "封闭"},
         "灰港北门将在谈判前夜封闭。", "{}", "accepted"},
        {"entity-ferry", kWorldId, "location", "渡口", {}, {"交通", "暴雨"},
         "距灰港步行约半小时，暴雨持续期间无法正常通航。", "{}", "accepted"},
        {"entity-council-hall", kWorldId, "location", "议事厅", {}, {"谈判", "入场点"},
         "翌日谈判地点；演示故事从议事厅外开始。", "{}", "accepted"},
        {"entity-no-teleport", kWorldId, "rule", "禁止瞬间移动", {}, {"硬约束"},
         "当前世界不存在可用的瞬间移动方式。", "{\"severity\":\"hard\"}", "accepted"},
        {"entity-transfer-consent", kWorldId, "rule", "物品转移需同意", {}, {"硬约束", "唯一物品"},
         "转移物品需持有人同意或合法行动结果。", "{\"severity\":\"hard\"}", "accepted"},
        {"entity-echo-focus", kWorldId, "rule", "残响消耗专注", {}, {"能力", "资源"},
         "林舟使用残响能力时消耗专注。", "{\"resource\":\"focus\",\"cost\":1}", "accepted"},
        {"entity-ferry-storm", kWorldId, "rule", "暴雨时渡口停航", {}, {"交通", "天气"},
         "暴雨持续期间渡口无法正常通航。", "{\"severity\":\"hard\"}", "accepted"},
        {"entity-negotiation-breaks", kWorldId, "event", "翌日谈判破裂", {}, {"原著候选", "谈判"},
         "未受干预的候选走向是翌日谈判破裂。", "{\"story_time\":100,\"truth_status\":\"future_candidate\"}", "accepted"},
        {"entity-council-entry", kWorldId, "event", "议事厅外入场", {}, {"入场点"},
         "谈判前一天傍晚，人物在议事厅外入场。", "{\"story_time\":0,\"truth_status\":\"fact\"}", "accepted"}
    };
}

} // namespace

DemoWorldService::DemoWorldService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

std::string DemoWorldService::sourceText() {
    return R"MD(# 灰港议和

谈判前一天傍晚，暴雨笼罩灰港。城卫署与盐运商会将在翌日午后于议事厅谈判。

灰港的北门将在今夜封闭，暴雨持续期间渡口停航。灰港到渡口步行约半小时。

城卫署代表许澄重视秩序，知道北门封闭计划。盐运商会代表沈棠重视交易信誉，不知道封门计划，担心货物滞留。

唯一的议和印章由沈棠持有。印章存在伪造迹象，但只有检查后才可能发现。转移物品需持有人同意或合法行动结果。

林舟是一名谨慎、重承诺的遗物调查者。他的“残响”能力会消耗专注，初始专注为3点。

原著候选走向是翌日谈判破裂。故事从议事厅外开始。

这个世界不存在瞬间移动。
)MD";
}

Result<DemoWorldProgress> DemoWorldService::inspect() {
    try {
        DemoWorldProgress progress;
        const auto text = sourceText();
        progress.source_id = "source-" + xuyan::domain::sha256(text).substr(0, 24);
        progress.world_version_id = derivedId("world-version-", kVersionCommand);
        progress.snapshot_id = derivedId("snapshot-", kSnapshotCommand);
        progress.character_instance_id = derivedId("character-instance-", kInstanceCommand);

        SourceImportService sources(database_path_);
        auto source_list = sources.list();
        const bool source_ready = source_list.ok() && std::any_of(source_list.value->begin(), source_list.value->end(),
            [&](const auto& item) { return item.id == progress.source_id; });
        bool evidence_ready = false;
        if (source_ready) {
            EvidenceService evidence(database_path_);
            auto entries = evidence.listForSource(progress.source_id);
            evidence_ready = entries.ok() && entries.value->size() >= 10;
        }

        WorkspaceService workspace(database_path_);
        int entity_count = 0;
        for (const auto* id : kEntityIds) {
            auto entity = workspace.load(id);
            if (entity.ok() && !entity.value->deleted) ++entity_count;
        }

        WorldGraphService graph(database_path_);
        auto timeline = graph.listTimeline(kWorldId, false);
        auto map = graph.loadMap(kWorldId);
        auto relations = graph.listRelations(kWorldId, {}, std::nullopt, {}, true);
        const bool graph_ready = timeline.ok() && timeline.value->size() >= 2 && map.ok()
            && map.value->locations.size() >= 4 && map.value->routes.size() >= 1
            && relations.ok() && relations.value->size() >= 2;

        WorldVersionService versions(database_path_);
        auto version_list = versions.list(kWorldId);
        const bool version_ready = version_list.ok() && std::any_of(version_list.value->begin(), version_list.value->end(),
            [&](const auto& item) { return item.id == progress.world_version_id && item.members.size() >= kEntityIds.size(); });

        CharacterInstanceService instances(database_path_);
        auto instance = instances.load(progress.character_instance_id);
        const bool character_ready = instance.ok() && instance.value->status == "ready"
            && instance.value->world_version_id == progress.world_version_id
            && instance.value->snapshot_id == progress.snapshot_id;

        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto binding = repository.loadBranchRootBinding("branch-main");
        const bool branch_ready = binding.ok() && binding.value->world_version_id == progress.world_version_id
            && binding.value->snapshot_id == progress.snapshot_id
            && std::find(binding.value->character_instance_ids.begin(), binding.value->character_instance_ids.end(),
                         progress.character_instance_id) != binding.value->character_instance_ids.end();

        progress.stages = {
            {"source", "自创短文与证据", "导入灰港短文并绑定可回跳的码点证据", source_ready && evidence_ready},
            {"entities", "完整世界条目", "人物、地点、物品、规则、候选事件与入场点", entity_count == static_cast<int>(kEntityIds.size())},
            {"graph", "时间线、关系与地图", "谈判事件、定向关系、地点层级和半小时路线", graph_ready},
            {"version", "世界 v1 与历史快照", "冻结成员修订，并固定谈判前一晚的故事时间", version_ready && character_ready},
            {"character", "林舟人物入场", "固定人物卡版本、能力映射与 3 点专注", character_ready},
            {"branch", "推演分支根", "世界版本、快照和人物实例不可变绑定", branch_ready}
        };
        progress.completed = static_cast<int>(std::count_if(progress.stages.begin(), progress.stages.end(),
            [](const auto& stage) { return stage.ready; }));
        progress.ready = progress.completed == static_cast<int>(progress.stages.size());
        return Result<DemoWorldProgress>::success(std::move(progress));
    } catch (const std::exception& exception) {
        return Result<DemoWorldProgress>::failure(installError(exception.what()));
    }
}

Result<DemoWorldProgress> DemoWorldService::install() {
    try {
        WorkspaceService workspace(database_path_);
        for (auto entity : demoEntities()) {
            auto ready = ensureEntity(workspace, std::move(entity));
            if (!ready.ok()) return Result<DemoWorldProgress>::failure(*ready.error);
        }

        const auto temporary = database_path_.parent_path() / ".grey-harbor-demo.md";
        {
            std::filesystem::create_directories(temporary.parent_path());
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            const auto text = sourceText();
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!output) return Result<DemoWorldProgress>::failure(installError("无法写入演示短文临时文件"));
        }
        SourceImportService sources(database_path_);
        auto source = sources.importTextFile("demo-grey-harbor-source-v1", temporary, "演示世界 v1");
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        if (!source.ok()) return Result<DemoWorldProgress>::failure(*source.error);

        const auto text = sourceText();
        const std::array<std::tuple<const char*, const char*, const char*>, 10> evidence_specs{{
            {"entity-grey-harbor", "/description", "谈判前一天傍晚，暴雨笼罩灰港。"},
            {"entity-north-gate", "/description", "灰港的北门将在今夜封闭"},
            {"entity-ferry", "/description", "暴雨持续期间渡口停航。灰港到渡口步行约半小时。"},
            {"entity-xucheng", "/description", "城卫署代表许澄重视秩序，知道北门封闭计划。"},
            {"entity-shentang", "/description", "盐运商会代表沈棠重视交易信誉，不知道封门计划，担心货物滞留。"},
            {"entity-seal", "/description", "唯一的议和印章由沈棠持有。印章存在伪造迹象，但只有检查后才可能发现。"},
            {"entity-transfer-consent", "/description", "转移物品需持有人同意或合法行动结果。"},
            {"entity-linzhou", "/description", "林舟是一名谨慎、重承诺的遗物调查者。他的“残响”能力会消耗专注，初始专注为3点。"},
            {"entity-negotiation-breaks", "/description", "原著候选走向是翌日谈判破裂。"},
            {"entity-no-teleport", "/description", "这个世界不存在瞬间移动。"}
        }};
        EvidenceService evidence(database_path_);
        for (const auto& [entity_id, field_path, quote] : evidence_specs) {
            const auto [start, end] = codepointSpan(text, quote);
            if (start == end) return Result<DemoWorldProgress>::failure(installError("演示证据片段与内置短文不一致"));
            auto created = evidence.create(std::string("demo-grey-harbor-evidence-") + entity_id,
                entity_id, field_path, source.value->id, start, end, "original_fact");
            if (!created.ok()) return Result<DemoWorldProgress>::failure(*created.error);
        }

        WorldGraphService graph(database_path_);
        xuyan::domain::TimelineEvent entry;
        entry.id = "timeline-council-entry"; entry.name = "议事厅外入场"; entry.story_time = 0;
        entry.narrative_order = 1; entry.truth_status = "fact"; entry.results = {"timeline-negotiation-breaks"};
        auto saved_entry = graph.saveTimelineEvent("demo-grey-harbor-timeline-entry", entry, 0);
        if (!saved_entry.ok()) return Result<DemoWorldProgress>::failure(*saved_entry.error);
        xuyan::domain::TimelineEvent negotiation;
        negotiation.id = "timeline-negotiation-breaks"; negotiation.name = "翌日谈判破裂"; negotiation.story_time = 100;
        negotiation.narrative_order = 2; negotiation.truth_status = "future_candidate";
        negotiation.prerequisites = {entry.id}; negotiation.causes = {entry.id};
        auto saved_negotiation = graph.saveTimelineEvent("demo-grey-harbor-timeline-negotiation", negotiation, 0);
        if (!saved_negotiation.ok()) return Result<DemoWorldProgress>::failure(*saved_negotiation.error);

        xuyan::domain::DirectedRelation xu_to_shen;
        xu_to_shen.id = "relation-xucheng-shentang-trust"; xu_to_shen.from_entity_id = "entity-xucheng";
        xu_to_shen.to_entity_id = "entity-shentang"; xu_to_shen.dimension = "trust"; xu_to_shen.strength = 15;
        xu_to_shen.valid_from = 0; xu_to_shen.evidence_status = "evidence";
        auto relation_one = graph.saveRelation("demo-grey-harbor-relation-xu-shen", xu_to_shen, 0);
        if (!relation_one.ok()) return Result<DemoWorldProgress>::failure(*relation_one.error);
        xuyan::domain::DirectedRelation shen_to_xu;
        shen_to_xu.id = "relation-shentang-xucheng-doubt"; shen_to_xu.from_entity_id = "entity-shentang";
        shen_to_xu.to_entity_id = "entity-xucheng"; shen_to_xu.dimension = "doubt"; shen_to_xu.strength = 10;
        shen_to_xu.valid_from = 0; shen_to_xu.visibility = "restricted"; shen_to_xu.actor_grants = {"actor-shentang"};
        shen_to_xu.evidence_status = "assumption";
        auto relation_two = graph.saveRelation("demo-grey-harbor-relation-shen-xu", shen_to_xu, 0);
        if (!relation_two.ok()) return Result<DemoWorldProgress>::failure(*relation_two.error);

        const std::array<xuyan::domain::LocationPlacement, 4> locations{{
            {"entity-grey-harbor", "", std::nullopt, std::nullopt, "", "evidence"},
            {"entity-north-gate", "entity-grey-harbor", 120, 80, "", "evidence"},
            {"entity-ferry", "entity-grey-harbor", 540, 350, "", "evidence"},
            {"entity-council-hall", "entity-grey-harbor", 330, 210, "", "evidence"}
        }};
        for (const auto& location : locations) {
            auto saved = graph.saveLocation("demo-grey-harbor-map-" + location.location_id, location, 0);
            if (!saved.ok()) return Result<DemoWorldProgress>::failure(*saved.error);
        }
        xuyan::domain::TravelRoute route{"route-grey-harbor-ferry", "entity-grey-harbor", "entity-ferry", 30, true, "evidence"};
        auto saved_route = graph.saveRoute("demo-grey-harbor-route-ferry", route, 0);
        if (!saved_route.ok()) return Result<DemoWorldProgress>::failure(*saved_route.error);

        CharacterService cards(database_path_);
        auto card_list = cards.openAndList();
        if (!card_list.ok()) return Result<DemoWorldProgress>::failure(*card_list.error);

        WorldVersionService versions(database_path_);
        auto version_list = versions.list(kWorldId);
        if (!version_list.ok()) return Result<DemoWorldProgress>::failure(*version_list.error);
        const auto version_id = derivedId("world-version-", kVersionCommand);
        auto version_it = std::find_if(version_list.value->begin(), version_list.value->end(),
            [&](const auto& item) { return item.id == version_id; });
        xuyan::domain::WorldVersion version;
        if (version_it == version_list.value->end()) {
            auto published = versions.publish(kVersionCommand, kWorldId);
            if (!published.ok()) return Result<DemoWorldProgress>::failure(*published.error);
            version = std::move(*published.value);
        } else {
            version = *version_it;
        }
        auto snapshot = versions.prepareSnapshot(kSnapshotCommand, version.id, 0);
        if (!snapshot.ok()) return Result<DemoWorldProgress>::failure(*snapshot.error);

        CharacterInstanceService instances(database_path_);
        auto instance = instances.instantiate(kInstanceCommand, "blueprint-linzhou", 1, version.id, snapshot.value->id,
            "{\"echo\":\"消耗 1 点专注读取物品残响\",\"铜制指针\":\"普通调查工具\",\"focus\":3}", "strict");
        if (!instance.ok()) return Result<DemoWorldProgress>::failure(*instance.error);
        SimulationService simulation(database_path_);
        auto root = simulation.open();
        if (!root.ok()) return Result<DemoWorldProgress>::failure(*root.error);
        auto binding = instances.bindBranchRoot(kBindingCommand, "branch-main", version.id, snapshot.value->id,
                                                "branching", {instance.value->id});
        if (!binding.ok()) return Result<DemoWorldProgress>::failure(*binding.error);
        return inspect();
    } catch (const std::exception& exception) {
        return Result<DemoWorldProgress>::failure(installError(exception.what()));
    }
}

} // namespace xuyan::application
