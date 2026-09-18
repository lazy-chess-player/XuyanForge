#pragma once

#include "xuyan/domain/scenario.h"

#include <chrono>
#include <string>

namespace xuyan::engine {

struct MockStepResult {
    xuyan::domain::ScenarioState state;
    std::string actor_id;
    std::string speech;
    std::string public_explanation;
};

class MockProvider {
public:
    explicit MockProvider(std::chrono::milliseconds latency = std::chrono::milliseconds{25});
    xuyan::domain::Result<MockStepResult> next(const xuyan::domain::ScenarioState& input) const;

private:
    std::chrono::milliseconds latency_;
};

} // namespace xuyan::engine

