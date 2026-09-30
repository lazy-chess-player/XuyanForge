#pragma once

#include "xuyan/domain/scenario.h"

#include <QCoreApplication>
#include <QString>

/*
 * 职责：将服务失败分类及内部状态映射为可翻译的中文提示；不读取异常正文、凭据或模型响应。
 * 生命周期：无共享可变状态，返回值独立持有；GUI 与工作线程均可调用纯映射函数。
 */
namespace view_model_text {

/*
 * 功能：为一个已失败的服务结果提供中文处理建议，隔离底层错误中的路径、响应及英文诊断。
 * 参数：error 为调用期间借用的失败值，只读取稳定分类，不显示 message 或 suggested_action。
 * 返回：非空中文提示；未识别的失败分类使用通用回退。
 * 失败：不解析外部文本；字符串分配异常沿调用栈传播。
 * 副作用：仅查询 Qt 翻译上下文，不写库、不记录错误正文、不自动重试。
 * 线程与生命周期：任意线程同步调用，不保存 error 引用。
 */
inline QString errorText(const xuyan::domain::Error& error) {
    using xuyan::domain::ErrorCode;
    switch (error.code) {
    case ErrorCode::validation_failed:
        return QCoreApplication::translate("ViewModelText", "输入或配置不符合要求，请检查字段、范围和协议设置");
    case ErrorCode::revision_conflict:
        return QCoreApplication::translate("ViewModelText", "资料已被修改，请刷新后重新确认；本次操作未覆盖最新修订");
    case ErrorCode::rule_conflict:
        return QCoreApplication::translate("ViewModelText", "操作与当前规则或权限冲突，请检查相关设置");
    case ErrorCode::missing_context:
        return QCoreApplication::translate("ViewModelText", "所需资料或上下文不存在，请刷新并重新选择");
    case ErrorCode::storage_error:
        return QCoreApplication::translate("ViewModelText", "工作区或资产读写失败，请检查文件、权限和可用空间");
    case ErrorCode::command_conflict:
        return QCoreApplication::translate("ViewModelText", "操作标识与已有记录冲突，请刷新后重新提交");
    case ErrorCode::credential_consistency_failed:
        return QCoreApplication::translate("ViewModelText", "连接保存失败且系统凭据可能与连接记录不一致；请先人工核对凭据和连接状态，暂勿重复发送");
    }
    return QCoreApplication::translate("ViewModelText", "操作失败，请检查当前资料和配置后重试");
}

/*
 * 功能：提供状态的显示标签，同时保留调用方原有协议值。
 * 参数：state 为同步借用的 UTF-8 协议值；空串或未列出的值均表示未识别。
 * 返回：中文状态标签；未识别值返回“未知状态”，不回显原始代码。
 * 失败：字符串分配异常传播，不改变调用方状态。
 * 副作用：只查询翻译上下文；任意线程同步调用，不保存引用。
 */
inline QString stateLabel(const std::string& state) {
    if (state == "queued") return QCoreApplication::translate("ViewModelText", "等待处理");
    if (state == "ready") return QCoreApplication::translate("ViewModelText", "已就绪");
    if (state == "running") return QCoreApplication::translate("ViewModelText", "处理中");
    if (state == "completed") return QCoreApplication::translate("ViewModelText", "已完成");
    if (state == "failed") return QCoreApplication::translate("ViewModelText", "失败");
    if (state == "unknown") return QCoreApplication::translate("ViewModelText", "结果未知");
    if (state == "cancelled") return QCoreApplication::translate("ViewModelText", "已取消");
    if (state == "cancelling") return QCoreApplication::translate("ViewModelText", "正在取消");
    if (state == "candidate") return QCoreApplication::translate("ViewModelText", "待校对");
    if (state == "accepted") return QCoreApplication::translate("ViewModelText", "已接受");
    if (state == "rejected") return QCoreApplication::translate("ViewModelText", "已拒绝");
    if (state == "conflicted") return QCoreApplication::translate("ViewModelText", "有冲突");
    return QCoreApplication::translate("ViewModelText", "未知状态");
}
}
