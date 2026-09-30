#pragma once

#include <optional>
#include <string>

namespace xuyan::application::detail {

/*
 * 同步调用内的秘密清除守卫；非拥有地观察字符串或可选字符串，在被观察值销毁前尽力覆盖有效字符。
 * 不保证分配器旧缓冲、外部副本或传输内部副本清除；不提供锁，必须与被观察值处于同一调用线程。
 */
class CredentialScope final {
public:
    /*
     * 功能：观察本地秘密字符串。参数：secret 为可写借用字符串，须长于本守卫。
     * 返回：完成观察绑定。失败：无，构造不分配。
     * 副作用：暂不清除；线程与生命周期：同线程使用，退出作用域时覆盖当时有效字符。
     */
    explicit CredentialScope(std::string& secret) noexcept : secret_(&secret) {}

    /*
     * 功能：观察可选秘密，包含后续才填入的旧凭据备份。
     * 参数：secret 为可写可选字符串引用，须长于守卫；初始无值也允许。
     * 返回：完成观察绑定。失败：无。副作用：暂不修改内容；线程：同调用线程，析构时检查是否有值。
     */
    explicit CredentialScope(std::optional<std::string>& secret) noexcept : optional_secret_(&secret) {}

    /* 功能：禁止复制双重清除守卫。参数：另一守卫。返回：无，编译期禁止。失败：编译拒绝；副作用：无；线程：不适用。 */
    CredentialScope(const CredentialScope&) = delete;
    /* 功能：禁止赋值转移观察目标。参数：另一守卫。返回：无，编译期禁止。失败：编译拒绝；副作用：无；线程：不适用。 */
    CredentialScope& operator=(const CredentialScope&) = delete;

    /*
     * 功能：成功或异常退出时覆盖本地秘密。参数：无。返回：完成覆盖并清空有效字符串。
     * 失败：不抛异常，不能保证所有历史副本清除。
     * 副作用：通过易变字符写覆盖有效字节，不在日志或数据库中保留秘密。
     * 线程与生命周期：被观察值仍须存活，不与其他线程并发读取/修改。
     */
    ~CredentialScope() noexcept {
        if (secret_ != nullptr) erase(*secret_);
        if (optional_secret_ != nullptr && optional_secret_->has_value()) erase(**optional_secret_);
    }

private:
    /*
     * 功能：尽力覆盖单个字符串的当前有效字节。
     * 参数：secret 为本调用独占的可写字符串，空串允许。返回：无。失败：不抛异常。
     * 副作用：覆盖后 clear，不缩容或释放旧分配记录；线程：同步，只在被观察对象存活时调用。
     */
    static void erase(std::string& secret) noexcept {
        volatile char* bytes = secret.data();
        for (std::size_t index = 0; index < secret.size(); ++index) bytes[index] = '\0';
        secret.clear();
    }

    /* 非拥有字符串观察指针，默认空；字符串重载构造设置，析构读取，目标须长于守卫。 */
    std::string* secret_{nullptr};
    /* 非拥有可选字符串观察指针，默认空；可选重载构造设置，析构检查当前值，目标须长于守卫。 */
    std::optional<std::string>* optional_secret_{nullptr};
};

} // namespace xuyan::application::detail
