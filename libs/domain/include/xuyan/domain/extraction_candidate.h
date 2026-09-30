#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/world_graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：模型或离线处理产生的待审候选，保留冻结协议和原文证据。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionCandidate {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 产生候选或步骤的解析任务稳定标识。默认空串。
    std::string job_id;
    // 产生候选的任务内步骤序号，单位为步骤。默认0。
    int step_ordinal{0};
    // 小说来源的稳定标识，用于原文回查。默认空串。
    std::string source_id;
    // 候选类别内部值，决定字段校验与专用投影路径。默认空串。
    std::string candidate_type;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 候选字段JSON正文，必须按冻结协议验证。默认"{}"。
    std::string fields_json{"{}"};
    // 原文Unicode码点的零基起点，半开范围包含此位置。默认0。
    std::size_t start_codepoint{0};
    // 原文Unicode码点的零基终点，半开范围不包含此位置。默认0。
    std::size_t end_codepoint{0};
    // 原文逐字引文，只作证据，不作为执行指令。默认空串。
    std::string quote;
    // 逐字引文的内容摘要，用于证据一致性校验。默认空串。
    std::string quote_hash;
    // 来源性质内部值，区分原文事实、作者设定、说法和模型推断。默认"model_inference"。
    std::string provenance_type{"model_inference"};
    // 人工审核的内部状态值，待审候选不能自动成为确认事实。默认"candidate"。
    std::string review_status{"candidate"};
    // 冻结的候选结构协议版本，不在任务恢复时自动升级。默认"candidate-v1"。
    std::string schema_version{"candidate-v1"};
    // 冻结的提示词版本，参与缓存和模型请求身份。默认"extract-v1"。
    std::string prompt_version{"extract-v1"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};


/*
 * 职责：同世界、来源及审核条件下的一页候选及其总数。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct ExtractionCandidatePage {
    // 当前页的轻量条目集合，调用者只显示本页。默认空集合，不预填资料。
    std::vector<ExtractionCandidate> items;
    // 同一查询范围匹配的条目总数，单位为条目。默认0。
    std::uint64_t total{0};
    // 分页返回的条目上限，单位为条目。默认0。
    int limit{0};
    // 分页的零基条目偏移，不是字节或码点位置。默认0。
    std::int64_t offset{0};
};


/*
 * 职责：作者明确选择的已有实体与修订，不提供自动绑定。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct CandidateEntitySelection {
    // 被引用的世界实体稳定标识。默认空串。
    std::string entity_id;
    // 明确选择目标时读取的实体修订，提交前再次核对。默认0。
    int expected_revision{0};
};


/*
 * 职责：同世界已确认实体的轻量精确身份线索。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct RelationEndpointMatch {
    // 被引用的世界实体稳定标识。默认空串。
    std::string entity_id;
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 实体分类的内部协议值，显示层提供中文名称。默认空串。
    std::string kind;
    // 当前实体的完整别名列表，仅用于逐字身份线索。默认空集合，不预填资料。
    std::vector<std::string> aliases;
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
    // 逐字标识是否命中目标当前主名称。默认false。
    bool name_match{false};
    // 逐字标识是否命中目标某个完整别名。默认false。
    bool alias_match{false};
};


/*
 * 职责：同一读快照中的逐字端点匹配页。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct RelationEndpointMatchPage {
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 关系端点的逐字提及，不能据此自动选择同名对象。默认空串。
    std::string mention;
    // 当前页的轻量条目集合，调用者只显示本页。默认空集合，不预填资料。
    std::vector<RelationEndpointMatch> items;
    // 同一查询范围匹配的条目总数，单位为条目。默认0。
    std::uint64_t total{0};
    // 分页返回的条目上限，单位为条目。默认0。
    int limit{0};
    // 分页的零基条目偏移，不是字节或码点位置。默认0。
    std::int64_t offset{0};
};


/*
 * 职责：候选主名称及完整别名匹配的同世界同类型目标页。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct CandidateEntityMatchPage {
    // 用于身份匹配的当前候选稳定标识。默认空串。
    std::string candidate_id;
    // 匹配请求绑定的候选修订，防止建议来源过期。默认0。
    int candidate_revision{0};
    // 所属世界的稳定标识，查询和写入必须据此隔离。默认空串。
    std::string world_id;
    // 实体分类的内部协议值，显示层提供中文名称。默认空串。
    std::string kind;
    // 当前页的轻量条目集合，调用者只显示本页。默认空集合，不预填资料。
    std::vector<RelationEndpointMatch> items;
    // 同一查询范围匹配的条目总数，单位为条目。默认0。
    std::uint64_t total{0};
    // 分页返回的条目上限，单位为条目。默认0。
    int limit{0};
    // 分页的零基条目偏移，不是字节或码点位置。默认0。
    std::int64_t offset{0};
};

/*
 * 功能：校验待审核抽取候选及其原文证据范围。
 * 参数：
 *   candidate：待校验的ExtractionCandidate值，按值持有，不修改调用者原对象。版本对须为v1/v2/v3对应组合；标识/步骤/四类类型/名称有效，名称不超过512字节；引文范围最多20000码点且摘要一致；字段对象外形及1MiB上限、来源性质有效；不替代类型化JSON协议或原文定位校验。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<ExtractionCandidate> validateExtractionCandidate(ExtractionCandidate candidate);

} // namespace xuyan::domain
