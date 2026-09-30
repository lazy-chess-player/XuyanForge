#pragma once

#include <filesystem>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace xuyan::application::detail {

/*
 * 功能：逐分量拒绝路径中的符号链接及Windows重解析点，不跟随链接校验其目标。
 * 参数：path为借用至返回的非空绝对路径；允许尚不存在的后缀，不允许父级回退分量或空字符。
 * 返回：无；通过仅说明检查时已存在的每一分量不是链接。
 * 失败：路径格式、权限、元数据读取或链接检查失败抛异常。
 * 副作用：只读文件元数据，不创建或删除资料。
 * 线程与生命周期：同步执行；不锁定目录，调用方须避免外部进程在检查后替换目录。
 */
inline void rejectLinkedPath(const std::filesystem::path& path) {
    if (path.empty() || !path.is_absolute() || path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
        throw std::runtime_error("备份路径必须是无空字符的绝对路径");
    std::filesystem::path current = path.root_path();
    for (const auto& component : path.relative_path()) {
        if (component == "..") throw std::runtime_error("备份路径不能包含父级回退分量");
        if (component == "." || component.empty()) continue;
        current /= component;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw std::runtime_error("无法核查备份路径分量");
        if (std::filesystem::is_symlink(status)) throw std::runtime_error("备份路径不能包含符号链接");
#ifdef _WIN32
        // Windows目录联接不是所有标准库版本都识别的符号链接，额外拒绝全部重解析点。
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            throw std::runtime_error("备份路径不能包含重解析点");
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto native_error = GetLastError();
            if (native_error != ERROR_FILE_NOT_FOUND && native_error != ERROR_PATH_NOT_FOUND)
                throw std::runtime_error("无法读取备份路径属性");
        }
#endif
    }
}

/*
 * 职责：仅拥有一次create_directory成功取得的单个暂存目录；不接管名称碰撞的已有目录。
 * 生命周期：同步操作栈内使用，禁止复制/移动清理责任；发布后解除拥有，失败尽力清理自身目录。
 * 安全边界：清理前重新核对父目录和路径链，但不抵御核对之后的恶意并发目录替换。
 */
class OwnedBackupStaging final {
public:
    /*
     * 功能：在已经存在且无链接的父目录中独占创建一个指定暂存目录。
     * 参数：candidate为按值取得的绝对候选路径，须位于可信父目录，名称不能已存在。
     * 返回：初始化独占守卫。失败：碰撞、链接或创建错误抛异常；碰撞目录不删除也不修改。
     * 副作用：仅创建candidate这个目录，不递归创建父目录；创建后只有此对象有清理责任。
     * 线程与生命周期：同步构造，不接受其他调用已创建的目录。
     */
    explicit OwnedBackupStaging(std::filesystem::path candidate) : path_(std::move(candidate)) {
        rejectLinkedPath(path_);
        parent_ = std::filesystem::canonical(path_.parent_path());
        if (path_.parent_path() != parent_) throw std::runtime_error("暂存父目录必须先解析为实际路径");
        if (!std::filesystem::create_directory(path_)) throw std::runtime_error("备份暂存目录名称已被占用");
        owned_ = true;
    }
    /*
     * 功能：失败退出时尽力清理成功创建的暂存路径；已发布、路径含链接或父身份不符时不清理。
     * 参数：无。返回：无，释放清理责任。失败：捕获全部清理异常，不从析构传播。
     * 副作用：仅递归移除path_，不移除父目录或其他暂存目录；失败可能残留本调用的暂存资料。
     * 线程与生命周期：调用线程析构，要求调用方保持目录树稳定；不辨认同名普通目录的外部原位替换。
     */
    ~OwnedBackupStaging() {
        if (!owned_) return;
        try {
            rejectLinkedPath(path_);
            if (std::filesystem::canonical(path_.parent_path()) != parent_
                || std::filesystem::canonical(path_) != path_) return;
            // 先核查整个树，拒绝通过后来插入的链接清理；核查失败宁可保留暂存供人工处理。
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path_)) rejectLinkedPath(entry.path());
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        } catch (...) {}
    }
    /* 功能：禁止复制清理所有权。参数：另一守卫的只读引用。返回：无；编译期拒绝，无副作用。 */
    OwnedBackupStaging(const OwnedBackupStaging&) = delete;
    /* 功能：禁止覆盖清理所有权。参数：另一守卫的只读引用。返回：无；编译期拒绝，无副作用。 */
    OwnedBackupStaging& operator=(const OwnedBackupStaging&) = delete;
    /*
     * 功能：读取本次独占暂存目录。
     * 参数：无。返回：借用路径引用，有效至守卫销毁。失败：无。副作用：只读成员。
     */
    const std::filesystem::path& path() const noexcept { return path_; }
    /*
     * 功能：再次核查目标不存在及无链接，将拥有的暂存目录发布为正式目录。
     * 参数：destination为借用的新绝对目录路径，须和暂存目录同父目录且尚不存在。
     * 返回：无。失败：目标已存在、父目录不同、链接或重命名失败抛异常，清理责任保留。
     * 副作用：重命名后解除暂存清理责任；不做持久化屏障，不保证消除外部竞争进程。
     * 线程与生命周期：同步执行，调用方独占目标范围；成功之后此对象不再删除任何目录。
     */
    void publish(const std::filesystem::path& destination) {
        rejectLinkedPath(path_);
        rejectLinkedPath(destination);
        if (!owned_ || destination.parent_path() != parent_ || std::filesystem::exists(destination))
            throw std::runtime_error("备份发布目标已存在或父目录发生变化");
        std::filesystem::rename(path_, destination);
        owned_ = false;
    }
private:
    /* 本调用候选绝对路径，构造取得、以后只读；成功创建才可清理，单位无，随守卫释放。 */
    std::filesystem::path path_;
    /* 已解析的真实父目录，初始空，构造填写；发布及清理用于防止父目录变更，无文件句柄所有权。 */
    std::filesystem::path parent_;
    /* 清理责任标记，默认false，创建成功置true，发布成功置false；只由本守卫同步修改。 */
    bool owned_{false};
};

} // namespace xuyan::application::detail
