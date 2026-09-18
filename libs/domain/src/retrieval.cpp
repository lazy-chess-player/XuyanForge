#include "xuyan/domain/retrieval.h"

#include <algorithm>
#include <array>

namespace xuyan::domain {

Result<EntityRetrievalScope> validateEntityRetrievalScope(EntityRetrievalScope scope) {
    constexpr std::array allowed{std::string_view{"public"}, std::string_view{"author"}, std::string_view{"restricted"}};
    if (scope.entity_id.empty() || std::find(allowed.begin(), allowed.end(), scope.visibility) == allowed.end())
        return Result<EntityRetrievalScope>::failure(
            {ErrorCode::validation_failed, "条目检索范围缺少实体或可见性无效", false, "使用 public、author 或 restricted"});
    if (scope.valid_from && scope.valid_to && *scope.valid_from > *scope.valid_to)
        return Result<EntityRetrievalScope>::failure(
            {ErrorCode::validation_failed, "条目生效时间晚于失效时间", false, "修正故事时间范围"});
    std::sort(scope.actor_grants.begin(), scope.actor_grants.end());
    scope.actor_grants.erase(std::remove_if(scope.actor_grants.begin(), scope.actor_grants.end(),
        [](const auto& value) { return value.empty(); }), scope.actor_grants.end());
    scope.actor_grants.erase(std::unique(scope.actor_grants.begin(), scope.actor_grants.end()), scope.actor_grants.end());
    if (scope.visibility == "restricted" && scope.actor_grants.empty())
        return Result<EntityRetrievalScope>::failure(
            {ErrorCode::validation_failed, "受限条目至少需要一个人物授权", false, "选择可见人物"});
    if (scope.visibility != "restricted") scope.actor_grants.clear();
    return Result<EntityRetrievalScope>::success(std::move(scope));
}

Result<RetrievalRequest> validateRetrievalRequest(RetrievalRequest request) {
    if (request.world_id.empty() || request.query.size() > 512 || request.limit < 1 || request.limit > 100)
        return Result<RetrievalRequest>::failure(
            {ErrorCode::validation_failed, "检索世界、查询文本或数量上限无效", false, "限制查询为 512 字节、结果为 1—100 条"});
    if (!request.author_view && request.actor_id.empty())
        return Result<RetrievalRequest>::failure(
            {ErrorCode::validation_failed, "人物视角检索缺少 actor_id", false, "指定人物或改用作者视角"});
    return Result<RetrievalRequest>::success(std::move(request));
}

} // namespace xuyan::domain
