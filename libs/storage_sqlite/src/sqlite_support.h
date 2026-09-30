#pragma once

#include "xuyan/domain/scenario.h"
#include <sqlite3.h>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace xuyan::storage::detail {

/* 功能：取得数据库文件对应的进程内共享写锁，用于事务和结构迁移采用同一协调边界。
 * 参数：database为有效连接，只读取其文件名。返回：拥有互斥量的共享指针；调用者自行取得锁租约。
 * 副作用：更新短临界区保护的弱引用登记表；路径规范化或分配失败可抛异常。
 */
std::shared_ptr<std::mutex> writeMutexFor(sqlite3* database);

/*
 * 职责：独占一个预编译 SQLite 语句，所有正常返回及异常路径均释放语句。
 * 生命周期：不拥有数据库；连接须比对象活得更久，仅在创建线程中操作。
 */
class Statement final {
public:
    /* 功能：在借用的连接上预编译SQL，不立即执行。
     * 参数：database为已打开的非空连接；sql为调用期间有效、零终止的固定SQL。
     * 返回：初始化后的语句对象。失败：预编译失败抛runtime_error，并清理部分创建的语句。
     */
    Statement(sqlite3* database, const char* sql);
    /* 功能：释放独占语句。参数：无。返回：无。副作用：销毁SQLite执行游标；析构不抛异常。 */
    ~Statement();
    /* 功能：禁止复制独占语句。参数：另一个语句对象。返回：无，此操作在编译期禁止。 */
    Statement(const Statement&) = delete;
    /* 功能：禁止复制赋值。参数：另一个语句对象。返回：无，此操作在编译期禁止。 */
    Statement& operator=(const Statement&) = delete;
    /* 功能：借用底层语句。参数：无。返回：非拥有指针，仅在本对象存活期间有效；不转移资源。 */
    sqlite3_stmt* get() const noexcept { return statement_; }
private:
    // 本对象独占的语句句柄；初始化为空，构造成功后由析构释放，不交给外部所有者。
    sqlite3_stmt* statement_{nullptr};
};

/* 职责：用同数据库进程锁协调独立连接的写事务；未提交的事务离开作用域时回滚。
 * 生命周期：借用连接，拥有共享锁的租约；SQL/状态读写均在调用线程，不能跨网络等待。
 */
class Transaction final {
public:
    /* 功能：取得进程内写锁并开启立即事务。
     * 参数：database为本线程独占使用的已打开连接，不能已有活动事务。
     * 返回：持有事务及锁的对象。失败：SQLite拒绝开启时抛runtime_error，自动释放已取得的锁。
     */
    explicit Transaction(sqlite3* database);
    /* 功能：未提交时回滚并释放写锁。参数：无。返回：无；回滚错误不能从析构抛出。 */
    ~Transaction();
    /* 功能：禁止复制事务。参数：另一个事务。返回：无，此操作在编译期禁止。 */
    Transaction(const Transaction&) = delete;
    /* 功能：禁止复制赋值。参数：另一个事务。返回：无，此操作在编译期禁止。 */
    Transaction& operator=(const Transaction&) = delete;
    /* 功能：提交当前事务。参数：无。返回：无。
     * 失败：提交失败抛runtime_error，保留未提交标记供析构回滚。副作用：成功后写入对其他连接可见。
     */
    void commit();
private:
    // 借用的连接，始终由仓储拥有；须覆盖事务对象生命周期。
    sqlite3* database_;
    // 当前数据库对应的进程内锁，共享拥有，保证加锁期间锁对象不会销毁。
    std::shared_ptr<std::mutex> write_mutex_;
    // 作用域锁租约；成员析构顺序保证先解锁，再释放共享锁对象。
    std::unique_lock<std::mutex> write_lock_;
    // 是否已经成功提交，初始false；仅commit修改，析构据此决定是否回滚。
    bool committed_{false};
};

/* 职责：跨多条只读查询维持同一SQLite快照，使分页总数与页面结果一致。
 * 生命周期：借用同线程连接，不取得进程写锁；首次读取确定快照，离开作用域后释放。
 */
class ReadTransaction final {
public:
    /* 功能：开启延迟事务。参数：database为有效且没有活动事务的连接。
     * 返回：初始化后的读事务；开启失败抛runtime_error。副作用：首次读后保持数据库快照。
     */
    explicit ReadTransaction(sqlite3* database);
    /* 功能：未结束时回滚以释放快照。参数：无。返回：无；不修改资料，不抛异常。 */
    ~ReadTransaction();
    /* 功能：禁止复制读事务。参数：另一个读事务。返回：无，此操作在编译期禁止。 */
    ReadTransaction(const ReadTransaction&) = delete;
    /* 功能：禁止复制赋值。参数：另一个读事务。返回：无，此操作在编译期禁止。 */
    ReadTransaction& operator=(const ReadTransaction&) = delete;
    /* 功能：结束读快照。参数：无。返回：无；提交失败抛runtime_error，析构仍尝试回滚。 */
    void commit();
private:
    // 借用的数据库连接；所有读语句与事务在同一线程执行。
    sqlite3* database_;
    // 快照是否已成功结束，初始false；仅commit更新。
    bool committed_{false};
};

/* 功能：按参数位置绑定完整UTF-8字节串，让SQLite复制内容而不借用调用者内存。
 * 参数：statement为有效语句；index为从1开始的绑定位置；value为输入文本，可为空或包含零字节。
 * 返回：无。失败：长度超过SQLite整型上限或绑定失败时抛runtime_error。副作用：替换该位置的绑定值。
 */
void bindText(sqlite3_stmt* statement, int index, const std::string& value);
/* 功能：复制当前结果行的文本列。参数：statement为已步进到行的语句；column为零基列号。
 * 返回：按SQLite字节长度复制的字符串，NULL返回空串，零字节不截断；结果不借用语句内存。
 */
std::string columnText(sqlite3_stmt* statement, int column);
/* 功能：执行只读语句的下一步，区分结果行、正常结束及真正的数据库错误。
 * 参数：statement为有效语句，仍由调用方拥有。返回：有一行结果为true，正常结束为false。
 * 失败：其他SQLite状态抛runtime_error，不将查询失败当成空列表。副作用：推进语句游标。
 */
bool stepRow(sqlite3_stmt* statement);
/* 功能：为新数据库记录生成带命名空间的随机标识。
 * 参数：prefix为调用期间借用的内部前缀，不含用户原文。返回：前缀、连字符及16位十六进制随机值。
 * 副作用：更新本线程随机发生器；随机源初始化失败可抛异常，不承担跨库唯一性证明。
 */
std::string randomId(std::string_view prefix);
/* 功能：生成持久化记录的当前UTC时间。参数：无。返回：精确到秒的ISO日期时间，以Z结尾；不修改资料。 */
std::string utcNow();
/* 功能：把内部存储异常转换为面向用户的中文错误，不展示SQLite英文细节。
 * 参数：exception为调用期间有效的异常引用。返回：可重试的storage_error及中文检查建议。
 * 副作用：无；既有中文业务错误保留，底层纯英文错误使用统一中文提示。
 */
xuyan::domain::Error storageError(const std::exception& exception);

} // namespace xuyan::storage::detail
