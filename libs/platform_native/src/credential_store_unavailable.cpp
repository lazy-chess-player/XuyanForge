#include "xuyan/platform/credential_store.h"

namespace xuyan::platform {
namespace {
/* 功能：统一报告未接入原生密钥环。参数：无。返回：不可自动重试的存储错误。
 * 失败：字符串分配异常可传播。副作用：不创建文件、不缓存明文，在调用线程执行。 */
xuyan::domain::Error unavailable() {
    return {xuyan::domain::ErrorCode::storage_error, "当前平台尚未接入系统凭据存储", false, "在 Windows 上运行或接入平台密钥环"};
}
}
xuyan::domain::Result<bool> SystemCredentialStore::put(const std::string&, const std::string&) { return xuyan::domain::Result<bool>::failure(unavailable()); }
xuyan::domain::Result<std::string> SystemCredentialStore::get(const std::string&) { return xuyan::domain::Result<std::string>::failure(unavailable()); }
xuyan::domain::Result<bool> SystemCredentialStore::remove(const std::string&) { return xuyan::domain::Result<bool>::failure(unavailable()); }
} // namespace xuyan::platform
