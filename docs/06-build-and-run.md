# 构建与运行

执行计划与任务依赖分别见[主计划](04-cpp-implementation-plan.md)和[看板](07-development-task-board.md)。本文列实际已有入口；自动化配置已加入，远端结果以[状态记录](05-development-status.md)为准，不把配置存在视为构建通过。

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

同一构建目录不能在 Windows 与 WSL 之间混用。WSL 的无界面检查使用单独目录；以下命令需本机已具备 C++20 编译器、CMake、Ninja、SQLite 开发库，实际成功与否记录在任务报告中，不能据此宣称 Linux 产品已验收：

```bash
cmake -S . -B build/linux-core-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DXUYANFORGE_BUILD_UI=OFF
cmake --build build/linux-core-dev
ctest --test-dir build/linux-core-dev --output-on-failure
```

界面和测试同时启用时，CTest 运行 `core_tests`、`stress_tests`、`tst_qmltests` 和 `tst_world_views_vm`，找到 Python 解释器时另运行 `contract_tests`；`check-contracts` 仍是独立构建目标。`tst_qmltests` 使用离屏平台和内存工作区替身，视图模型测试使用独立临时工作区；二者都不读取用户工作区、不发模型请求。可按单个 QML 用例输出 JUnit：

```powershell
./build/windows-dev/tst_qmltests.exe -o ./build/qmltests.xml,junitxml
```

纯核心预设不生成 Qt Quick 测试目标；正式发行构建关闭 `XUYANFORGE_BUILD_TESTS` 后也不需要 Qt QuickTest。契约可单独执行 `python tools/validate_contracts.py`（WSL 用 `python3`），脚本仅使用标准库，不需要额外 jsonschema 包。它只覆盖本仓库使用的部分关键字与选定夹具，不等于完整 JSON Schema 运行时校验器。Qt Quick 组件测试不能替代完整 UI 端到端、中文输入法或真实系统对话框验收。

应用数据存放在 Qt 返回的 `AppLocalDataLocation` 中，不写入源码仓库。首次启动使用空白的 `workspace.sqlite`；不会自动创建世界、来源或人物资料。界面左侧选择项目，右侧新建世界并导入小说，可在侧栏切换深色和浅色主题。

本地长篇回归只读取环境变量指向的外部 TXT 文件，不把原文放进源码仓库或发行包：

```powershell
$env:XUYANFORGE_NOVEL_FIXTURE = 'C:\path\to\your-local-novel.txt'
./build/windows-dev/core_tests.exe
Remove-Item Env:XUYANFORGE_NOVEL_FIXTURE
```

需要验证整本小说的离线规则处理时，可同时设置 `XUYANFORGE_NOVEL_FULL_OFFLINE=1` 再运行同一测试；这会处理全部切片，耗时和本地测试数据库占用明显增加，但仍不会访问模型。此开关不验证真实模型的提取质量。

千万字结构压力测试使用运行时生成的合成文本，不需要外部小说文件。默认测试不会生成大文件；显式运行后会清理临时目录。它检验导入、章节校正、章内切片、末章证据和一次离线抽样，不代表真实小说的全书模型提取质量或总耗时：

```powershell
$env:XUYANFORGE_STRESS_10M = '1'
./tools/build-and-preview.ps1 -NoLaunch -RunTests
Remove-Item Env:XUYANFORGE_STRESS_10M
```

## 无密钥自动化质量门禁

工作流：[开发质量门禁](../.github/workflows/quality.yml)。推送及拉取请求自动运行：

- Linux核心、压力与契约回归，不构建界面；
- Windows界面、核心、Qt Quick和视图模型回归；
- Windows单独关闭测试的正式应用编译，避免把测试引擎作为生产依赖；
- 汇总“阶段质量门禁”：任一前置作业失败、取消或跳过时汇总失败。

这不是软件发行流水线，不会发布安装器，也未替管理员启用仓库分支保护。阶段上传后必须检查汇总状态；后续发行任务依赖门禁通过。若要在仓库层禁止绕过检查合并，需要管理员将汇总状态设为必需检查，本轮未修改该设置。

