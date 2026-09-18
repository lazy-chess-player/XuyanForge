# 构建与运行

## Windows（已验证工具链）

在 Qt 命令行环境中，确保 Qt 的 MinGW `bin`、CMake 和 Ninja 位于 `PATH`，并把 `CMAKE_PREFIX_PATH` 指向 Qt kit。示例：

```powershell
$env:PATH = "C:\QT\Tools\mingw1310_64\bin;C:\QT\Tools\Ninja;C:\QT\Tools\CMake_64\bin;$env:PATH"
$env:CMAKE_PREFIX_PATH = "C:\QT\6.11.1\mingw_64"
cmake --preset windows-dev
cmake --build --preset windows-dev
ctest --preset windows-dev
./build/windows-dev/xuyanforge_app.exe
```

纯核心构建不需要 Qt GUI/Quick：

```powershell
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

应用数据存放在 Qt 返回的 `AppLocalDataLocation` 中，不写入源码仓库。当前演示数据库名为 `grey-harbor.sqlite`。

## Windows 部署目录

先构建 release preset，再在相同 Qt 命令行环境执行：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
./tools/package-windows.ps1
```

脚本只接受项目目录内的构建/输出路径，并用 `windeployqt` 收集 Qt/QML、平台、运行库与 TLS 组件。当前已在本机从部署目录启动通过；仍需在无 Qt SDK 的干净 Windows 环境完成 W0-12 最终验收。

生成便携 ZIP：

```powershell
./tools/package-windows.ps1 -CreatePortableZip
```

安装 NSIS 后可生成未签名安装器：

```powershell
./tools/package-windows.ps1 -CreatePortableZip -CreateInstaller
```

卸载器只删除安装目录和开始菜单入口，默认保留 `AppLocalDataLocation` 下的工作区。当前机器未安装 NSIS，安装器入口已配置但尚未在干净 Windows 中实测；未配置代码签名证书，因此不得把产物描述为已签名。
