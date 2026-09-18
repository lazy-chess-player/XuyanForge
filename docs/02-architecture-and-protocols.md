# 叙演工坊：技术架构与数据协议

版本：0.2-draft · 日期：2026-09-18  
本文描述拟实现的架构与协议。示例用于明确语义，具体库版本和生成代码在M0锁定；不代表已实现或已测试。

## 1. 技术决策摘要

| 决策 | 暂定选择 | 理由与边界 |
|---|---|---|
| 原生客户端 | Qt 6 | Windows/macOS/Linux/Android，模块与平台桥接须实测 |
| 原生界面 | QML + Qt Quick Controls | 共享组件；桌面与触屏布局分别设计 |
| 领域核心 | C++20库 | 与Qt界面解耦，桌面、Android、服务端共享状态规则 |
| 本地数据 | SQLite C API的RAII封装 + 文件资产目录 | 事务、备份与FTS5能力显式控制；单写入协调 |
| 服务端 | C++无界面服务 | 复用领域与应用库；HTTP服务框架在M7前验证确定 |
| Web界面 | 独立浏览器前端，建议TypeScript | 共用协议与视觉规范，不要求复用QML界面 |
| 构建 | CMake + Ninja，Windows默认MSVC工具链 | 显式目标与依赖清单；具体版本在M0锁定 |
| 协议 | JSON Schema + 版本化命令/事件 | 类型和运行时验证使用同一权威定义 |
| 检索 | 实体/时间/权限过滤 + 中文词法检索，向量可选 | 先保证正确范围，再提升语义召回 |
| UI状态 | C++ ViewModel + QAbstractItemModel，Web使用独立查询适配 | 界面状态与世界事实分离 |
| 任务执行 | C++持久化队列、事件调度与明确状态机 | 流式网络异步处理；CPU任务使用有界工作池 |
| 图谱 | SQLite关系表 + Qt Quick绘制/浏览器视图 | 首版不引入独立图数据库 |

