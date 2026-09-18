#pragma once

#include "xuyan/application/credential_store.h"

namespace xuyan::platform {

class SystemCredentialStore final : public xuyan::application::ICredentialStore {
public:
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override;
    xuyan::domain::Result<std::string> get(const std::string& reference) override;
    xuyan::domain::Result<bool> remove(const std::string& reference) override;
};

} // namespace xuyan::platform
