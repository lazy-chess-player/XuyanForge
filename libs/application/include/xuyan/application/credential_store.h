#pragma once

#include "xuyan/domain/scenario.h"

#include <map>
#include <string>

namespace xuyan::application {

class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;
    virtual xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) = 0;
    virtual xuyan::domain::Result<std::string> get(const std::string& reference) = 0;
    virtual xuyan::domain::Result<bool> remove(const std::string& reference) = 0;
};

class InMemoryCredentialStore final : public ICredentialStore {
public:
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override {
        secrets_[reference] = secret; return xuyan::domain::Result<bool>::success(true);
    }
    xuyan::domain::Result<std::string> get(const std::string& reference) override {
        const auto found = secrets_.find(reference);
        if (found == secrets_.end()) return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "凭据不存在", false, "重新输入凭据"});
        return xuyan::domain::Result<std::string>::success(found->second);
    }
    xuyan::domain::Result<bool> remove(const std::string& reference) override {
        return xuyan::domain::Result<bool>::success(secrets_.erase(reference) > 0);
    }
private:
    std::map<std::string, std::string> secrets_;
};

} // namespace xuyan::application

