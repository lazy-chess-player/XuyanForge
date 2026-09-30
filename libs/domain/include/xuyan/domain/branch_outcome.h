#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_version.h"

#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：一个状态字段在两条冻结分支上的差异。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct BranchDifference {
    // 存在差异的状态字段路径。默认空串。
    std::string field;
    // 左侧分支该字段的序列化值。默认空串。
    std::string left_value;
    // 右侧分支该字段的序列化值。默认空串。
    std::string right_value;
};

/*
 * 职责：共同祖先之后的分支路径、状态差异及模型用量比较。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct BranchComparison {
    // 比较左侧分支标识。默认空串。
    std::string left_branch_id;
    // 比较右侧分支标识。默认空串。
    std::string right_branch_id;
    // 两个分支的共同祖先提交标识。默认空串。
    std::string common_commit_id;
    // 比较时冻结的左侧分支头提交标识。默认空串。
    std::string left_head_commit_id;
    // 比较时冻结的右侧分支头提交标识。默认空串。
    std::string right_head_commit_id;
    // 共同祖先之后左侧分支的提交路径。默认空集合，不预填资料。
    std::vector<std::string> left_commit_ids;
    // 共同祖先之后右侧分支的提交路径。默认空集合，不预填资料。
    std::vector<std::string> right_commit_ids;
    // 冻结左右状态的字段差异列表。默认空集合，不预填资料。
    std::vector<BranchDifference> differences;
    // 左侧比较范围内的模型调用次数，单位为次。默认0。
    int left_calls{0};
    // 右侧比较范围内的模型调用次数，单位为次。默认0。
    int right_calls{0};
    // 左侧比较范围内实际输入词元数。默认0。
    int left_input_tokens{0};
    // 右侧比较范围内实际输入词元数。默认0。
    int right_input_tokens{0};
    // 左侧比较范围内实际输出词元数。默认0。
    int left_output_tokens{0};
    // 右侧比较范围内实际输出词元数。默认0。
    int right_output_tokens{0};
    // 左侧结果未知的在途或中断调用次数。默认0。
    int left_unknown_calls{0};
    // 右侧结果未知的在途或中断调用次数。默认0。
    int right_unknown_calls{0};
};

} // namespace xuyan::domain
