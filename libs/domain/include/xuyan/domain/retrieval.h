#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_entity.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xuyan::domain {

struct EntityRetrievalScope {
    std::string entity_id;
    std::optional<std::int64_t> valid_from;
    std::optional<std::int64_t> valid_to;
    std::string visibility{"public"};
    std::vector<std::string> actor_grants;
    int revision{0};
};

struct RetrievalRequest {
    std::string world_id{"world-grey-harbor"};
    std::string query;
    std::optional<std::int64_t> story_time;
    std::string actor_id;
    bool author_view{false};
    int limit{20};
};

struct RetrievalHit {
    WorldEntity entity;
    int lexical_score{0};
};

Result<EntityRetrievalScope> validateEntityRetrievalScope(EntityRetrievalScope scope);
Result<RetrievalRequest> validateRetrievalRequest(RetrievalRequest request);

} // namespace xuyan::domain
