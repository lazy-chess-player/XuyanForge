#pragma once

#include "xuyan/domain/scenario.h"

#include <map>
#include <string>

namespace xuyan::application {

class ICredentialStore {
public:
    /** @brief 释放凭据存储适配器；不向调用方暴露内部存储实现。 */
    virtual ~ICredentialStore() = default;
    /** @brief 按引用保存或覆盖秘密，不把秘密写入工作区数据库。 */
    virtual xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) = 0;
    /** @brief 按引用读取秘密；调用方必须限制其生命周期和日志暴露。 */
    virtual xuyan::domain::Result<std::string> get(const std::string& reference) = 0;
    /** @brief 删除指定引用下的秘密并报告是否存在。 */
    virtual xuyan::domain::Result<bool> remove(const std::string& reference) = 0;
};

class InMemoryCredentialStore final : public ICredentialStore {
public:
    /** @brief 仅在当前进程内保存测试凭据，不提供持久化。 */
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override {
        secrets_[reference] = secret; return xuyan::domain::Result<bool>::success(true);
    }
    /** @brief 返回进程内测试凭据；不存在时返回缺失上下文错误。 */
    xuyan::domain::Result<std::string> get(const std::string& reference) override {
        const auto found = secrets_.find(reference);
        if (found == secrets_.end()) return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "凭据不存在", false, "重新输入凭据"});
        return xuyan::domain::Result<std::string>::success(found->second);
    }
    /** @brief 从进程内测试映射移除指定凭据。 */
    xuyan::domain::Result<bool> remove(const std::string& reference) override {
        return xuyan::domain::Result<bool>::success(secrets_.erase(reference) > 0);
    }
private:
    std::map<std::string, std::string> secrets_;
};

} // namespace xuyan::application
