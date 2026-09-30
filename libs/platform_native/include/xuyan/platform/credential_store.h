#pragma once

#include "xuyan/application/credential_store.h"

namespace xuyan::platform {

/* 系统凭据适配器，无实例级缓存或持有句柄。Windows 使用当前登录用户的通用凭据，
 * CRED_PERSIST_LOCAL_MACHINE 表示该用户在本机后续登录可读，不是向其他用户共享。
 * 其他平台显式返回尚未接入，不降级到文件。调用同步执行；同一引用的并发覆盖由调用方协调。
 * 销毁适配器不删除持久化凭据；返回的明文由调用方限制生命周期并安全清理。 */
class SystemCredentialStore final : public xuyan::application::ICredentialStore {
public:
    /* 功能：保存或覆盖当前用户指定引用的密钥。
     * 参数：reference 为非空、无零字节的 UTF-8 引用，转换后的 UTF-16 单元数不得超过系统目标名上限；
     *       secret 为非空明文字节串，接口预检上限为 65536 字节，操作系统仍施加自己的凭据大小限制。
     *       两者借用至调用结束，所有权仍在调用方，本函数不擦除调用方输入。
     * 返回：Windows 写入成功为 success(true)。失败：Windows 非法输入为校验错误，系统失败为存储错误；
     *       非 Windows 不检查输入，直接返回不可自动重试的未接入存储错误；分配异常传播。
     * 副作用：Windows 更新系统凭据并覆盖旧秘密；非 Windows 不访问凭据设施；不写工作区、文件或日志。
     * 线程：调用线程同步执行，不保存回调；同引用的读写需由上层控制顺序。 */
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override;
    /* 功能：读取当前用户的指定密钥，不创建缺失凭据。
     * 参数：reference 为调用内借用的非空 UTF-8 引用，不含零字节，满足系统目标名限制。
     * 返回：Windows 成功为拥有型明文字节串，系统中的零长度 blob 返回空串，调用方负责清理；不存在为 missing_context。
     * 失败：Windows 非法引用为校验错误，系统失败或损坏 blob 为存储错误；
     *       非 Windows 不检查输入，直接返回不可自动重试的未接入存储错误；分配异常传播。
     * 副作用：Windows 只读持久化凭据，系统返回缓冲在正常返回或异常展开时擦除并释放；非 Windows 不访问凭据设施。
     * 线程：调用线程同步执行，返回值不依赖适配器生命周期，不记录明文。 */
    xuyan::domain::Result<std::string> get(const std::string& reference) override;
    /* 功能：删除当前用户指定引用的系统凭据。
     * 参数：reference 为调用内借用的非空、无零字节 UTF-8 引用，满足系统目标名限制。
     * 返回：Windows 删除为 success(true)，原本不存在为 success(false)。
     * 失败：Windows 非法引用为校验错误，系统失败为存储错误；
     *       非 Windows 不检查输入，直接返回不可自动重试的未接入存储错误；分配异常传播。
     * 副作用：Windows 删除持久化秘密，无法通过此接口恢复；非 Windows 不删除任何记录；不改连接元数据。
     * 线程：同步执行，不持有跨调用资源；不撤回其他调用已经取得的明文副本。 */
    xuyan::domain::Result<bool> remove(const std::string& reference) override;
};

} // namespace xuyan::platform
