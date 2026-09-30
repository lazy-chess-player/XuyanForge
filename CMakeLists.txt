cmake_minimum_required(VERSION 3.24)

# 数字版本、中文展示名称和发行渠道只从同一配置读取，避免构建与安装器分叉。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.json" XUYANFORGE_VERSION_JSON)
string(JSON XUYANFORGE_VERSION GET "${XUYANFORGE_VERSION_JSON}" version)
string(JSON XUYANFORGE_CHANNEL GET "${XUYANFORGE_VERSION_JSON}" channel)
string(JSON XUYANFORGE_VERSION_LABEL GET "${XUYANFORGE_VERSION_JSON}" displayName)
if(NOT XUYANFORGE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$" OR
   NOT XUYANFORGE_CHANNEL MATCHES "^[a-z][a-z0-9]*$")
    message(FATAL_ERROR "版本配置格式无效")
endif()
set(XUYANFORGE_ARTIFACT_VERSION "${XUYANFORGE_VERSION}-${XUYANFORGE_CHANNEL}")
project(XuyanForge VERSION ${XUYANFORGE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# 模块移入子目录后仍保留一键预览、测试脚本与 CI 已使用的可执行文件位置。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
configure_file(version.json "${CMAKE_BINARY_DIR}/release-version.json" COPYONLY)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(XUYANFORGE_BUILD_UI "构建 Qt Quick 桌面程序" ON)
option(XUYANFORGE_BUILD_TESTS "构建自动化测试" ON)

# Qt MinGW 工具链的 SQLite 安装在 opt；仅对本次配置加入搜索路径。
get_filename_component(XUYANFORGE_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(XUYANFORGE_TOOLCHAIN_ROOT "${XUYANFORGE_COMPILER_BIN}" DIRECTORY)
if(EXISTS "${XUYANFORGE_TOOLCHAIN_ROOT}/opt/include/sqlite3.h")
    list(PREPEND CMAKE_PREFIX_PATH "${XUYANFORGE_TOOLCHAIN_ROOT}/opt")
endif()
find_package(SQLite3 REQUIRED)
add_subdirectory(libs)

if(XUYANFORGE_BUILD_UI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.8)
    qt_policy(SET QTP0004 NEW)
    if(XUYANFORGE_BUILD_TESTS)
        find_package(Qt6 6.8 REQUIRED COMPONENTS QuickTest)
    endif()
    add_subdirectory(ui)
endif()

if(XUYANFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
