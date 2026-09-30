#pragma once

#include "xuyan/domain/scenario.h"

#include <chrono>
#include <string>

namespace xuyan::engine {

/*
 * 职责：保存一次离线合成推进的完整结果，仅在测试目标内传递。
 * 生命周期：值对象拥有所有字段，无外部资源；由调用线程构造和消费。
 */
struct MockStepResult {
    /* 下一步完整状态；初始使用领域默认值，成功推进后由提供商赋值，消费者只读。 */
    xuyan::domain::ScenarioState state;
    /* 本步说话人的稳定测试标识；默认空，随结果拥有，不代表用户人物。 */
    std::string actor_id;
    /* 本步合成对白，UTF-8；默认空，由 next 填写，生命周期随结果。 */
    std::string speech;
    /* 可公开的合成操作解释，UTF-8；默认空，不包含凭据或真实模型响应。 */
    std::string public_explanation;
};

/*
 * 职责：为固定三回合规则回归提供同步离线响应，仅拥有毫秒延迟配置，不拥有线程或连接。
 * 生命周期与线程：由测试调用方管理；只读配置可供独立线程调用，但输入状态由各调用方保证稳定。
 */
class MockProvider {
public:
    /*
     * 功能：保存每次合法推进前的模拟阻塞延迟。
     * 参数：latency：输入，毫秒，默认 25；不额外校验负值，sleep_for 按标准库语义处理。
     * 返回：完成提供商初始化。
     * 失败：无显式失败路径。
     * 副作用：仅保存配置；构造不休眠、不访问数据库或网络。
     * 线程与生命周期：调用线程同步构造，配置随提供商存活，无后台退出任务。
     */
    explicit MockProvider(std::chrono::milliseconds latency = std::chrono::milliseconds{25});
    /*
     * 功能：根据合成状态的回合生成对白和下一状态，供持久化/规则回归显式调用。
     * 参数：input：输入，只读状态引用，仅调用期间借用；应使用匹配的合成人物标识。
     * 返回：成功为独立状态、说话人、对白和公开解释；输入状态保持原值。
     * 失败：暂停、已完成或回合达到 3 返回校验错误；操作规则或缺失人物/知识返回对应错误；分配异常传播。
     * 副作用：合法输入同步阻塞调用线程 latency_ 毫秒；无数据库和网络操作，无后台回调。
     * 线程与生命周期：调用线程同步执行；input 须保持稳定到返回，结果拥有复制后的状态，不保留外部引用。
     */
    xuyan::domain::Result<MockStepResult> next(const xuyan::domain::ScenarioState& input) const;

private:
    /* 同步推进前的模拟延迟，单位毫秒，构造时写入、默认 25，之后只读，生命周期随提供商。 */
    std::chrono::milliseconds latency_;
};

} // namespace xuyan::engine