Windows固定Qt 6.8.3、Visual Studio 2022编译环境、Python 3.12.10及vcpkg `2026.07.29`提交，SQLite清单明确启用全文检索；MSVC指定`/utf-8`。第三方动作固定完整提交号。Linux基础镜像与系统包可能更新，报告记录实际CMake、编译器、SQLite和Python版本；固定配置不等于逐字节可重复发行。

工作流无需模型密钥，显式清空本地小说开关；默认只生成有限合成测试资料，不读取私人小说、不调用模型。可手动运行并勾选“额外运行千万字合成结构回归”；该选项增加Linux运行时合成结构测试，仍不发送远程请求，也不代表完整千万字界面验收。

证据产物仅包含白名单构建输入摘要、工具版本、CTest JUnit与测试日志，保留14天；不上传CMake完整缓存、环境变量、工作区数据库、可执行文件或小说素材。构建摘要不含原始差异正文和未跟踪文件名。隐私边界回归及本机构建记录入口：

```powershell
$env:PYTHONUTF8 = '1'
python -m unittest discover -s tools -p test_record_build_inputs.py -v
python tools/record_build_inputs.py --build-directory build/windows-dev --label '本机开发构建'
```

依赖清单位于`tools/ci-dependencies`，不放在仓库根目录，因此不会改变现有一键编译对本机Qt MinGW工具链的使用。自动化配置依据：[Qt安装动作](https://github.com/jurplel/install-qt-action)、[vcpkg清单模式](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)、[GitHub工作流产物](https://docs.github.com/en/actions/concepts/workflows-and-actions/workflow-artifacts)。

## DeepSeek 连接与真实自检

“模型连接”页可选择深度求索或兼容协议，并手动填写接口地址、模型名称与密钥；程序不预填测试连接或密钥。密钥只写入当前 Windows 用户的 Credential Manager；SQLite 仅保存 `credential_ref`。保存后可执行端点探测。控制台工具仍可运行结构化生成自检。

开发环境也提供控制台工具。`configure-deepseek` 只从标准输入读取一行凭据，不要把 Key 放在命令行参数、脚本文件或 Git 配置中：

```powershell
./build/windows-dev/xuyanforge_provider_cli.exe configure-deepseek ./build/provider-test.sqlite
./build/windows-dev/xuyanforge_provider_cli.exe test ./build/provider-test.sqlite provider-deepseek
```

2026-09-18 的真实自检结果为 `deepseek-flash` 完成、JSON 有效、输入/输出 122/124 tokens。该测试证明认证、HTTPS 传输、Responses 解析和结构化输出链路可用，不等同于两家真实模型参与同一推演场景。

在“解析任务”选择已配置且允许远程发送的连接后，创建任务仍不会发起请求。只有点击任务卡片的“模型抽样 1 步”，才把下一块小说文本发送给所选提供商，占用一次请求预算；返回的候选必须含原文逐字引文，并在本地复核码点范围后进入人工校对。默认离线规则提取，不发送小说。任务创建时绑定连接配置指纹；若连接后来被修改或停用，原任务会拒绝继续发送，须确认新配置并重新创建任务。旧工作区中没有配置快照的远程任务也不会自动发送。真实抽取流程已用合成文本和伪传输测试；本地长篇不在自动测试中发往远程服务，费用估算仍未知。

## Windows 部署目录

先构建 release preset，再在相同 Qt 命令行环境执行：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
./tools/package-windows.ps1
```

脚本只接受项目目录内的构建/输出路径，并用 `windeployqt` 收集 Qt/QML、平台、运行库与 TLS 组件。XF-02 已验证全新暂存目录与便携包资源隔离；当前剩余工作由 XF-29 跟踪，包括版本统一及无 Qt SDK 的 Windows 安装/升级验收。不能将复用旧输出目录生成的包直接视为已无素材残留的正式发行包。

生成便携 ZIP：

```powershell
./tools/package-windows.ps1 -CreatePortableZip
```

安装 NSIS 后可生成未签名安装器：

```powershell
./tools/package-windows.ps1 -CreatePortableZip -CreateInstaller
```

卸载器只删除安装目录和开始菜单入口，默认保留 `AppLocalDataLocation` 下的工作区。当前机器未安装 NSIS，安装器入口已配置但尚未在干净 Windows 中实测；未配置代码签名证书，因此不得把产物描述为已签名。
