#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <vector>

namespace xuyan::domain {

/*
 * 职责：可编辑人物卡的一份历史版本，和入场后的实例状态分开。
 * 生命周期：值对象独立持有字段，由调用者管理副本，不拥有数据库、线程或网络资源。
 * 约束：默认字段不代表预置世界或验证事实；写入和发送前由对应服务继续校验。
 */
struct CharacterBlueprint {
    // 本对象的稳定标识，由创建调用方或仓储分配，用于关联而非显示名称。默认空串。
    std::string id;
    // 人物卡的不可变版本号，由版本写入路径递增。默认0。
    int version{0};
    // 用户或原文提供的显示名称，不用于自动合并同名对象。默认空串。
    std::string name;
    // 人物卡的概括说明，属于作者可编辑正文。默认空串。
    std::string summary;
    // 人物核心价值取向文本列表，供卡片编辑与上下文生成读取。默认空集合，不预填资料。
    std::vector<std::string> values;
    // 性格特征文本列表，不包含预置人物。默认空集合，不预填资料。
    std::vector<std::string> traits;
    // 人物长期目标描述，由作者维护。默认空串。
    std::string long_term_goal;
    // 人物当前短期目标描述，与长期目标独立。默认空串。
    std::string short_term_goal;
    // 人物语言风格说明，用作上下文资料。默认空串。
    std::string speech_style;
    // 人物能力的JSON数组文本，由校验器限制结构与长度。默认"[]"。
    std::string abilities_json{"[]"};
    // 人物装备条目文本列表，由作者维护。默认空集合，不预填资料。
    std::vector<std::string> equipment;
    // 人物经历和背景正文。默认空串。
    std::string background;
    // 作者私人备注，检索和导出须遵守私有信息边界。默认空串。
    std::string private_notes;
    // 扩展字段JSON对象文本，供不破坏既定字段的扩展使用。默认"{}"。
    std::string extensions_json{"{}"};
    // 软删除标志，写入接口维护，已发布历史不因此丢失。默认false。
    bool deleted{false};
};

/*
 * 功能：校验并规范化人物卡字段；失败时返回可供界面展示的错误。
 * 参数：
 *   blueprint：待校验的CharacterBlueprint值，按值持有，不修改调用者原对象。名称1—512字节且无禁用控制字符；摘要不超过16KiB，背景/私注各不超过256KiB；价值/特征各最多128项、装备最多256项；能力/扩展仅检查数组/对象外形，不保证完整JSON语法；返回值移除空项、排序去重。
 * 返回：成功为规范化值的Result；无效字段为含中文原因的validation_failed，不将错误当作空值。
 * 失败：校验以错误结果返回；值复制或缓冲分配失败可传播标准异常。
 * 副作用：只处理内存值，不访问数据库、文件、网络或凭据，不生成默认资料。
 */
Result<CharacterBlueprint> validateBlueprint(CharacterBlueprint blueprint);

} // namespace xuyan::domain
