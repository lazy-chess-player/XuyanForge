#include "xuyan/platform/credential_store.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

#include <limits>
#include <memory>
#include <string_view>

namespace xuyan::platform {
namespace {

/* 功能：将凭据目标名严格转为 UTF-16，拒绝零字节和系统无法完整表示的长度。
 * 参数：value 为调用期间有效的 UTF-8 字节视图。返回：拥有型宽串；非法/空输入为 empty。
 * 失败：编码或长度错误不读取旧 GetLastError，分配异常传播。
 * 副作用：仅调用编码转换，在调用线程执行，不接触密钥。 */
std::wstring wide(std::string_view value) {
    if (value.empty() || value.find('\0') != std::string_view::npos
        || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0 || count > CRED_MAX_GENERIC_TARGET_NAME_LENGTH) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), count) != count) return {};
    return result;
}

/* 功能：将已捕获的系统错误包装为中文。参数：action 为非空中文操作说明，借用至返回；
 *       code 为失败 API 后立即保存的 Windows 错误码，避免分配或清理改写线程错误状态。
 * 返回：可人工重试的存储错误。失败：分配异常传播。
 * 副作用：不读取系统错误文本、不记录引用或明文，同步构造值。 */
xuyan::domain::Error systemError(const char* action, DWORD code) {
    return {xuyan::domain::ErrorCode::storage_error,
            std::string(action) + "（Windows 错误 " + std::to_string(code) + "）",
            true, "检查当前 Windows 用户的凭据管理器后重试"};
}

/* CredReadW 返回内存的无状态释放器；仅用于系统分配的 CREDENTIALW，随 unique_ptr 管理生命周期。
 * 系统 blob 是本调用拥有的可写缓冲，释放前清除明文，不修改系统持久化记录。 */
struct CredentialReleaser {
    /* 功能：擦除并释放一次系统返回的凭据内存。
     * 参数：value 为拥有的系统分配指针，可空；不得传入栈对象。
     * 返回：无。失败：不抛异常。副作用：清零 blob 并 CredFree；在持有者销毁线程执行。 */
    void operator()(CREDENTIALW* value) const noexcept {
        if (value == nullptr) return;
        if (value->CredentialBlob != nullptr && value->CredentialBlobSize != 0)
            SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
        CredFree(value);
    }
};

} // namespace

xuyan::domain::Result<bool> SystemCredentialStore::put(const std::string& reference, const std::string& secret) {
    const auto target = wide(reference);
    // 保留既有接口预检边界；工具链头文件的旧宏可能小于当前系统限制，实际容量由 CredWriteW 判定。
    if (target.empty() || secret.empty() || secret.size() > 65536) return xuyan::domain::Result<bool>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "凭据引用或内容无效", false, "重新输入凭据"});
    /* 本次写入描述，零初始化后绑定类型/路径/正文/持久化策略；本身不拥有字段指针。
     * ABI 要求可写指针，但 CredWriteW 仅同步读取；目标名借用 target、正文借用 secret，所有借用在返回前结束。 */
    CREDENTIALW value{};
    value.Type = CRED_TYPE_GENERIC; value.TargetName = const_cast<wchar_t*>(target.c_str());
    value.CredentialBlobSize = static_cast<DWORD>(secret.size());
    value.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    value.Persist = CRED_PERSIST_LOCAL_MACHINE; value.UserName = const_cast<wchar_t*>(L"XuyanForge");
    if (!CredWriteW(&value, 0)) return xuyan::domain::Result<bool>::failure(systemError("无法保存系统凭据", GetLastError()));
    return xuyan::domain::Result<bool>::success(true);
}

xuyan::domain::Result<std::string> SystemCredentialStore::get(const std::string& reference) {
    const auto target = wide(reference);
    if (target.empty()) return xuyan::domain::Result<std::string>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "凭据引用无效", false, "刷新连接后重试"});
    /* 系统读取的输出指针，初始空；读取成功立即交给 owned 管理，后续只在本调用中观察。 */
    PCREDENTIALW value = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &value)) {
        const auto code = GetLastError();
        if (code == ERROR_NOT_FOUND) return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "凭据不存在", false, "重新输入凭据"});
        return xuyan::domain::Result<std::string>::failure(systemError("无法读取系统凭据", code));
    }
    /* 先收归 RAII 再分配返回字符串，保证 bad_alloc 等异常也释放并擦除系统明文；
     * secret 返回副本不依赖系统缓冲，副本清理由调用方负责，不能宣称全部明文都已擦除。 */
    std::unique_ptr<CREDENTIALW, CredentialReleaser> owned(value);
    if (value == nullptr || (value->CredentialBlobSize != 0 && value->CredentialBlob == nullptr))
        return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::storage_error, "系统凭据内容无效", false, "重新保存凭据"});
    std::string secret;
    if (value->CredentialBlobSize != 0)
        secret.assign(reinterpret_cast<const char*>(value->CredentialBlob), value->CredentialBlobSize);
    return xuyan::domain::Result<std::string>::success(std::move(secret));
}

xuyan::domain::Result<bool> SystemCredentialStore::remove(const std::string& reference) {
    const auto target = wide(reference);
    if (target.empty()) return xuyan::domain::Result<bool>::failure(
        {xuyan::domain::ErrorCode::validation_failed, "凭据引用无效", false, "刷新连接后重试"});
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        const auto code = GetLastError();
        if (code == ERROR_NOT_FOUND) return xuyan::domain::Result<bool>::success(false);
        return xuyan::domain::Result<bool>::failure(systemError("无法删除系统凭据", code));
    }
    return xuyan::domain::Result<bool>::success(true);
}

} // namespace xuyan::platform
