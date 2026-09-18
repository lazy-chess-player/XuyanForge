#pragma once

#include "xuyan/application/provider_generation_service.h"

namespace xuyan::providers {

class QtProviderTransport final : public xuyan::application::IProviderTransport {
public:
    xuyan::domain::Result<xuyan::application::ProviderTransportResponse> send(
        const ProviderHttpRequest& request, const std::string& credential,
        int timeout_ms) override;
};

} // namespace xuyan::providers
