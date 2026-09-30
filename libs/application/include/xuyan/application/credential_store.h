#pragma once

#include "xuyan/domain/scenario.h"

#include <map>
#include <string>

namespace xuyan::application {

/* 凭据存储端口；生产实现使用系统设施，秘密只按引用访问。所有调用同步，线程安全由具体适配器声明。 */
class ICredentialStore {
public:
    /*
     * 功能：通过基类安全销毁适配器。参数：无。返回：完成派生资源释放。
     * 失败：析构不得抛出；副作用：释放适配器资源，不要求删除持久化凭据。
     * 线程与生命周期：销毁前调用方须结束全部借用该端口的服务调用。
     */
    virtual ~ICredentialStore() = default;
    /*
     * 功能：按引用保存/覆盖秘密。参数：reference 为凭据键；secret 为借用秘密，调用期间有效，输入限制由实现校验。
     * 返回：Result<bool> 表示保存结果，成功布尔语义由适配器定义。失败：系统设施或输入错误返回 Result，异常策略由实现声明。
     * 副作用：仅凭据设施改变，不写工作区数据库或日志；线程：同步，实现不得在返回后借用 secret。
     */
    virtual xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) = 0;
    /*
     * 功能：读取凭据。参数：reference 为凭据键，借用至返回。
     * 返回：拥有秘密的字符串，调用方负责缩短其生命期和尽力清除副本。失败：不存在返回 missing_context，其他设施错误不得伪装成不存在。
     * 副作用：读取凭据设施，不输出日志；线程：同步，不保留 reference 引用，调用方遵循适配器归属。
     */
    virtual xuyan::domain::Result<std::string> get(const std::string& reference) = 0;
    /*
     * 功能：删除凭据键。参数：reference 为待移除键，借用至返回。
     * 返回：true 表示删除存在项、false 表示不存在，具体系统适配器可按其设施契约报告。失败：设施删除错误返回 Result。
     * 副作用：删除秘密，不删除工作区连接元数据；线程：同步，调用方确保没有同键并发修改。
     */
    virtual xuyan::domain::Result<bool> remove(const std::string& reference) = 0;
};

/* 无预置凭据的进程内测试替身；拥有秘密映射，不持久化、不访问网络。非线程安全，测试需独占实例。 */
class InMemoryCredentialStore final : public ICredentialStore {
public:
    /*
     * 功能：保存或覆盖测试秘密。参数：reference 为键，允许空键；secret 为复制到映射的值，允许空串。
     * 返回：成功 true。失败：容器/字符串分配异常传播，不模拟系统权限失败。
     * 副作用：更新内存映射，旧字符串释放不保证安全擦除；线程：仅独占调用，值随替身持有至覆盖/删除/销毁。
     */
    xuyan::domain::Result<bool> put(const std::string& reference, const std::string& secret) override {
        secrets_[reference] = secret; return xuyan::domain::Result<bool>::success(true);
    }
    /*
     * 功能：读取测试秘密。参数：reference 为键，空键亦按原值查找。
     * 返回：秘密自有副本。失败：不存在返回 missing_context；复制分配异常传播。
     * 副作用：只读映射，不记录秘密；线程：仅独占调用，副本由调用方负责清除。
     */
    xuyan::domain::Result<std::string> get(const std::string& reference) override {
        const auto found = secrets_.find(reference);
        if (found == secrets_.end()) return xuyan::domain::Result<std::string>::failure(
            {xuyan::domain::ErrorCode::missing_context, "凭据不存在", false, "重新输入凭据"});
        return xuyan::domain::Result<std::string>::success(found->second);
    }
    /*
     * 功能：删除测试凭据。参数：reference 为键，可为空。
     * 返回：true 删除存在项，false 无此键。失败：不模拟系统设施错误。
     * 副作用：释放映射中的秘密，但不保证已分配内存擦除；线程：仅独占调用，不影响此前返回的副本。
     */
    xuyan::domain::Result<bool> remove(const std::string& reference) override {
        return xuyan::domain::Result<bool>::success(secrets_.erase(reference) > 0);
    }
private:
    /* 凭据键到秘密的拥有映射，默认空；put 写入、get 复制、remove 删除，随替身销毁，禁止日志输出。 */
    std::map<std::string, std::string> secrets_;
};

} // namespace xuyan::application
