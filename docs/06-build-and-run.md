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

界面和测试同时启用时，CTest运行`core_tests`、`stress_tests`、`storage_support_tests`、`domain_boundary_tests`、`application_catalog_tests`、`tst_qmltests`和`tst_world_views_vm`。找到Python时另运行`contract_tests`、`development_standards_tests`及`development_standards`，共10项；`check-contracts`仍是独立构建目标。Qt Quick采用离屏平台和内存替身，视图模型及目录用例使用独占临时工作区，不读取用户资料或发送模型请求。可按单个QML用例输出JUnit：

```powershell
./build/windows-dev/tst_qmltests.exe -o ./build/qmltests.xml,junitxml
```

纯核心预设不生成 Qt Quick 测试目标；正式发行构建关闭 `XUYANFORGE_BUILD_TESTS` 后也不需要 Qt QuickTest。契约可单独执行 `python tools/validate_contracts.py`（WSL 用 `python3`），脚本仅使用标准库，不需要额外 jsonschema 包。它只覆盖本仓库使用的部分关键字与选定夹具，不等于完整 JSON Schema 运行时校验器。Qt Quick 组件测试不能替代完整 UI 端到端、中文输入法或真实系统对话框验收。

构建边界：根`CMakeLists.txt`配置版本、工具链与开关；`libs/CMakeLists.txt`管理纯C++库，`ui/CMakeLists.txt`管理Qt传输、桌面和工具，`tests/CMakeLists.txt`只在启用测试时加入。`version.json`是版本唯一入口，数字版本和内部渠道用于构建身份，`displayName`用于中文展示；构建生成`release-version.json`，打包发现与源码不一致时拒绝继续。

规范静态检查可单独执行`python tools/check_development_standards.py`，其回归为`python -m unittest discover -s tools -p test_development_standards.py -v`。扫描不代替逐函数契约审查、中文交互或空首启/包隔离验收。

应用数据存放在 Qt 返回的 `AppLocalDataLocation` 中，不写入源码仓库。首次启动使用空白的 `workspace.sqlite`；不会自动创建世界、来源或人物资料。界面左侧选择项目，右侧新建世界并导入小说，可在侧栏切换深色和浅色主题。

在“包与备份”导出世界条目包前，先在左侧明确选择世界。导出绑定开始时的世界，未选择/世界失效会拒绝，不默认取首世界。world-v1最多100000条、单项载荷32MiB，超限不截断且保留既有目标；包只含当前条目，不含小说、证据、历史版本或完整图谱。需要保存完整数据库和资产时另用“创建备份”，备份与条目包不是同一功能；目录/空世界和完整语义导入往返仍待开发验收。

导入后在“解析任务”创建任务：模型连接不选即为离线。点击“开始解析”处理剩余切片，已有进度的任务显示“继续解析”；绑定模型的任务会先确认剩余片数和模型标识，只有确认后才发送。运行中可暂停或取消；它们在当前片段结束后生效，不撤销已发请求。进度显示已提交片段及完整处理的章节，候选须进入人工校对，不会自动成为世界事实。失败/未知片段不自动重发；关闭程序会请求停止并等待当前片段结算，重启后从持久化检查点继续。页面默认原文，主干模式选择仍待接入。

本地长篇回归只读取环境变量指向的外部 TXT 文件，不把原文放进源码仓库或发行包：

```powershell
$env:XUYANFORGE_NOVEL_FIXTURE = 'C:\path\to\your-local-novel.txt'
./build/windows-dev/core_tests.exe
Remove-Item Env:XUYANFORGE_NOVEL_FIXTURE
```

需要验证整本小说的离线规则处理时，可同时设置 `XUYANFORGE_NOVEL_FULL_OFFLINE=1` 再运行同一测试；这会处理全部切片，耗时和本地测试数据库占用明显增加，但仍不会访问模型。此开关不验证真实模型的提取质量。

