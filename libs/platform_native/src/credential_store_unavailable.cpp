#include "xuyan/platform/credential_store.h"

namespace xuyan::platform {
namespace {
xuyan::domain::Error unavailable() {
    return {xuyan::domain::ErrorCode::storage_error, "当前平台尚未接入系统凭据存储", false, "在 Windows 上运行或接入平台密钥环"};
}
}
xuyan::domain::Result<bool> SystemCredentialStore::put(const std::string&, const std::string&) { return xuyan::domain::Result<bool>::failure(unavailable()); }
xuyan::domain::Result<std::string> SystemCredentialStore::get(const std::string&) { return xuyan::domain::Result<std::string>::failure(unavailable()); }
xuyan::domain::Result<bool> SystemCredentialStore::remove(const std::string&) { return xuyan::domain::Result<bool>::failure(unavailable()); }
} // namespace xuyan::platform
