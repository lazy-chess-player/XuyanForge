#pragma once

#include "xuyan/domain/scenario.h"

#include <string>

namespace xuyan::domain {

struct ProviderConnection {
    std::string id;
    std::string name;
    std::string kind;
    std::string endpoint;
    std::string default_model;
    std::string credential_ref;
    std::string data_policy{"remote_allowed"};
    bool enabled{true};
    bool deleted{false};
    int revision{0};
};

Result<ProviderConnection> validateProviderConnection(ProviderConnection connection);

} // namespace xuyan::domain
