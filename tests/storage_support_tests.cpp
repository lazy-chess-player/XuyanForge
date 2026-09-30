#include "sqlite_support.h"
#include <iostream>
#include <stdexcept>

namespace {
/* 职责：测试独占的内存数据库，不读取文件、用户工作区或凭据；析构关闭连接。 */
class MemoryDatabase final {
public:
    /* 功能：打开独占内存数据库。参数：无。返回：初始化对象；失败抛异常。 */
    MemoryDatabase() {
        if (sqlite3_open(":memory:", &handle_) != SQLITE_OK) {
            sqlite3_close(handle_);
            throw std::runtime_error("无法创建测试内存数据库");
        }
    }
    /* 功能：释放内存数据库。参数：无。返回：无；测试语句应先析构。 */
    ~MemoryDatabase() { sqlite3_close(handle_); }
    /* 功能：禁止复制独占连接。参数：另一测试连接。返回：无，编译期禁止。 */
    MemoryDatabase(const MemoryDatabase&) = delete;
    /* 功能：禁止复制赋值。参数：另一测试连接。返回：无，编译期禁止。 */
    MemoryDatabase& operator=(const MemoryDatabase&) = delete;
    /* 功能：借用测试连接。参数：无。返回：非拥有指针，有效期限于本对象。 */
    sqlite3* get() const { return handle_; }
private:
    // 独占的内存连接；初始空，构造打开，析构关闭，不用于正式程序。
    sqlite3* handle_{nullptr};
};

/* 功能：验证文本绑定/读取不截断嵌入零字节，以及结果游标能正常结束。
 * 参数：无。返回：无；任一行为不符合时抛异常。副作用：仅操作独占内存数据库。
 */
void testEmbeddedNullText() {
    MemoryDatabase database;
    xuyan::storage::detail::Statement query(database.get(), "SELECT ?");
    const std::string input{"a\0b", 3};
    xuyan::storage::detail::bindText(query.get(), 1, input);
    if (!xuyan::storage::detail::stepRow(query.get())
        || xuyan::storage::detail::columnText(query.get(), 0) != input
        || xuyan::storage::detail::stepRow(query.get()))
        throw std::runtime_error("数据库文本范围或查询结束语义不正确");
}
/* 功能：通过SQLite整数溢出复现步进错误，确保错误不能被解释为空结果。
 * 参数：无。返回：无；没有抛出预期存储异常时失败。副作用：只执行内存只读语句。
 */
void testQueryFailureIsNotEmpty() {
    MemoryDatabase database;
    xuyan::storage::detail::Statement query(database.get(), "SELECT abs(-9223372036854775808)");
    bool rejected = false;
    try { (void)xuyan::storage::detail::stepRow(query.get()); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("查询错误被错误地当成空结果");
}
/* 功能：验证未提交写事务自动回滚，连接随后仍可读取。
 * 参数：无。返回：无；有未提交记录残留时失败。副作用：仅创建测试内存表，不写工作区文件。
 */
void testUncommittedTransactionRollsBack() {
    MemoryDatabase database;
    if (sqlite3_exec(database.get(), "CREATE TABLE value_log(value INTEGER)", nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error("创建测试表失败");
    {
        xuyan::storage::detail::Transaction transaction(database.get());
        if (sqlite3_exec(database.get(), "INSERT INTO value_log VALUES(1)", nullptr, nullptr, nullptr) != SQLITE_OK)
            throw std::runtime_error("写入测试表失败");
    }
    xuyan::storage::detail::Statement query(database.get(), "SELECT count(*) FROM value_log");
    if (!xuyan::storage::detail::stepRow(query.get()) || sqlite3_column_int(query.get(), 0) != 0)
        throw std::runtime_error("未提交事务没有回滚");
}
}

/* 功能：运行私有SQLite基础设施回归。参数：无。返回：全部通过为0，异常为1。
 * 副作用：只在独占内存库中执行，控制台输出中文错误，不读取用户资料。
 */
int main() {
    try {
        testEmbeddedNullText();
        testQueryFailureIsNotEmpty();
        testUncommittedTransactionRollsBack();
        std::cout << "存储基础设施回归通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
