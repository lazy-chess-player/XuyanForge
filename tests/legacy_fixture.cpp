#include "xuyan/application/workspace_service.h"
#include "xuyan/application/character_service.h"
#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

xuyan::domain::Result<bool> WorkspaceService::installTestFixture() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto page = repository.searchEntities({}, {}, 0, 1);
    if (!page.ok()) return xuyan::domain::Result<bool>::failure(*page.error);
    if (page.value->total > 0) return xuyan::domain::Result<bool>::success(false);
    const std::vector<xuyan::domain::WorldEntity> samples{
        {"entity-xucheng", "world-grey-harbor", "character", "许澄", {"许代表"}, {"城卫署", "谈判"},
         "城卫署谈判代表，重视秩序；在场景开始时知道北门今夜封闭。", "{\"role\":\"negotiator\"}", "accepted"},
        {"entity-shentang", "world-grey-harbor", "character", "沈棠", {"沈代表"}, {"盐运商会", "谈判"},
         "盐运商会代表，重视交易信誉；担心暴雨造成货物滞留。", "{\"role\":\"merchant\"}", "accepted"},
        {"entity-seal", "world-grey-harbor", "item", "议和印章", {"印章"}, {"唯一物品", "议和"},
         "谈判凭证，初始由沈棠持有。检查后可能发现伪造迹象。", "{\"unique\":true}", "accepted"},
        {"entity-grey-harbor", "world-grey-harbor", "location", "灰港", {}, {"港口", "暴雨"},
         "城卫署与盐运商会准备谈判的港城；暴雨期间渡口停航。", "{\"weather\":\"storm\"}", "accepted"},
        {"entity-no-teleport", "world-grey-harbor", "rule", "禁止瞬间移动", {}, {"硬约束"},
         "当前世界不存在可用的瞬间移动方式。", "{\"severity\":\"hard\"}", "accepted"},
    };
    for (const auto& sample : samples) {
        auto created = repository.createEntity("seed-" + sample.id, sample);
        if (!created.ok()) return xuyan::domain::Result<bool>::failure(*created.error);
    }
    return xuyan::domain::Result<bool>::success(true);
}

xuyan::domain::Result<bool> CharacterService::installTestFixture() {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto existing = repository.listBlueprints();
    if (!existing.ok()) return xuyan::domain::Result<bool>::failure(*existing.error);
    if (!existing.value->empty()) return xuyan::domain::Result<bool>::success(false);
    xuyan::domain::CharacterBlueprint card;
    card.id = "blueprint-linzhou";
    card.name = "林舟";
    card.summary = "谨慎、重承诺的遗物调查者。";
    card.values = {"不以无辜者换取胜利"};
    card.traits = {"谨慎", "重承诺", "面对权威会质疑"};
    card.long_term_goal = "寻找失散的导师";
    card.short_term_goal = "查明议和印章的来历";
    card.speech_style = "短句，先问证据，很少主动暴露情绪";
    card.abilities_json = "[{\"key\":\"echo\",\"description\":\"触碰物品感知过去残留\",\"cost\":\"消耗专注\"}]";
    card.equipment = {"铜制指针"};
    card.background = "长期从事遗物调查。";
    card.private_notes = "害怕导师已背叛自己。";
    auto created = repository.createBlueprint("seed-blueprint-linzhou", std::move(card));
    if (!created.ok()) return xuyan::domain::Result<bool>::failure(*created.error);
    return xuyan::domain::Result<bool>::success(true);
}

} // namespace xuyan::application
