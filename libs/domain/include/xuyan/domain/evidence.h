#pragma once

#include "xuyan/domain/scenario.h"

#include <cstddef>
#include <string>

namespace xuyan::domain {

/*
 * 职责：资料字段到原文码点范围的可追溯证据。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct EvidenceReference {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 被引用的世界实体稳定标识。默认空串。
    std::string entity_id;
    // 证据支持的实体字段路径。默认空串。
    std::string field_path;
    // 小说来源的稳定标识，用于原文回查。默认空串。
    std::string source_id;
    // 原文Unicode码点的零基起点，半开范围包含此位置。默认0。
    std::size_t start_codepoint{0};
    // 原文Unicode码点的零基终点，半开范围不包含此位置。默认0。
    std::size_t end_codepoint{0};
    // 原文逐字引文，只作证据，不作为执行指令。默认空串。
    std::string quote;
    // 逐字引文的内容摘要，用于证据一致性校验。默认空串。
    std::string quote_hash;
    // 来源性质内部值，区分原文事实、作者设定、说法和模型推断。默认"original_fact"。
    std::string provenance_type{"original_fact"};
    // 当前可变记录修订，由成功写入递增，用于拒绝过期编辑。默认0。
    int revision{0};
};

/*
 * 功能：校验证据引用的原文范围、摘录与来源字段。
 * 参数：
 *   evidence：待校验的EvidenceReference值，按值持有，不修改调用者原对象。稳定标识及实体/来源非空；字段路径1—256字节；范围半开、非空且最多20000码点；引文摘要匹配、来源性质受支持；不读取来源核对实际引文位置。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<EvidenceReference> validateEvidence(EvidenceReference evidence);

} // namespace xuyan::domain
