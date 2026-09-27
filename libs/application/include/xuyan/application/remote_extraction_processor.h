#pragma once

#include "xuyan/application/provider_generation_service.h"
#include "xuyan/domain/extraction_job.h"

#include <filesystem>

namespace xuyan::application {

class RemoteExtractionProcessor {
public:
    /** @brief 绑定工作区、凭据存储和传输端口；构造过程不发送任何请求。 */
    RemoteExtractionProcessor(std::filesystem::path database_path, ICredentialStore& credentials,
                              IProviderTransport& transport);
    /** @brief 仅显式执行下一步远程抽样，并在发送前复核连接快照与原文证据。 */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
