# 构建与运行

## Windows（已验证工具链）

日常查看界面：双击仓库根目录的 `一键编译并预览.cmd`。它会配置、增量编译并启动桌面版；失败时窗口会停留显示错误。首次运行需有本机 Qt 6 MinGW、CMake 和 Ninja。默认识别 `C:\QT\6.11.1\mingw_64`，其他 Qt 安装位置可先设置 `CMAKE_PREFIX_PATH`。如只需编译或想顺便运行测试：

```powershell
./tools/build-and-preview.ps1 -NoLaunch
./tools/build-and-preview.ps1 -NoLaunch -RunTests
```

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

应用数据存放在 Qt 返回的 `AppLocalDataLocation` 中，不写入源码仓库。首次启动使用空白的 `workspace.sqlite`；不会自动创建世界、来源或人物资料。界面左侧选择项目，右侧新建世界并导入小说，可在侧栏切换深色和浅色主题。

本地长篇回归只读取环境变量指向的外部 TXT 文件，不把原文放进源码仓库或发行包：

```powershell
$env:XUYANFORGE_NOVEL_FIXTURE = 'C:\path\to\your-local-novel.txt'
./build/windows-dev/core_tests.exe
Remove-Item Env:XUYANFORGE_NOVEL_FIXTURE
```

## DeepSeek 连接与真实自检

“模型连接”页提供 DeepSeek 预设，当前官方配置为 `https://api.deepseek.com` 与 `deepseek-flash`。API Key 只写入当前 Windows 用户的 Credential Manager；SQLite 仅保存 `credential_ref`。保存后可分别执行 `/models` 端点探测和 Responses API JSON Schema 结构化生成自检。

开发环境也提供控制台工具。`configure-deepseek` 只从标准输入读取一行凭据，不要把 Key 放在命令行参数、脚本文件或 Git 配置中：

```powershell
./build/windows-dev/xuyanforge_provider_cli.exe configure-deepseek ./build/provider-test.sqlite
./build/windows-dev/xuyanforge_provider_cli.exe test ./build/provider-test.sqlite provider-deepseek
```

2026-09-18 的真实自检结果为 `deepseek-flash` 完成、JSON 有效、输入/输出 122/124 tokens。该测试证明认证、HTTPS 传输、Responses 解析和结构化输出链路可用，不等同于两家真实模型参与同一推演场景。

在“解析任务”选择已配置且允许远程发送的连接后，创建任务仍不会发起请求。只有点击任务卡片的“真实模型抽样 1 步”，才把下一块小说文本发送给所选提供商，占用一次请求预算；返回的候选必须含原文逐字引文，并在本地复核码点范围后进入人工校对。默认“离线（不发送小说）”，可使用离线 Mock。真实抽取流程已用合成文本和伪传输测试；本地长篇不在自动测试中发往远程服务，费用估算仍未知。

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
