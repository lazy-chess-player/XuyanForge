#pragma once

#include "xuyan/domain/simulation_session.h"

namespace xuyan::engine {

/*
 * 功能：从合成状态生成指定人物的可见事实及确定性序列化上下文，隔离其他人物私密知识。
 * 参数：state：输入，只读完整状态；input_commit_id：输入，当前提交标识，原样记录；
 *   actor_id：输入，必须存在于 state.characters 的人物标识；引用只在调用期间借用。
 * 返回：成功为拥有自身事实数组和序列化字符串的上下文。
 * 失败：人物缺失返回 missing_context；分配/字符串构造异常向外传播。
 * 副作用：只读输入，无数据库、文件、凭据或网络操作；调用线程同步执行。
 */
xuyan::domain::Result<xuyan::domain::ActorContext> buildActorContext(
    const xuyan::domain::ScenarioState& state, const std::string& input_commit_id,
    const std::string& actor_id);

/* 职责：为合成会话回归按回合产生确定性意图；无成员资源，调用线程同步使用。 */
class SessionMockProvider {
public:
    /*
     * 功能：按合成回合生成说话、检查或定向传播意图，并交由领域意图校验器检查结构。
     * 参数：context：输入，人物 ID 与提交 ID 的来源；state：输入，当前回合的来源；均只读借用。
     * 返回：成功为校验后的意图；第 2 回合标记结束，其他未覆盖回合返回无状态变化的对白。
     * 失败：领域意图校验失败返回错误；不自行验证上下文与 state 是否属于同一提交，分配异常传播。
     * 副作用：无；不修改状态、不预留预算、不访问外部模型。
     * 线程与生命周期：调用线程同步执行，context/state 不保存或传递后台；返回意图独立持有字符串。
     */
    xuyan::domain::Result<xuyan::domain::ActorIntent> propose(
        const xuyan::domain::ActorContext& context, const xuyan::domain::ScenarioState& state) const;
};

} // namespace xuyan::engine
