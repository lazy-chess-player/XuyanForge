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
    /**
     * @brief 仅显式执行下一步类型化远程抽样，并在发送前复核协议、连接快照和原文证据。
     * @details 旧远程任务不自动改版本或重发；整份字段与证据通过后才原子写入待审候选。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> processNext(const std::string& job_id);
private:
    std::filesystem::path database_path_;
    ICredentialStore& credentials_;
    IProviderTransport& transport_;
};

} // namespace xuyan::application
