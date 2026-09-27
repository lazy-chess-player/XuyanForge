#pragma once

#include "xuyan/domain/scenario.h"

#include <chrono>
#include <string>

namespace xuyan::engine {

/** @brief 保存固定测试响应生成的状态、说话人和解释。 */
struct MockStepResult {
    xuyan::domain::ScenarioState state;
    std::string actor_id;
    std::string speech;
    std::string public_explanation;
};

class MockProvider {
public:
    /** @brief 创建具有固定模拟延迟的离线测试提供商。 */
    explicit MockProvider(std::chrono::milliseconds latency = std::chrono::milliseconds{25});
    /** @brief 根据输入状态生成下一步合成结果，不调用网络。 */
    xuyan::domain::Result<MockStepResult> next(const xuyan::domain::ScenarioState& input) const;

private:
    std::chrono::milliseconds latency_;
};

} // namespace xuyan::engine
