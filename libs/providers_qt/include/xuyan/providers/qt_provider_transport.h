#pragma once

#include "xuyan/application/provider_generation_service.h"

namespace xuyan::providers {

class QtProviderTransport final : public xuyan::application::IProviderTransport {
public:
    /** @brief 执行一次带超时的模型 HTTP 请求，并将响应归一化返回。 */
    xuyan::domain::Result<xuyan::application::ProviderTransportResponse> send(
        const ProviderHttpRequest& request, const std::string& credential,
        int timeout_ms) override;
};

} // namespace xuyan::providers