Qt提供所需原生平台支持，Qt Quick可结合QML界面与C++逻辑。具体系统版本、编译器和模块支持必须按选定Qt版本验证。[Qt支持平台](https://doc.qt.io/qt-6/supported-platforms.html)、[Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)

### 1.1 备选方案与回退

当前计划根据用户的C++方向，采用Qt Quick原生客户端。QML只承担界面和轻量交互，业务规则保持在C++层；首版不以Qt Widgets作为全部页面基础，以便桌面和触屏采用相同组件体系。这是本项目的工程选择，并非声称Qt Quick在所有任务上优于其他框架。

正式Web版以“独立浏览器前端＋C++服务端”为主线。Qt WebAssembly仅做有预算的复用实验，只有中文输入、长文本选择、无障碍、首屏加载和文件交互通过评估后才考虑扩大使用。Qt官方列出其浏览器沙箱及模块限制，不能据此承诺原生功能全部直编可用。[Qt WebAssembly](https://doc.qt.io/qt-6/wasm.html)

M0若发现Android凭据或系统文件接口不满足需要，替换平台适配器或增加小范围JNI桥接。临时远端模式可以先交付，但必须注明能力边界，不能替代原生版本验收。

### 1.2 语言与依赖边界

领域模块使用C++20标准库和受控值类型；不暴露QObject、QML对象、数据库连接或操作系统句柄。应用/引擎模块经接口调用存储、网络、时钟和调度器，Qt适配层负责事件循环与系统集成。命令与模型事件跨线程按值传递或使用明确的不可变共享对象。

桌面和Android使用Qt Core、Gui、Qml、Quick、Quick Controls及Network作为主要候选模块。地图、时间线和关系图先采用自有场景组件，不因功能名称相似就引入Qt Graphs或Qt Quick Timeline。实际Qt模块的授权方式需在依赖表记录；部分模块不提供LGPL选项，不能把“采用Qt”解释为任意模块都适用同一发行条件。[Qt授权说明](https://doc.qt.io/qt-6/licensing.html)

SQLite选择同一固定版本的C API封装作为唯一持久化路径。Qt SQL可用于原型比较，但正式实现不得让Qt SQL与另一套SQLite连接封装各自管理相同工作区的事务。JSON值类型、Schema校验器及中文分词库在M0做小样本验证后锁定，不把它们暴露到公共领域接口。

## 2. 分层架构

```mermaid
flowchart TB
  UI[Qt Quick 原生界面] --> VM[C++ ViewModel / 命令入口]
  VM --> APP[C++应用服务与持久化任务调度]
  WEB[独立浏览器界面] --> HTTP[HTTP/SSE 适配器]
  HTTP --> SERVER[认证后的服务端入口]
  SERVER --> APP
  APP --> DOMAIN[世界版本 / 人物实例 / 分支 / 规则领域核心]
  APP --> ENGINE[资料提取与剧情推演引擎]
  ENGINE --> CONTEXT[按时间与知识范围构建上下文]
  ENGINE --> MODELS[模型提供商适配器]
  APP --> REPO[存储仓储接口]
  APP --> PORTS[文件 / 凭据 / 网络 / 生命周期接口]
  REPO --> DB[(SQLite)]
  REPO --> FILES[原文与资产文件]
```

依赖方向：UI依赖用例接口与协议；应用服务依赖领域与接口；平台实现依赖接口；领域代码不依赖Qt GUI、浏览器或操作系统路径。Web和原生端提交相同语义的命令，但不要求采用相同的进程通信方式。

推演状态机在C++应用核心中运行，不放进QML绑定或页面生命周期回调。窗口重绘不触发模型重复请求。客户端退出时任务安全暂停；首版不默认安装系统常驻服务。

### 2.1 各运行环境

| 环境 | UI与核心 | 数据与模型调用 |
|---|---|---|
| Windows/macOS/Linux | Qt Quick + C++核心 | 本地SQLite、Qt Network传输适配、系统凭据 |
| Android | Qt Quick + C++核心 + 必要的JNI桥接 | 应用沙箱数据库、平台文件选择、前台短任务 |
| 正式Web | 独立浏览器UI + C++服务端 | 服务器存储和模型调用，浏览器不持有厂商长期密钥 |
| 可选Wasm实验 | QML界面/有限C++逻辑编译为Wasm | 默认远端服务；浏览器缓存不作为工作区权威存储 |

Web首个完整形态建议为单用户自托管或受控账号服务。静态演示页面可以只使用MockClient；不能把演示误称为完整本地Web版。Wasm实验不要求带上原生SQLite/凭据模块，也不向浏览器打包服务端Key。

### 2.2 推荐目录（实施时创建）

```text
XuyanForge/
  CMakeLists.txt              # 原生C++工程入口
  CMakePresets.json           # 可复现的构建配置，不含本机绝对路径
  cmake/                     # 工具链、依赖版本与安装规则
  apps/native/               # Qt应用入口，桌面与Android平台配置
  apps/server/               # 后续无界面C++ HTTP/SSE服务
  apps/web/                  # 后续独立浏览器前端
  libs/domain/               # 标准C++世界、人物、时间、分支与规则
  libs/application/          # 用例、权限、事务与任务边界
  libs/engine/               # 提取、上下文、推演调度
  libs/providers/            # 厂商协议适配，依赖传输接口
  libs/storage_sqlite/       # SQLite RAII封装、迁移、快照和检索
  libs/platform_qt/          # Qt网络、线程调度、文件和系统桥接
  ui/qml/                    # 原生界面与可复用QML组件
  ui/viewmodels/             # C++视图模型、列表模型、命令代理
  contracts/                 # JSON Schema、版本与生成类型
  tests/                     # 领域/存储/协议测试及Qt界面测试
  fixtures/                   # 自有虚构世界、恶意输入、回归样本
  docs/                       # 当前四份设计文档及后续ADR
  tools/                      # 构建、协议生成、验证脚本
```

不预先为每类Agent建独立服务。服务拆分需要性能或部署证据，首版保持模块化单体。

### 2.3 线程与资源模型

原生进程至少区分GUI线程、应用协调器、网络事件线程、数据库写入执行器和有界CPU工作池。这是职责划分，不要求每个职责都创建独立线程；M0可让协调器与网络共用一个有事件循环的后台线程。所有队列都要有长度限制和取消检查点。

GUI线程只处理界面状态与模型通知。网络对象在所属线程创建、使用和销毁，通过异步事件返回结果；不得在UI线程用同步等待包装HTTP。Qt网络对象有线程归属限制，基础适配必须遵守。[QNetworkAccessManager](https://doc.qt.io/qt-6/qnetworkaccessmanager.html)

SQLite写入连接由一个执行器持有，读连接按执行器独立持有；不跨线程共享活跃statement。一次领域提交使用短事务，事务期间禁止等待模型或网络。备份与迁移通过同一存储调度入口协调。Qt SQL如用于验证原型，也必须遵守其连接线程要求。[Qt线程与数据库](https://doc.qt.io/qt-6/threads-modules.html)

取消采用协作式信号；取消请求、网络结束和页面关闭可能同时发生，按request_id与attempt_id去重。停止接收界面事件不等于取消核心任务。任务由应用服务持有，页面离开时只解除订阅；显式暂停/退出流程另行持久化检查点。

### 2.4 C++资源与错误处理

使用RAII封装文件、数据库、statement及网络请求生命周期；标准C++对象以值语义与unique_ptr为主，确需共享才使用shared_ptr。QObject父子所有权和智能指针所有权不得同时负责删除同一个对象。异步回调使用受保护的生命周期引用，禁止捕获可能先析构的裸this。

预期错误使用统一Result/Error值类型；接口和Qt事件回调边界不得让异常逃逸。C++20项目不依赖C++23才提供的标准接口；若选第三方Result实现，纳入依赖清单。取消和拒绝不是程序崩溃，错误码延续第12.4节。

发布前对可用工具链启用静态检查、地址/未定义行为检测，并对并发路径做竞态检查；不把“通过编译”当作内存和线程正确性的证据。

## 3. 通用协议约定

- 持久实体ID使用UUID字符串；前缀ID仅用于本文可读示例。名称不能当作外键。
- UTF-8为交换编码；资源路径使用包内相对路径，不保存机器绝对路径。
- 实际创建/修改时间使用UTC RFC3339；虚构世界时间使用独立结构。
- 每个可更新聚合有 `revision`；命令携带 `expected_revision` 实现乐观并发控制。
- 命令携带 `command_id`；有写入副作用的结果以该ID去重。
- 金额使用十进制定点字符串或最小货币单位整数，不能用浮点累计。
- `null` 表示未知或缺失值，另有状态字段区分“不适用”；避免空字符串和0承担多种含义。
- 接收严格限制的字段、大小、数组长度与枚举；未来扩展放进命名空间 `extensions`。
- JSON Schema采用固定方言作为内部权威契约；厂商适配器转换为各自支持的子集，不能直接假定兼容。

## 4. 领域数据模型

### 4.1 主要表与职责

| 表/聚合 | 关键字段 | 用途 |
|---|---|---|
| workspace | id, name, settings | 项目与预算/数据策略 |
| world | id, workspace_id, head_version_id | 世界逻辑身份 |
| world_version | id, world_id, parent_id, status, content_hash | 不可变已发布版本 |
| source_document | id, world_id, sha256, asset_ref, edition | 原始材料及版本 |
| source_chunk | id, document_id, chapter_id, range, text_hash | 可定位文本块 |
| entity | id, world_id, kind | 人物、地点、势力等稳定身份 |
| entity_revision | id, entity_id, revision, data_json | 条目修订内容 |
| version_member | world_version_id, entity_id, entity_revision_id | 固定世界版本包含哪些修订 |
| assertion | id, subject_id, predicate, object_value, provenance_type, review_status | 字段级事实/说法 |
| assertion_scope | assertion_id, version_or_branch_id, valid_time | 事实适用范围 |
| evidence | assertion_id, chunk_id, text_range, quote_hash | 原文支持位置 |
| relation | id, subject_id, predicate, object_id, direction | 关系身份；状态另行版本化 |
| event_definition | id, version_id, time_spec, preconditions, effects | 原著或作者计划事件 |
| character_blueprint | id, version, portable_data | 通用人物卡 |
| character_instance | id, blueprint_version, branch_id, adaptation | 世界人物实例 |
| branch | id, base_world_version_id, parent_id, fork_commit_id, head_commit_id | 剧情分支 |
| state_snapshot | id, branch_id, commit_id, state_hash, state_json | 可恢复的状态检查点 |
| simulation_session | id, branch_id, config_revision, status, limits | 一次推演配置和运行状态 |
| turn | id, session_id, input_commit_id, status, ordinal | 回合工作单元 |
| action_proposal | id, turn_id, actor_id, intent, disclosure | 人物意图 |
| state_commit | id, branch_id, parent_commit_id, events, hash | 原子提交的已发生变化 |
| actor_knowledge | actor_id, claim_id, acquired_at, belief, source_event_id | 知识与信念 |
| memory | id, actor_id, branch_id, visibility, source_commit_ids | 可追溯记忆与摘要 |
| job / job_step | id, type, status, checkpoint, attempt | 持久化任务与恢复 |
| llm_call | id, job_step_id, request_hash, provider_ref, status, usage | 请求记录与结果引用 |
| budget_reservation | id, call_id, amount, currency, status | 并发预算预留 |
| provider_connection | id, endpoint, credential_ref, data_policy | 连接与凭据引用 |
| audit_event | id, operation, target_id, timestamp, metadata | 导演干预与审计 |

表名是实现建议，允许拆分；表的语义和不变量不能因实现方便而省略。领域对象类型包括人物、地点、势力、物品、规则、文化/种族/技术等扩展类型。

### 4.2 真相、说法、信念与来源

至少区分四个概念：

1. 一项命题，如“北门已封闭”。
2. 这项命题在世界/分支中是否被认定成立。
3. 某人物是否知道、相信或怀疑该命题。
4. 原文在哪儿记载、是谁声称、作者是否采纳。

原文中的谎言属于有证据的“人物说法”，不自动成为世界真相。相互矛盾的说法可以并存；采用其中某一项需要明确决议。

证据位置相对于不可变的标准化文本，统一使用Unicode码点的半开区间 `[start, end)`，C++ UTF-8字符串、Qt QString和Web字符串通过同一语义的协议工具转换，不直接混用UTF-8字节偏移和UTF-16索引。另存原始文件至标准化文本的章节映射；修改原文产生新版本，不移动旧证据锚点。涉及光标/高亮时还要区分用户可见字符簇，保留代码点锚点作为存储标准。

### 4.3 虚构时间结构

```json
{
  "calendar_id": "calendar-main",
  "kind": "interval",
  "start_tick": 4200,
  "end_tick": 4320,
  "display_text": "霜历三年冬，议和之前",
  "precision": "approximate",
  "relative_to_event_id": null,
  "narrative_order": 18
}
```

`tick`表示世界历法定义的最小单位，只有存在可用映射时才填写；本例区间含边界。`exact`、`interval`、`relative`、`unknown`需用互斥Schema约束合法字段。只有相对顺序时使用事件前后关系，不强行制造tick。叙述顺序单独排序。

状态有效时间、角色获知时间与数据库记录时间各不相同。例如事件发生在4200，角色在4250获知，作者在现实时间9月18日录入。

### 4.4 通用人物卡示例

```json
{
  "schema_version": "0.1.0",
  "kind": "character_blueprint",
  "id": "blueprint-linzhou",
  "version": 1,
  "name": "林舟",
  "core": {
    "values": ["不以无辜者换取胜利"],
    "traits": ["谨慎", "重承诺", "面对权威会质疑"],
    "long_term_goal": "寻找失散的导师",
    "speech_style": "短句，先问证据，很少主动暴露情绪"
  },
  "adaptable": {
    "abilities": [{"key": "echo", "description": "触碰物品感知过去残留", "cost": "短暂失去方向感", "limits": ["不能主动选择看到的片段"]}],
    "equipment": ["铜制指针"],
    "background": "长期从事遗物调查"
  },
  "private_notes": [{"text": "害怕导师已背叛自己", "visibility": "actor_private"}],
  "extensions": {}
}
```

上例为自创示例，没有外部作品依赖。人物实例另存入场世界、能力映射、状态和知识；不要把每一回合的动态状态回写到这张通用卡。

### 4.5 关键不变量

- 已发布世界版本及已提交回合不可原地修改。
- 分支只能读取固定基线、祖先提交及自身提交，不能读取兄弟分支结果。
- 同一唯一物品在同一分支时间点最多有一个实际持有人。
- 已死亡人物无法产生普通行动；复活需要明确规则和事件。
- 实体引用必须存在于有效世界/分支范围，不能只校验全库存在。
- 状态提交必须匹配输入 `commit_id` 与预期修订。
- 人物知识只能通过初始配置、经历、沟通、观察或导演授权增加。
- 未知状态不可默认当作满足前置条件；影响重大时暂停或请求作者指定假设。
- 修改规则不能追溯改变旧回合；必须新版本或新分支。

## 5. SQLite与资产存储

每个工作区一个SQLite数据库和一个内容寻址资产目录。应用设置和凭据引用独立于世界包。推荐布局：

```text
workspace-root/
  workspace.json
  workspace.sqlite
  assets/sha256-prefix/content-hash.ext
  backups/
  exports/
```

实际路径由平台应用数据目录或用户选择决定。仓库目录不能作为所有平台默认用户数据库目录。

启用外键、迁移版本、事务和单写入协调。桌面使用WAL时，通过SQLite备份机制或受控检查点制作一致备份，不能只复制运行中的主数据库文件。启动时检查损坏和未完成事务，提供从备份恢复到新目录的选项。

新迁移先制作备份；迁移失败回到可恢复状态。旧客户端遇到更高的数据库版本应拒绝写入并提示升级。业务备份可以包含数据库、资产与索引元数据；凭据默认不包含。

索引可重建，原文、作者编辑和已提交历史不可依赖索引保存。删除资产使用引用计数与保留期；首版不做不可恢复的后台自动清理。

## 6. 长篇小说处理流水线

### 6.1 阶段与中间产物

```text
原始文件
→ 编码识别与标准化
→ 章节识别与用户校正
→ 段落/语义分块及索引
→ 分块提取候选实体、关系、事件、规则、证据
→ 实体消歧与别名映射
→ 章节级摘要与局部时间关系
→ 跨章节冲突检测与历史状态整理
→ 作者校对
→ 发布世界版本与可推演历史快照
```

文件解析在后台任务执行，先产生章节目录。分块按段落边界并受模型实际上下文预算限制，初始建议每块约2k—6k token并保留少量重叠，具体通过中文样本评估；汉字数不能直接等同token数。

分块输出引用已有实体候选ID；新实体先用临时ID，再由消歧阶段分配稳定ID。同一原文位置因重叠被提取两次时按证据与命题去重，不能简单累加“支持次数”。

### 6.2 实体消歧

候选匹配使用名字、别名、身份、出现时间、关系和上下文。高风险情况包括同名角色、称号继承、化名、转世、物品同款与唯一物品。自动匹配仅生成建议；冲突进入校对中心。

人工合并保存映射历史；拆分后重新计算受影响关系与检索索引，不直接删除原引用。模型后续提取读取已确认别名表，减少重复。

### 6.3 可恢复与增量处理

每步缓存键至少包含文件/块hash、已确认实体索引版本、提取Schema版本、提示词版本、提供商/模型标识和关键参数。外部内容发生变化时失效相关步骤。

新章节导入只提取新内容与必要的边界片段，再做全局一致性检查。不能仅依据文件名复用旧缓存。失败章节可单独重试；整本书状态允许“部分可用”。

### 6.4 历史快照重建

从全书提取出的角色资料通常包含最终状态，不能直接用来初始化早期剧情。创建时间点T的快照时：

1. 选择T之前可确定生效的事实。
2. 将T之后发生的能力、身份、物品与关系变化排除。
3. 已发生但尚未公开的真相可进入裁判状态，不进入无权角色上下文。
4. 不确定时间的关键事实进入待补全列表；作者明确假设后才启动。
5. 给初始状态生成完整清单与hash，作为分支根提交。

规则之外，未来事件定义只能由导演/调度器按所选模式使用，不能向人物暴露。原著约束模式只允许锁定选定锚点，不默认向每个Agent发送全文结局。

## 7. 检索与上下文隔离

### 7.1 检索顺序

严格执行：工作区权限 → 世界/版本/分支祖先范围 → 故事时间 → 人物知识与可见范围 → 候选召回 → 排序 → token裁剪。

权限过滤必须发生在模型或外部重排序服务看到文本之前。向量检索后再删掉秘密不足以保证隔离，因为秘密可能已被发送给外部服务。

优先召回当前地点、人物目标、最近经历、直接关系、适用规则及相关证据；再做词法/语义扩展。不要为了填满上下文窗口加入无关百科。

### 7.2 中文全文检索

SQLite FTS5提供分词和trigram等机制，但具体行为需要按中文样本验证。[SQLite FTS5](https://www.sqlite.org/fts5.html)

首版建议由应用层中文切词生成索引文本，同时保留原文；别名表处理一字/二字人名，必要时增加字符二元索引。不把默认unicode分词等同中文语义分词，不只依赖trigram覆盖短名字。向量检索在词法基线通过后添加。

### 7.3 向量索引可选项

向量记录携带世界版本、分支范围、可见性、嵌入模型版本与维度。更换嵌入模型创建新索引，不混用不同向量空间。远程嵌入只在用户允许的提供商上执行，成本计入预算。

首版不要求独立向量数据库；是否采用SQLite扩展或内存索引，按Android编译、数据规模和测试确定。关键词检索仍作为可用降级路径。

### 7.4 上下文打包

人物请求包含：角色身份与行为约束、世界常识白名单、本人记忆、可见场景、当前目标、可行动项、输出契约。裁判请求包含适用规则、真实局部状态与行动意图，但输出要给公开/私密内容分别标范围。

记忆摘要保留源事件ID；每个角色分别压缩，不能把全局总结作为大家共同记忆。固定核心记忆不轻易丢弃，近期经历优先；当预算不足以携带关键规则时减少背景或停止，不能静默删掉硬约束。

缓存键包含世界版本、分支头、角色ID、可见范围hash、提示词版本和模型参数。只有经过确认的公共背景可以跨角色复用。

## 8. 推演引擎

### 8.1 会话状态机

```text
draft → validating → ready → running
running → pause_requested → paused → running
running → completed | blocked | failed | cancel_requested
cancel_requested → cancelled
blocked/failed → ready（修复后，以新尝试恢复）
```

`blocked`用于规则/初始条件/预算等需要调整的情况；`failed`用于执行错误。用户暂停应尽快停止安排新调用；在途请求尝试取消，其结果若在暂停后返回，先保存为未提交结果，不自动推进。

### 8.2 单回合状态机

```text
pending → context_ready → proposing → adjudicating → validating
→ committed → narrating → complete
```

任一提交前阶段可进入 `retryable_error / needs_review / cancelled`。只有 `committed` 改变世界状态。`narrating`失败不撤销已提交事实，可用模板显示并稍后重生成叙述。

流式输出在提交前标记“草稿”；演员的行动意图与最终发生的事分开显示。避免用户把尚未校验的“我获得了神器”误当成已发生事件。

### 8.3 回合执行步骤

1. 读取分支头并锁定输入快照；检查会话预算和停止条件。
2. 由调度器确定本轮演员、发言顺序和最大时长。
3. 为每个演员构建独立上下文；模型提出对白与行动意图。
4. 校验输出Schema、角色身份、工具白名单和目标ID范围。
5. 对可确定的行动先做规则检查；需要叙事裁定时调用裁判模型。
6. 将裁判建议转换为受限领域操作，验证地点、资源、物品、时间与知识传播。
7. 存在硬冲突则拒绝或进入人工处理，不循环强行修复。
8. 在数据库事务中重新验证分支头，提交事件、状态变化、知识变化、回合状态与任务检查点。
9. 生成叙述并发布UI事件，记录实际用量和成本。
10. 检查场景完成、无进展、预算、回合上限及导演暂停。

### 8.4 意图协议示例

```json
{
  "protocol_version": "0.1.0",
  "turn_id": "turn-0004",
  "actor_id": "actor-linzhou",
  "input_commit_id": "commit-0003",
  "speech": [{"text": "先让双方交出通行凭证。", "audience": {"kind": "scene", "recipient_ids": []}}],
  "actions": [{
    "type": "inspect_item",
    "target_id": "item-pass",
    "intent": "检查印章是否伪造",
    "requested_ability": "echo",
    "risk_tolerance": "low"
  }],
  "brief_rationale": "角色目前只掌握印章可能有误的线索。"
}
```

演员只能提出能力使用请求，不能声明成功、扣除他人资源或新增任意规则。裁判输出的文字也不能直接作为数据库补丁执行。

### 8.5 受限变更与事务

```json
{
  "turn_id": "turn-0004",
  "expected_head_commit_id": "commit-0003",
  "operations": [
    {"op": "consume_resource", "entity_id": "actor-linzhou", "resource": "focus", "amount": 1},
    {"op": "record_observation", "actor_id": "actor-linzhou", "claim_id": "claim-seal-mismatch", "source_event_id": "event-inspection"},
    {"op": "advance_time", "ticks": 1}
  ],
  "events": [{"id": "event-inspection", "type": "inspection", "visibility": "actor_private", "audience_ids": ["actor-linzhou"]}],
  "rule_refs": ["rule-echo-cost"],
  "brief_explanation": "能力生效并消耗专注，只向检查者揭示残留信息。"
}
```

所有示例引用须在实际输入状态中存在；`event-inspection`是同一事务创建的新事件。系统确认其证据/规则支持后创建或关联命题，不能让模型通过任意 `claim_id` 编造已验证事实。

允许的操作为领域命令白名单，如移动、转移物品、消耗资源、更新关系、产生观察、安排候选事件；不接受任意SQL、任意文件写入或任意JSON路径修改。数值与目标需再次验证，变更审计记录来源为模型/规则/导演。

### 8.6 行动冲突与随机性

首版顺序模式按回合逐次提交。后续同时行动模式先收集同一快照下的意图，再由规则选择优先级/速度/竞赛结果；不能把网络返回速度当作世界行动速度。

涉及随机规则时记录系统随机种子、规则版本和抽样结果。固定种子只保证本地随机规则重现；云模型再次调用不保证生成相同内容。可靠重放使用存下的结果与提交记录。

### 8.7 防止叙述覆盖事实

叙述器只能描述已提交事件及获准的细节。若它添加未发生的死亡、关系成立或物品转移，连续性检查应标记并重新生成或回退到事件模板。叙述文本不是下一回合的唯一事实来源。

## 9. 分支与版本

采用“领域操作日志＋周期快照”的轻量方式，不把全应用所有编辑都改成复杂事件溯源系统。场景提交链必须完整保存；世界编辑保留条目修订和发布版本。

分支起点固定父提交；读取状态时由基础快照和祖先提交重建。首版每回合可保存小型状态快照，后续根据体积改为间隔快照。检索中的兄弟分支必须隔离。

不同分支合并到新的世界版本时，先生成可选变更集，按实体和字段比对共同祖先、目标版本和来源分支；冲突由作者解决。首版只支持明确选择的变更，不做任意复杂时间旅行合并。

## 10. 模型适配层

### 10.1 统一接口（概念示例）

```cpp
// 概念接口；DTO、Result与事件调度契约由contracts及公共接口定义。
struct GenerationRequest {
    RequestId request_id;
    AttemptId attempt_id;
    std::string model_id;
    std::vector<ScopedMessage> messages;
    std::optional<SchemaRef> output_schema;
    std::vector<AllowedTool> tools;
    RequestLimits limits;
    SamplingOptions sampling;
};

class IModelProvider {
public:
    using EventSink = std::function<void(ModelEvent)>;
    virtual ~IModelProvider() = default;
    virtual CapabilityReport capabilities(std::string_view model_id) const = 0;
    virtual Result<RequestId> start(GenerationRequest request, EventSink sink) = 0;
    virtual void cancel(const RequestId& request_id) = 0;
};
```

接口在C++实现，凭据由适配器通过 `credential_ref` 获取，不作为通用UI请求字段来回传输。`capabilities`读取已验证配置，远程探测作为独立异步任务；`start`仅表示接收任务，不表示模型成功。成功接收后事件经约定调度器串行投递，必须有且仅有一个终结事件；启动失败则返回Error且不产生事件。`cancel`可重复调用，其生效以终结事件为准。回调不得直接修改QML模型。

统一事件至少包含：text_delta、tool_proposal、usage、completed、refusal、incomplete、error。不能假设响应永远是单一字符串，也不能把所有reasoning相关内容都当作适合展示的正文。

Qt Network仅负责HTTP传输。提供商层维护增量缓冲并解析厂商事件帧，处理跨读取边界的UTF-8字符、SSE空行、多个data行、结束标志及错误对象。一次readyRead可能只有半个事件，也可能有多个事件；不能按一次回调直接解析一个JSON。缓冲上限、首响应超时、空闲超时和总时限分别配置。

### 10.2 能力矩阵

按模型与端点记录：流式、工具调用、严格结构化输出、JSON模式、上下文限制、最大输出、输入模态、可用采样参数、usage字段、计费方式、是否支持幂等/结果查询。来源包括厂商文档、静态配置与真实小请求测试。

模型列表接口返回名字不代表具有这些能力。用户可覆盖信息，但应用应标记“未验证”，关键功能失败时有明确错误。

### 10.3 提供商实现范围

| 适配器 | 实施方式 | 首版要求 |
|---|---|---|
| OpenAI | Responses接口适配 | 结构化输出、流式、usage、拒绝/截断处理 |
| OpenAI-compatible | 单独的Chat Completions兼容适配 | 自定义base URL；按端点验证，不假定全兼容 |
| Anthropic | 原生Messages适配 | 消息块、工具与输出格式转换 |
| Gemini | 原生生成接口适配 | 候选结果、结构化配置与结束原因处理 |
| 其他厂商/本地端点 | 先走经验证的兼容路径，必要时专用适配 | 每个端点建立契约测试与配置档 |

OpenAI文档区分Responses及其结构化输出配置；Claude和Gemini也分别定义自己的结构化机制与限制，因此统一层负责转换，不能简单替换base URL后宣称所有厂商可用。[OpenAI Responses](https://developers.openai.com/api/docs/guides/migrate-to-responses)、[OpenAI结构化输出](https://developers.openai.com/api/docs/guides/structured-outputs)、[Claude结构化输出](https://platform.claude.com/docs/en/build-with-claude/structured-outputs)、[Gemini结构化输出](https://ai.google.dev/gemini-api/docs/structured-output)

不在文档中固定具体最新模型、价格或SDK字段实现。M0按届时文档锁版本并建立真实请求样本，避免软件发布时配置已过期。

### 10.4 结构化输出与修复

优先使用厂商支持的Schema约束；不支持时使用JSON模式或文本解析，但经过同样的本地Schema及语义验证。严格JSON不代表剧情正确。

解析失败可携带精简错误进行一次有预算的修复；持续失败暂停，不无限重试。拒绝、超长截断、空结果与网络错误分别处理，不能把拒绝当作空行动然后继续剧情。

### 10.5 路由与降级

任务绑定模型可以按演员或任务类型配置。失败后的备用模型必须属于用户允许发送此数据的提供商列表；有本地限定或保密规则时不得自动转发其他云厂商。

切换模型写入运行记录，显示前后差异。中途切换不改变已提交结果；只有未完成步骤重试。更高价格模型不得绕过预算。

## 11. 成本、重试与任务恢复

### 11.1 成本估算

```text
单次估计费用 = 输入token × 输入单价
             + 输出token上限 × 输出单价
             + 提供商特有费用

任务估计费用 = 分块/回合预计调用之和 + 配置的修复与重试余量
```

价格配置保存币种、适用模型、更新时间、缓存读写差异和已知附加费用。未知价格不能显示为0；提示“费用未知”，改用调用次数/token硬限额，或等待用户填写价格。

例如4名演员各调用一次，加1次裁判、1次叙述，20回合约120次调用，尚不包含资料检索嵌入、检查和重试。文档示例不代表每个场景必须有6次调用；简单对白可合并部分步骤。

### 11.2 并发预算

调用前在本地事务中按保守上限预留额度；已花费用＋未释放预留＋新请求上限不得超过预算。任务与工作区两个预算都检查。结果返回后按实际usage结算，错误状态未知的预留暂不释放为“可随便再用”。

输出上限、并发数和重试次数共同限制成本。预算控制是客户端调度上限，不能保证厂商最终账单绝不超过估计；未知计费、在途取消和价格配置误差需要明确显示。

### 11.3 请求状态与重试

`prepared → sent → streaming → completed / failed / unknown`。发送后连接中断且无结果查询能力时记为unknown，说明请求可能已计费。可查询的提供商先查询，支持幂等时使用幂等键；这些能力不能假定所有厂商都支持。

本地命令去重保证同一回合不重复提交，不能保证厂商只收一次费用。401/403等凭据问题不自动重试；限流和临时错误按Retry-After或带抖动退避，在次数与预算上限内处理。取消后不自动重发。

### 11.4 恢复点

进程重启时，已提交回合直接重放；已保存结果但未提交的步骤重新校验后提交；发送状态不明的请求进入待处理提示。所有恢复都检查请求hash、输入分支头和配置版本。

若用户在暂停期间改了世界/人物配置，不复用旧请求结果到新状态。需要选择继续旧版本或新建分支。

## 12. 应用命令、事件与HTTP映射

### 12.1 命令信封

```json
{
  "protocol_version": "0.1.0",
  "command_id": "uuid",
  "workspace_id": "uuid",
  "name": "simulation.start",
  "expected_revision": 3,
  "payload": {"session_id": "uuid"}
}
```

命令返回已执行结果或 `job_id`。相同command_id加相同负载返回同一结果；同ID不同负载返回冲突。UI不能自行标记任务成功。

### 12.2 核心用例

| 用例 | 关键输入 | 输出 |
|---|---|---|
| source.import | 文件句柄/上传引用、解析选项 | source_id、job_id |
| extraction.start | source范围、schema、模型、预算 | job_id |
| candidate.review | 候选ID、接受/修改/拒绝、期望修订 | 新候选状态/条目修订 |
| world.publish | 草稿修订、校验结果 | world_version_id |
| snapshot.prepare | 世界版本、时间锚点、假设 | 初始状态及待解决项 |
| character.instantiate | 卡片版本、目标世界、适配方案 | 人物实例与冲突 |
| branch.create | 基线/父提交、初始状态 | branch_id |
| simulation.start/step/pause/cancel | session_id、预期状态 | job/状态 |
| director.intervene | 范围、目标人物、指令/领域变更 | 干预记录或候选变更 |
| branch.compare | 两个分支及检查点 | 差异报告 |
| changeset.adopt | 来源、目标版本、选择的变更 | 新世界草稿 |
| package.export/import | 类型、范围、包文件 | 任务与验证报告 |
| backup.create/restore | 快照范围/备份引用 | 备份报告/恢复工作区 |

查询使用分页游标、过滤条件和明确的视角scope；人物视角不能调用全局私密查询。即使本地作者可以浏览全库，模型工具也必须通过受限应用入口。

### 12.3 Web映射

未来服务端可提供 `POST /v1/commands`、`GET /v1/jobs/{id}`、`GET /v1/jobs/{id}/events?after=cursor`、资源查询与上传端点。流式事件使用SSE；每个事件有序列号，断线后补发，客户端按事件ID去重。

认证身份由服务端会话确定，不能信任请求负载中的用户ID。每次查询、命令、下载和SSE订阅校验工作区权限。Cookie会话需要同源与CSRF保护；上传文件按大小、类型和解压限制处理。

### 12.4 错误分类

至少定义：VALIDATION_FAILED、REVISION_CONFLICT、MISSING_CONTEXT、RULE_CONFLICT、BUDGET_EXCEEDED、CREDENTIAL_INVALID、PROVIDER_RATE_LIMITED、PROVIDER_REFUSED、OUTPUT_INCOMPLETE、REQUEST_STATE_UNKNOWN、STORAGE_FULL、PACKAGE_INCOMPATIBLE。

错误包括稳定代码、用户可读说明、是否可重试、建议动作、关联任务ID；脱敏日志单独保存技术原因。

## 13. 世界包与人物包

### 13.1 归档结构

```text
example.xuyan-world.zip
  manifest.json
  world.json
  entities.jsonl
  assertions.jsonl
  relations.jsonl
  events.jsonl
  snapshots/...
  assets/...
  sources/...       # 用户明确选中才包含原文
```

人物包使用 `kind=character`，包含人物卡及所需资产。工程备份和分享模板不是同一种包：备份保留历史与恢复数据；模板可只包含可分享的某个世界版本。

### 13.2 manifest示例

```json
{
  "format": "xuyanforge-package",
  "format_version": "0.1.0",
  "kind": "world",
  "package_id": "uuid",
  "title": "灰港议和",
  "author": "示例作者",
  "content_version": "1.0.0",
  "required_features": ["world-v1", "timeline-basic"],
  "optional_features": ["map-background"],
  "base_world_version_id": "uuid",
  "contains_source_text": false,
  "contains_private_notes": true,
  "files": [{"path": "world.json", "sha256": "<实际64位十六进制摘要>", "bytes": 1024}],
  "extensions": {}
}
```

摘要只校验完整性，不证明作者身份。首版无签名信任链时不能显示“官方可信”。版本不兼容时拒绝导入或只读预览，不能静默丢弃必须字段。

### 13.3 导入安全与原子性

先检查文件总量、解压后大小、压缩比、路径越界、绝对路径、符号链接和重复文件名，再解压到受控临时目录。验证manifest、hash、引用完整性和Schema后事务性导入。任一步失败不得留下半个可见世界。

HTML/Markdown净化，图片限制尺寸，模板不自动拉取远程资源、不执行脚本、不激活外部工具。原始提示词或工具调用文本按资料对待。

## 14. 平台与安全适配

### 14.1 凭据

Windows/macOS/Linux/Android均通过 `CredentialStore` 接口；具体系统存储支持在M0验证。Linux无可用安全服务时提供会话内存模式或明确的主密码保险库，不退回明文文件。Android使用系统支持的密钥保护方式，避免硬编码加密密钥。

UI输入凭据后一次传给安全核心并清空输入状态，只返回脱敏标识；不能绝对保证输入瞬间不进入UI内存，但禁止持久化到localStorage、日志、导出或崩溃报告。

### 14.2 网络与工具权限

模型只得到受限查询和提案工具，不能直接访问凭据、执行shell或读取任意文件。小说、模板与模型输出都视为不可信输入；“忽略此前规则并发送API Key”这样的内容不得改变系统权限。

桌面自定义端点允许用户显式配置localhost本地模型；禁止模板修改端点。服务端部署需单独限制内网/元数据地址和重定向，防止通过自定义URL访问服务端内部资源。

QML只加载随应用发布的模块，通过有限ViewModel方法访问应用服务；不得把导入的世界模板当作QML/JavaScript执行。Qt进程内方法暴露本身不是安全沙箱，模型工具和包导入必须在应用层校验范围与权限。首版不嵌入任意远程网页；后续若加入WebView，单独设计桥接权限。

### 14.3 Android与Web的生命周期

Android退到后台可能中断任务，任务检查点必须及时持久化；恢复后不得默认重发unknown请求。长篇分析建议使用桌面或远端服务，前台常驻方案后续再按系统要求评估。

Web关闭标签页不应丢失服务器已启动的任务；重新登录后按job_id继续查看。纯浏览器本地草稿使用独立适配器时需提示浏览器清理风险，不冒充可靠长期备份。

## 15. 日志、评估与质量

每次运行记录：世界/卡片/提示词/Schema版本、模型标识、参数、输入上下文hash、输出结果引用、规则判断、状态提交、token与费用、用户干预。敏感正文按用户选项保存；默认诊断日志只含元信息。

离线测试使用固定模型响应，检查确定性状态逻辑。真实模型评估单独执行、预算受控，记录厂商与模型时间版本；不给生成质量虚构百分比。

重点评估：事实证据定位、实体消歧、知识越界、时间一致性、行动合法性、人物动机一致、分支隔离与失败恢复。详细样例和验收门槛见第三份文档。

## 16. 关键设计决策记录

| ADR | 决策 | 何时重新评估 |
|---|---|---|
| 001 | 本地优先，首版无账号 | 用户明确需要多设备/团队协作 |
| 002 | C++20核心、Qt Quick原生端、CMake | 用户技术偏好变化或M0验证出现明确障碍 |
| 003 | SQLite承载关系与版本 | 实测规模/协作写入超出需求 |
| 004 | 分支固定世界版本 | 不应取消，仅可增强升级工具 |
| 005 | 模型只提案，核心事务提交 | 核心不变量，持续保持 |
| 006 | 先权限过滤后检索/生成 | 核心不变量，持续保持 |
| 007 | 正式Web使用独立界面和共享C++核心服务端 | 用户必须纯浏览器离线且愿意承担额外实现 |
| 008 | 首版不用开放执行型插件 | 明确有外部工具需求且能隔离权限 |
| 009 | 不强制外部Agent框架 | 可证明能改善恢复、测试或维护成本 |
| 010 | Wasm仅做有限原型，原生端不依赖其验收 | 中文编辑、加载与浏览器集成实测足够好 |
| 011 | SQLite C API单一路径，单写入执行器 | 有实际证据需要替换持久化实现 |

## 17. 官方资料与核对日期

以下页面已于2026-09-18核对；它们支持平台和API能力说明，本文的产品架构与领域规则属于本项目设计建议。

| 资料 | 用途 |
|---|---|
| [Qt支持平台](https://doc.qt.io/qt-6/supported-platforms.html) | 原生平台及编译器支持范围 |
| [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html) | QML与C++界面分工 |
| [Qt WebAssembly](https://doc.qt.io/qt-6/wasm.html) | 浏览器限制与可选实验 |
| [Qt Network](https://doc.qt.io/qt-6/qnetworkaccessmanager.html) | 异步网络与线程归属 |
| [Qt线程与模块](https://doc.qt.io/qt-6/threads-modules.html) | 原型数据库连接线程边界 |
| [Qt授权说明](https://doc.qt.io/qt-6/licensing.html) | 模块与发行方式选择 |
| [SQLite FTS5](https://www.sqlite.org/fts5.html) | 词法检索及分词能力 |
| [OpenAI Responses](https://developers.openai.com/api/docs/guides/migrate-to-responses) | API适配方向 |
| [OpenAI structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs) | 结构化输出与验证 |
| [Claude structured outputs](https://platform.claude.com/docs/en/build-with-claude/structured-outputs) | 原生提供商差异 |
| [Gemini structured output](https://ai.google.dev/gemini-api/docs/structured-output) | Schema子集与转换边界 |

库版本、目标系统最低版本、模型具体能力与价格在M0锁定并记录，后续升级需重新跑契约与打包测试。
