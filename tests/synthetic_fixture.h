#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/simulation_session.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::test {

/*
 * 功能：构造规则回归的初始双人物状态；固定资料仅由测试目标显式使用。
 * 参数：无。
 * 返回：拥有自身字符串和人物数组的状态值，回合与修订沿用领域默认值。
 * 失败：内存分配异常向调用方传播，不提供错误结果包装。
 * 副作用：无文件、数据库或网络操作；返回值生命周期由调用方管理。
 * 线程与生命周期：调用线程同步构造，无外部引用或后台任务；状态由调用方拥有。
 */
domain::ScenarioState makeSyntheticInitialState();

/*
 * 功能：仅在没有世界条目的测试工作区显式安装五个有限合成条目。
 * 参数：database_path：输入，测试拥有的临时 SQLite 文件路径，不得指向用户工作区；调用期间有效。
 * 返回：成功值 true 表示已安装，false 表示已有任意条目而跳过；失败携带仓储错误。
 * 失败：查询或任一创建失败立即返回；仓储构造与分配异常向外传播。
 * 副作用：打开并可能初始化数据库，各条目独立创建；后项失败时前项不会整体回滚。不联网。
 * 线程与生命周期：调用线程同步使用局部仓储连接，函数返回时释放；路径只借用，不负责删除文件。
 */
domain::Result<bool> installSyntheticEntities(const std::filesystem::path& database_path);

/*
 * 功能：仅在没有人物卡的测试工作区显式安装一张合成人物卡。
 * 参数：database_path：输入，测试拥有的临时数据库路径，调用期间有效，不接管文件所有权。
 * 返回：成功值 true 表示创建，false 表示已有卡片而跳过；失败携带仓储错误。
 * 失败：列举或创建错误原样传播为失败结果；仓储构造与分配异常向外传播。
 * 副作用：打开并可能初始化数据库，创建卡片及其仓储记录；不联网、不访问生产默认目录。
 * 线程与生命周期：调用线程同步使用局部仓储连接；文件由测试调用方持有并在连接关闭后清理。
 */
domain::Result<bool> installSyntheticBlueprint(const std::filesystem::path& database_path);

/*
 * 功能：读取测试工作区活动分支；仅在尚无活动分支时显式创建合成根快照。
 * 参数：database_path：输入，测试拥有的临时数据库路径，调用期间有效。
 * 返回：成功为现有分支头或新根提交；失败携带活动分支查询、读取或创建错误。
 * 失败：只有 missing_context 触发创建，其他错误直接传播；构造异常向外传播。
 * 副作用：打开数据库；创建路径写入根分支和快照，已有分支路径只读，不联网。
 * 线程与生命周期：调用线程同步查询或创建；局部仓储返回前释放，返回快照自行持有数据。
 */
domain::Result<domain::CommitView> ensureSyntheticBranch(const std::filesystem::path& database_path);

/*
 * 功能：提供与合成状态人物标识一致的两个离线模型绑定。
 * 参数：无。
 * 返回：拥有两个绑定值的数组，提供商和模型均为测试协议标识。
 * 失败：分配异常向外传播，不查询模型可用性。
 * 副作用：无；不创建连接、不访问凭据、不发送请求。
 * 线程与生命周期：调用线程同步构造，返回数组拥有字段，无借用、无后台回调。
 */
std::vector<domain::ActorModelBinding> syntheticActors();

/*
 * 功能：同步推进测试活动分支一步，先查命令重放，再生成合成状态并提交。
 * 参数：database_path：输入，测试独占临时数据库路径；command_id：输入，幂等命令标识，合法性由仓储校验。
 * 返回：成功为历史重放或新提交的快照；失败携带仓储或合成提供商错误。
 * 失败：无活动分支、读取失败、暂停/结束或规则冲突均传播；构造及分配异常向外传播。
 * 副作用：新命令写入分支快照和命令记录；同步模拟延迟阻塞调用线程，重放不再次生成响应；不联网。
 * 线程与生命周期：调用线程同步执行且模拟延迟阻塞；仓储连接局部持有，无在途网络或异步退出。
 */
domain::Result<domain::CommitView> stepSyntheticBranch(
    const std::filesystem::path& database_path, const std::string& command_id);

/*
 * 功能：同步推进或恢复一个测试会话回合，按预留、事实提交、叙述完成分阶段执行。
 * 参数：database_path：输入，测试独占临时数据库路径；command_id：输入，本次操作的幂等标识，派生子命令后缀；
 *   session_id：输入，已由测试创建的会话标识；三个参数仅在调用期间借用。
 * 返回：成功为最新会话，包括需要人工审核的回合；已完成命令直接返回历史会话。
 * 失败：未知/未决调用、不可执行状态、上下文、规则或仓储错误返回失败；构造异常向外传播。
 * 副作用：可能预留预算、提交事实、完成叙述；各阶段并非一个事务，失败保留已完成阶段供恢复。
 *   粗略 token 计数按 UTF-8 字节数计算，只用于测试；全部同步运行，不联网。
 * 线程与生命周期：调用线程同步执行各持久化阶段；引用不逃逸，局部仓储连接返回前释放，无模型工作线程。
 */
domain::Result<domain::SimulationSession> stepSyntheticSession(
    const std::filesystem::path& database_path, const std::string& command_id,
    const std::string& session_id);

} // namespace xuyan::test