千万字结构压力测试使用运行时生成的合成文本，不需要外部小说文件。默认测试不会生成大文件；显式运行后会清理临时目录。它检验导入、章节校正、章内切片、末章证据及两片之间的暂停/重建继续，不代表真实小说的全量处理、模型提取质量或总耗时：

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

新建远程任务固定`candidate-v3/extract-v3`类型化输出版本，字段说明见[提取契约](../prompts/extraction-v3.md)。旧远程协议任务保持可读，但在领取步骤和发送前拒绝，不自动重试或升级；须重新创建并确认发送范围。既有合法v1/v2候选可继续校对，离线任务仍使用v1。v3增加有界候选容量、单项隔离统计和唯一原文映射；真实片段证据见[开发状态](05-development-status.md)，但尚未建立人工金标准，不能宣称语义准确率或全书召回率已达标。

### 远程批次与输入模式服务边界

`ExtractionJobService::create`新增可选的`ExtractionInputConfig`参数，默认仍为原文。后端调用者可显式指定`{"backbone", "conservative"/"balanced"/"compact", "backbone-v1"}`，当前界面不提供新的选择入口。模式、密度及算法在建任务时持久化并进入缓存摘要；更换参数须新建任务，不会修改旧检查点或自动发送。版本26工作区只回填原文快照，升级27后当前任务配置缺失会拒绝发送。发送前和候选提交前都校验完整片段摘要；引文只在连续保留原文中唯一定位，省略标记不是证据。小说资产不因压缩改动；token估算仍保留原文粗略上界，不能当成实际账单或压缩质量报告。

`RemoteExtractionProcessor::processBatch`目前只提供C++服务入口，不接任务创建、页面刷新、启动恢复或一键编译。调用者必须显式填写`RemoteBatchOptions.maximum_steps`（1—100000），同时受任务创建时持久化的请求次数上限限制；本次步骤上限不是付费授权，也不是增加预算的接口。默认仍离线，界面仍只有已确认的单步抽样。

批次同步串行运行，进度通知只含计数；回调可在提交后的检查点返回暂停或取消，停止令牌只阻止下一请求。暂停后显式再次调用从下一片继续；失败、未知或中断领取先人工核对，不能跳过或自动重试。取消不强制中断已发出的请求，有效回报保存为待审候选后停止后续发送。同进程同任务的单步和批次互斥；这不是跨进程/共享配额协调器，后台线程生命周期、成本累计和界面批量确认仍待实现。

默认CTest只运行自有动态文本及伪传输回归，不调用真实接口；本轮没有使用批次入口发送用户的小说。用户此前小样本授权已执行2次，以下手动命令仍需按剩余额度核对，不能因服务支持多片而追加发送。

远程调度回归另有可选开关`XUYANFORGE_REMOTE_BATCH_STRESS=1`：在现有核心构建环境中运行`build/core-dev/core_tests.exe`，动态生成1000章、11,727,786源码点的自有文本并全量执行伪传输。当前大样本显式选择主干保守模式，强制实际发送正文缩小、历史输出整表读取不超过1次和每个候选的原文范围/哈希一致；默认40章仍用原文模式。合成样本大量重复，压缩比不能推广到真实小说；没有真实凭据、端点请求或付费调用。素材只在测试独占临时目录并于运行后清理，不链接或打包到正式程序。运行前保存原环境值、用`try/finally`恢复，不能把压力开关遗留到日常预览或CI。尚未测峰值内存、真实模型质量或费用。

### 明确授权的本机小说抽样

以下命令会付费发送小说片段，**不是CI或日常构建的一部分**。须有明确的开发验证授权、已有官方`deepseek-flash`连接和系统凭据；不通过命令行传密钥。工具只读连接元数据，在全新输出目录导入用户指定的外部小说，默认选择第2个章节区间和中间区间各最多2000码点，每个样本只执行1步，每次输出上限2400 token；未知/失败停止，不自动重试。不能使用已有输出目录重复发送。抽样工具显式使用非思考模式；这不暗改正式程序中已创建任务的配置，也不使构建、CI或页面刷新获得发送权限。

