#include "xuyan/platform/credential_store.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <string_view>

namespace xuyan::platform {
namespace {

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

xuyan::domain::Error systemError(const char* action) {
    return {xuyan::domain::ErrorCode::storage_error,
            std::string(action) + "（Windows 错误 " + std::to_string(GetLastError()) + "）",
            true, "检查当前 Windows 用户的凭据管理器后重试"};
}

} // namespace

xuyan::domain::Result<bool> SystemCredentialStore::put(const std::string& reference, const std::string& secret) {
    const auto target = wide(reference);
    if (target.empty() || secret.empty() || secret.size() > 65536) return xuyan::domain::Result<bool>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "凭据引用或内容无效", false, "重新输入凭据"});
    CREDENTIALW value{};
    value.Type = CRED_TYPE_GENERIC; value.TargetName = const_cast<wchar_t*>(target.c_str());
    value.CredentialBlobSize = static_cast<DWORD>(secret.size());
    value.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    value.Persist = CRED_PERSIST_LOCAL_MACHINE; value.UserName = const_cast<wchar_t*>(L"XuyanForge");
    if (!CredWriteW(&value, 0)) return xuyan::domain::Result<bool>::failure(systemError("无法保存系统凭据"));
    return xuyan::domain::Result<bool>::success(true);
}

xuyan::domain::Result<std::string> SystemCredentialStore::get(const std::string& reference) {
    const auto target = wide(reference);
    PCREDENTIALW value = nullptr;
    if (target.empty() || !CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &value)) {
        if (GetLastError() == ERROR_NOT_FOUND) return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "凭据不存在", false, "重新输入凭据"});
        return xuyan::domain::Result<std::string>::failure(systemError("无法读取系统凭据"));
    }
    std::string secret(reinterpret_cast<const char*>(value->CredentialBlob), value->CredentialBlobSize);
    CredFree(value);
    return xuyan::domain::Result<std::string>::success(std::move(secret));
}

xuyan::domain::Result<bool> SystemCredentialStore::remove(const std::string& reference) {
    const auto target = wide(reference);
    if (target.empty()) return xuyan::domain::Result<bool>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "凭据引用无效", false, "刷新连接后重试"});
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        if (GetLastError() == ERROR_NOT_FOUND) return xuyan::domain::Result<bool>::success(false);
        return xuyan::domain::Result<bool>::failure(systemError("无法删除系统凭据"));
    }
    return xuyan::domain::Result<bool>::success(true);
}

} // namespace xuyan::platform
