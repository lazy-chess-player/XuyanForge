#include "backup_filesystem.h"

#include <fstream>
#include <iostream>
#include <random>

namespace {
/*
 * 功能：运行暂存守卫的碰撞、失败清理及发布回归，所有资料仅在成功独占的临时目录内生成。
 * 参数：无。返回：无。失败：断言或文件错误抛异常。
 * 副作用：创建测试自有目录、空文件和暂存树；异常路径也仅清理该精确自有根。
 * 线程与生命周期：同步执行；守卫先析构，再释放根，不接触用户工作资料。
 */
void testStagingOwnership() {
    std::filesystem::path root;
    std::mt19937_64 random(std::random_device{}());
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto candidate = std::filesystem::temp_directory_path() / ("xuyan-backup-owner-test-" + std::to_string(random()));
        if (std::filesystem::create_directory(candidate)) { root = std::filesystem::canonical(candidate); break; }
    }
    if (root.empty()) throw std::runtime_error("测试未取得独占根目录");
    try {
        const auto occupied = root / "occupied.partial";
        std::filesystem::create_directory(occupied);
        std::ofstream(occupied / "keep.marker") << "keep";
        bool rejected = false;
        try { xuyan::application::detail::OwnedBackupStaging collision(occupied); }
        catch (const std::exception&) { rejected = true; }
        if (!rejected || !std::filesystem::exists(occupied / "keep.marker"))
            throw std::runtime_error("暂存名称碰撞必须拒绝并保留原目录");
        const auto failed = root / "failed.partial";
        {
            xuyan::application::detail::OwnedBackupStaging staging(failed);
            std::ofstream(staging.path() / "owned.marker") << "owned";
        }
        if (std::filesystem::exists(failed)) throw std::runtime_error("失败退出必须清理本调用自有暂存");
        const auto published = root / "published";
        {
            xuyan::application::detail::OwnedBackupStaging staging(root / "success.partial");
            std::ofstream(staging.path() / "owned.marker") << "owned";
            staging.publish(published);
        }
        if (!std::filesystem::exists(published / "owned.marker"))
            throw std::runtime_error("发布成功后析构不能清理正式目录");
        {
            xuyan::application::detail::OwnedBackupStaging staging(root / "rejected.partial");
            rejected = false;
            try { staging.publish(published); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error("发布不能覆盖已有目录");
        }
        if (!std::filesystem::exists(published / "owned.marker") || std::filesystem::exists(root / "rejected.partial"))
            throw std::runtime_error("发布失败应保留目标并只清理本调用暂存");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        throw;
    }
    std::filesystem::remove_all(root);
}
} // namespace

/*
 * 功能：运行独占暂存回归并输出中文结论。
 * 参数：无。返回：成功0，捕获异常失败1。
 * 失败：标准异常转失败码。副作用：测试自有临时文件及终端输出，无网络。
 * 线程与生命周期：同步执行，退出前清理成功取得的临时根。
 */
int main() {
    try { testStagingOwnership(); std::cout << "备份暂存所有权回归通过\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "备份暂存回归失败：" << error.what() << '\n'; return 1; }
}