```powershell
./build/windows-dev/xuyanforge_provider_cli.exe verify-novel `
  ./build/my-new-sample-output ./build/my-provider-workspace.sqlite C:\path\to\your-local-novel.txt
```

`verify-novel-second`只发送中段一次；`verify-novel-chapter`接受从1开始的明确章节序号并只发送该章一次，例如：

```powershell
./build/windows-dev/xuyanforge_provider_cli.exe verify-novel-chapter `
  ./build/my-new-sample-output ./build/my-provider-workspace.sqlite C:\path\to\your-local-novel.txt 500
```

这些命令只供人工质量验证，不是无限自动调用接口；每次都要求全新本机输出目录，失败不会自行重试。

原文、抽样文件、候选和详细输出仅存放本机验证目录，不能提交或打包。控制台只打印区间序号、码点、候选/证据计数及已知用量；证据一致不等于语义正确或召回合格。工具限制官方端点、模型、请求字节和次数；按[官方价格](https://api-docs.deepseek.com/zh-cn/quick_start/pricing/)记录高峰全未命中缓存估算，不冒称实际账单，超时请求用量可能未知。用户授权本次小样本不意味着授权未来整书发送。

## 备份安全定向回归

在已配置的核心工具链环境中执行：

```powershell
cmake --build --preset core-dev --target backup_safety_tests backup_staging_tests
ctest --preset core-dev -R '^backup_.*_tests$' -V
```

`backup_safety_tests`覆盖45项普通备份/恢复用例，`backup_staging_tests`验证暂存碰撞保留、失败清理与发布所有权；两者不允许跳过。`backup_link_tests`复用前者可执行文件的`--links-only`模式，另跑12项目录链链接用例。只有明确链接权限/设施不足返回77时，该链接分组允许CTest标记跳过，输出实际未执行数；其他失败仍阻断门禁。2026-09-30本机Windows原生创建链接返回权限错误1314，链接分组未执行，不能计为通过；不自动修改系统权限或开发者模式。

备份目标必须为尚不存在、位于整个源工作区之外的新目录；恢复目标与备份根不得互相包含。源或目标路径链中的链接/Windows重解析点拒绝；清单校验实际大小、SHA及SQLite快速检查/核心表。此预检不保证消除外部进程并发替换，不提供断电持久性。数据库与资产仍需调用方保持稳定，文件摘要仍有界整文件读入，不是千万字资源验收。只读SQLite连接可能生成日志模式协调文件，不自动删除源日志；系统凭据不主动纳入，但用户自行保存在数据库/资产内的敏感内容不被过滤。

## Windows 部署目录

先构建 release preset，再在相同 Qt 命令行环境执行：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
./tools/package-windows.ps1
```

脚本只接受项目 `build` 下相互分离的构建/输出路径，并用 `windeployqt` 收集 Qt/QML、平台、运行库与 TLS 组件。数字版本、发行渠道及中文展示名称统一来自根目录 `version.json`，打包前核对构建目录的冻结副本。2026-09-30 已在本机新部署目录验证移除 Qt SDK 路径后启动并截图；这不等同于独立干净 Windows 的安装、升级或卸载验收，后者仍由 XF-29 跟踪。不能将复用旧输出目录生成的包直接视为无素材残留的正式发行包。

生成便携 ZIP：

```powershell
./tools/package-windows.ps1 -CreatePortableZip
```

安装 NSIS 后可生成未签名安装器：

```powershell
./tools/package-windows.ps1 -CreatePortableZip -CreateInstaller
```

卸载器只删除安装目录和开始菜单入口，默认保留 `AppLocalDataLocation` 下的工作区。当前机器未安装 NSIS，安装器入口已配置但尚未在干净 Windows 中实测；未配置代码签名证书，因此不得把产物描述为已签名。
