#pragma once

#include "xuyan/domain/scenario.h"
#include "xuyan/domain/character_blueprint.h"
#include "xuyan/domain/source_document.h"
#include "xuyan/domain/retrieval.h"
#include "xuyan/domain/world_version.h"
#include "xuyan/domain/world_graph.h"
#include "xuyan/domain/character_instance.h"
#include "xuyan/domain/simulation_session.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/domain/world_entity.h"
#include "xuyan/domain/world_template.h"
#include "xuyan/domain/evidence.h"
#include "xuyan/domain/extraction_job.h"
#include "xuyan/domain/extraction_candidate.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace xuyan::storage {

// 兼容既有仓储接口名称；目录值实际属于领域层，界面通过应用服务而非仓储依赖它。
using WorldTemplate = xuyan::domain::WorldTemplate;

/* 职责：独占本线程的SQLite工作区连接，提供迁移、只读查询及原子写入。
 * 生命周期：构造打开，析构关闭；禁止复制，不能跨线程共享实例。后台任务各自构造实例。
 * 边界：查询/事务在本机完成，不持数据库事务等待模型网络，不创建默认样例。
 */
class WorkspaceRepository {
public:
    /*
     * 功能：打开工作区数据库并执行必要的结构迁移。
     * 参数：
     *   database_path：本机工作区数据库路径；构造期间借用，连接随后由仓储独占。
     * 返回：初始化后独占本机数据库连接的仓储对象；必要时创建数据库并迁移结构。
     * 失败：打开或迁移失败抛runtime_error，不能创建成功对象。
     * 副作用：打开本机连接并执行支持的迁移；不预置世界、来源或人物。
     */
    explicit WorkspaceRepository(const std::filesystem::path& database_path);
    /*
     * 功能：关闭当前工作区数据库连接。
     * 参数：无。
     * 返回：无；关闭独占连接，不删除数据库和资产。
     * 失败：析构不抛异常；数据库应无仍存活的语句。
     * 副作用：释放数据库连接；调用者须先结束所有借用该连接的语句与事务。
     */
    ~WorkspaceRepository();

    /* 功能：禁止复制独占连接。参数：另一个仓储。返回：无，此操作在编译期禁止。 */
    WorkspaceRepository(const WorkspaceRepository&) = delete;
    /* 功能：禁止复制赋值。参数：另一个仓储。返回：无，此操作在编译期禁止。 */
    WorkspaceRepository& operator=(const WorkspaceRepository&) = delete;

    /*
     * 功能：轻量核对当前版本的核心资料表是否齐全，供切换工作区前拒绝明显不完整的数据库。
     * 参数：无；只检查本实例已打开的当前工作区，不读取世界、小说或人物行。
     * 返回：核心结构存在时成功值为 true；不完整时返回失败，不以 false 冒充可用工作区。
     * 失败：缺失核心表或 SQLite 查询失败经 Result.error 返回，不尝试猜测或修补损坏结构。
     * 副作用：仅读取 sqlite_master，不修改记录、不启动迁移以外的写入、不发送网络请求。
     * 线程与生命周期：在仓储所属线程同步查询，返回值不借用连接；不能替代完整数据库完整性检查。
     */
    xuyan::domain::Result<bool> validateCoreSchema();

    /*
     * 功能：用调用方提供的状态显式创建初始分支，已有活动分支时拒绝覆盖。
     * 参数：
     *   branch_id：分支稳定标识；只定位该分支，不按显示名称猜测。
     *   branch_name：新分支的用户显示名称。
     *   commit_id：提交稳定标识；指向不可变快照。
     *   initial_state：调用者提供的完整初始状态；借用读取，不自动补充人物或样例。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：用调用方提供的状态显式创建初始分支，已有活动分支时拒绝覆盖；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> createRootBranch(
        const std::string& branch_id, const std::string& branch_name,
        const std::string& commit_id, const xuyan::domain::ScenarioState& initial_state);
    /*
     * 功能：读取指定分支的最新提交及其状态快照。
     * 参数：
     *   branch_id：分支稳定标识；只定位该分支，不按显示名称猜测。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> loadHead(const std::string& branch_id);
    /*
     * 功能：按提交标识读取不可变状态快照。
     * 参数：
     *   commit_id：提交稳定标识；指向不可变快照。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> loadCommit(const std::string& commit_id);
    /*
     * 功能：返回当前选中分支的标识。
     * 参数：无。
     * 返回：成功值为当前活动分支稳定标识。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::string> activeBranchId();
    /*
     * 功能：列出模拟分支及其父分支和最新提交。
     * 参数：无。
     * 返回：成功值为分支轻量元数据列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::BranchInfo>> listBranches();
    /*
     * 功能：查找既有命令的提交结果，用于幂等重放。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     * 返回：成功值为分支及不可变提交快照可选值；未找到该命令的提交记录时返回成功空值。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::optional<xuyan::domain::CommitView>> replayCommand(
        const std::string& command_id);

    /*
     * 功能：在预期提交仍为分支头时原子写入下一状态。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   payload_hash：请求正文的规范摘要；同一命令重放时必须保持一致。
     *   expected：调用者已读取的分支头和状态修订；写入前再次核对，防止覆盖并发修改。
     *   next_state：准备提交的下一状态值；由调用者显式提供，不用模型文本直接替代。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：在预期提交仍为分支头时原子写入下一状态；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> commitStep(
        const std::string& command_id,
        const std::string& payload_hash,
        const xuyan::domain::CommitView& expected,
        const xuyan::domain::ScenarioState& next_state);

    /*
     * 功能：以新提交记录分支的暂停状态。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   expected：调用者已读取的分支头和状态修订；写入前再次核对，防止覆盖并发修改。
     *   paused：true记录暂停，false记录继续；改变分支状态而不执行新回合。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：以新提交记录分支的暂停状态；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> setPaused(
        const std::string& command_id,
        const xuyan::domain::CommitView& expected,
        bool paused);

    /*
     * 功能：从指定提交创建新分支并复制其状态快照。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   source_commit_id：作为分叉起点的不可变提交标识。
     *   branch_name：新分支的用户显示名称。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：从指定提交创建新分支并复制其状态快照；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> forkBranch(
        const std::string& command_id,
        const std::string& source_commit_id,
        const std::string& branch_name);

    /*
     * 功能：切换活动分支并返回其最新提交。
     * 参数：
     *   branch_id：分支稳定标识；只定位该分支，不按显示名称猜测。
     * 返回：成功值为分支及不可变提交快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：切换活动分支并返回其最新提交；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CommitView> switchBranch(const std::string& branch_id);
    /*
     * 功能：使用 SQLite 在线备份接口保存一致性数据库快照。
     * 参数：
     *   destination：本机备份目标路径；不能与源数据库相同，数据库一致性由SQLite在线备份维持。
     * 返回：成功值为备份文件路径。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在线备份一致数据库到指定文件，不能复制正在变化的裸数据库文件代替。
     */
    xuyan::domain::Result<std::string> backupTo(const std::filesystem::path& destination);

    /*
     * 功能：校验并创建世界实体，重复命令保持幂等。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   entity：待写入世界实体值；分类、来源、世界和修订随实体传入，由写入接口校验。
     * 返回：成功值为世界实体当前版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：校验并创建世界实体，重复命令保持幂等；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> createEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity);
    /*
     * 功能：按预期修订保存实体，冲突时拒绝覆盖。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   entity：待写入世界实体值；分类、来源、世界和修订随实体传入，由写入接口校验。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为世界实体当前版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订保存实体，冲突时拒绝覆盖；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> saveEntity(
        const std::string& command_id, xuyan::domain::WorldEntity entity, int expected_revision);
    /*
     * 功能：读取指定世界实体的当前版本。
     * 参数：
     *   entity_id：世界实体稳定标识；按标识定位，不按名称自动选同名资料。
     * 返回：成功值为世界实体当前版本。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> loadEntity(const std::string& entity_id);
    /*
     * 功能：按关键词和类型分页检索世界实体。
     * 参数：
     *   query：检索关键词；空串表示不限制关键词。
     *   kind：实体分类协议值；空串表示不过滤分类，显示中文在界面层处理。
     *   offset：从零开始的条目偏移，单位是条目而非字节；分页接口校验非负范围。
     *   limit：本页最多条目数，范围1—200；不是整库加载上限。
     * 返回：成功值为实体分页结果和匹配总数。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::EntityPage> searchEntities(
        const std::string& query, const std::string& kind, int offset, int limit);
    /*
     * 功能：按预期修订软删除世界实体。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   entity_id：世界实体稳定标识；按标识定位，不按名称自动选同名资料。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为世界实体当前版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订软删除世界实体；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::WorldEntity> deleteEntity(
        const std::string& command_id, const std::string& entity_id, int expected_revision);
    /*
     * 功能：显式合并同类条目，原子改写证据及关系端点并保留历史；专用本体冲突需单独校对。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   source_id：合并源实体的稳定标识；由作者明确选择，不与小说来源标识混用。
     *   source_expected_revision：合并源实体的预期当前修订；拆分时须满足合并后未再编辑的保护。
     *   target_id：合并目标实体稳定标识，必须由调用者明确选择。
     *   target_expected_revision：合并目标实体的预期当前修订；禁止无修订校验覆盖作者修改。
     * 返回：成功值为合并/拆分结果及历史关联。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：显式合并同类条目，原子改写证据及关系端点并保留历史；专用本体冲突需单独校对；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> mergeEntities(
        const std::string& command_id, const std::string& source_id, int source_expected_revision,
        const std::string& target_id, int target_expected_revision);
    /*
     * 功能：按修订校验撤销既有实体合并。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   merge_id：既有合并历史记录标识；用于恢复记录内的条目和受影响关系引用。
     *   source_expected_revision：合并源实体的预期当前修订；拆分时须满足合并后未再编辑的保护。
     *   target_expected_revision：合并目标实体的预期当前修订；禁止无修订校验覆盖作者修改。
     * 返回：成功值为合并/拆分结果及历史关联。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按修订校验撤销既有实体合并；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::EntityMergeResult> splitEntityMerge(
        const std::string& command_id, const std::string& merge_id,
        int source_expected_revision, int target_expected_revision);

    /*
     * 功能：保存小说来源元数据及章节索引，不写入原文字节。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   document：小说来源元数据及章节索引；原文字节由外部资产管理，本接口不复制原文。
     * 返回：成功值为小说来源及章节索引。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：保存小说来源元数据及章节索引，不写入原文字节；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> saveSource(
        const std::string& command_id, const xuyan::domain::SourceDocument& document);
    /*
     * 功能：创建空白世界模板，不自动附加来源资料。
     * 参数：
     *   id：待创建世界的稳定标识，由调用者分配。
     *   name：世界的用户显示名称，不包含预置资料。
     * 返回：成功值为空白世界模板元数据。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：创建空白世界模板，不自动附加来源资料；不执行网络调用。
     */
    xuyan::domain::Result<WorldTemplate> createWorldTemplate(const std::string& id, const std::string& name);
    /*
     * 功能：列出工作区已有的世界模板。
     * 参数：无。
     * 返回：成功值为实际世界目录元数据列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<WorldTemplate>> listWorldTemplates();
    /* 功能：读取明确选定世界的目录和全部当前条目，保持同一SQLite只读快照。
     * 参数：world_id为调用期间借用的非空世界稳定标识；须存在于目录，不按名称或首项回退。
     * 返回：拥有型WorldExportSnapshot；存在但没有条目的世界返回成功空集合。
     * 失败：空标识、目录缺失、超过100000条或文本字段累计超过32MiB返回校验错误；
     *   SQLite查询、损坏的修订头或分配失败转换为存储错误，不返回部分载荷。
     * 副作用：参数绑定只查询该世界、释放只读快照；不读小说/凭据、不写资料或输出文件。
     * 线程与生命周期：所属线程同步执行，仓储须无其他活动事务；结果独立持有，写包时不继续占用快照。 */
    xuyan::domain::Result<xuyan::domain::WorldExportSnapshot> readWorldExportSnapshot(const std::string& world_id);
    /*
     * 功能：将已导入的小说来源关联到指定世界。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   source_id：已导入且属于 world_id 的非空来源稳定标识；空串不会表示全部来源。
     * 返回：成功值为实际世界目录元数据。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：将已导入的小说来源关联到指定世界；不执行网络调用。
     */
    xuyan::domain::Result<WorldTemplate> attachWorldSource(const std::string& world_id,
                                                            const std::string& source_id);
    /*
     * 功能：列出当前工作区保存的小说来源元数据。
     * 参数：无。
     * 返回：成功值为小说来源及章节索引列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listSources();
    /*
     * 功能：仅列出指定世界的小说来源及其章节索引。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     * 返回：成功值为小说来源及章节索引列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> listSourcesForWorld(
        const std::string& world_id);
    /*
     * 功能：读取指定小说来源与其章节索引。
     * 参数：
     *   source_id：要读取的非空来源稳定标识；缺失来源作为失败返回。
     * 返回：成功值为小说来源及章节索引。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> loadSource(const std::string& source_id);
    /*
     * 功能：按章节修订校验替换来源的章节划分。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   source_id：待校正章节的非空来源稳定标识；不能跨来源替换章节。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   chapters：替换后的完整章节索引；范围采用原文Unicode码点，按值持有供事务写入。
     * 返回：成功值为更新章节修订后的单个来源及完整章节索引；不存在来源返回失败。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按章节修订校验替换来源的章节划分；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SourceDocument> replaceSourceChapters(
        const std::string& command_id, const std::string& source_id, int expected_revision,
        std::vector<xuyan::domain::SourceChapter> chapters);

    /*
     * 功能：创建人物卡首版并保存其可编辑字段。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   blueprint：人物卡的可编辑字段与版本信息；按值传入，不改变调用者对象。
     * 返回：成功值为人物卡指定版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：创建人物卡首版并保存其可编辑字段；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> createBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    /*
     * 功能：基于预期版本写入人物卡的新版本。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   blueprint：人物卡的可编辑字段与版本信息；按值传入，不改变调用者对象。
     *   expected_version：调用者已读取的人物卡版本；不同于其他对象的修订计数。
     * 返回：成功值为人物卡指定版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：基于预期版本写入人物卡的新版本；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> saveBlueprint(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    /*
     * 功能：读取人物卡指定版本，默认读取最新版。
     * 参数：
     *   blueprint_id：人物卡稳定标识。
     *   version：人物卡版本号；默认-1表示读取最新版本。
     * 返回：成功值为人物卡指定版本。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> loadBlueprint(
        const std::string& blueprint_id, int version = -1);
    /*
     * 功能：列出当前工作区的人物卡。
     * 参数：无。
     * 返回：成功值为人物卡指定版本列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> listBlueprints();

    /*
     * 功能：幂等导入已校验的世界实体集合。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   package_hash：已校验包内容的摘要；用来核对同一导入命令的幂等身份。
     *   entities：待导入的已校验实体集合；按值持有，成功时整体事务写入。
     * 返回：成功值为本次导入的记录数；幂等重放遵循原命令结果。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：幂等导入已校验的世界实体集合；不执行网络调用。
     */
    xuyan::domain::Result<int> importEntities(const std::string& command_id,
                                              const std::string& package_hash,
                                              std::vector<xuyan::domain::WorldEntity> entities);
    /*
     * 功能：幂等导入人物卡的多个历史版本。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   package_hash：已校验包内容的摘要；用来核对同一导入命令的幂等身份。
     *   versions：同一人物卡的历史版本集合；按值持有供导入，不把历史版本静默改成最新。
     * 返回：成功值为本次导入的记录数；幂等重放遵循原命令结果。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：幂等导入人物卡的多个历史版本；不执行网络调用。
     */
    xuyan::domain::Result<int> importBlueprintVersions(
        const std::string& command_id, const std::string& package_hash,
        std::vector<xuyan::domain::CharacterBlueprint> versions);

    /*
     * 功能：按预期修订保存不含明文密钥的模型连接配置。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   connection：模型连接配置值，仅含凭据引用，不存明文密钥。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为不含明文密钥的连接配置。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订保存不含明文密钥的模型连接配置；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> saveProviderConnection(
        const std::string& command_id, xuyan::domain::ProviderConnection connection, int expected_revision);
    /*
     * 功能：按标识读取模型连接配置。
     * 参数：
     *   connection_id：模型连接稳定标识；查询不探测网络。
     * 返回：成功值为不含明文密钥的连接配置。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> loadProviderConnection(const std::string& connection_id);
    /*
     * 功能：列出未删除的模型连接配置，结果仍可能包含停用项。
     * 参数：无。
     * 返回：成功值为不含明文密钥的连接配置列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ProviderConnection>> listProviderConnections();
    /*
     * 功能：按预期修订软删除模型连接配置。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   connection_id：模型连接稳定标识；查询不探测网络。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为不含明文密钥的连接配置。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订软删除模型连接配置；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ProviderConnection> deleteProviderConnection(
        const std::string& command_id, const std::string& connection_id, int expected_revision);

    /*
     * 功能：新增世界资料与原文范围之间的证据引用。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   evidence：资料与原文范围之间的证据元数据；原文偏移、摘要和来源须保持对应。
     * 返回：成功值为原文证据元数据。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：新增世界资料与原文范围之间的证据引用；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::EvidenceReference> createEvidence(
        const std::string& command_id, xuyan::domain::EvidenceReference evidence);
    /*
     * 功能：列出指定小说来源对应的证据引用。
     * 参数：
     *   source_id：要检索的小说来源稳定标识；此接口不按世界汇总，空串不会代表全部来源。
     * 返回：成功值为原文证据元数据列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listEvidenceForSource(
        const std::string& source_id);

    /*
     * 功能：创建持久化解析任务与待执行步骤。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job：解析任务及待执行步骤；写入被冻结的输入模式、版本和配置，不执行模型。
     * 返回：成功值为任务及其完整步骤。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：创建持久化解析任务与待执行步骤；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> createExtractionJob(
        const std::string& command_id, xuyan::domain::ExtractionJob job);
    /*
     * 功能：加载任务及其全部解析步骤。
     * 参数：
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     * 返回：成功值为任务及其完整步骤。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> loadExtractionJob(const std::string& job_id);
    /*
     * 功能：在同一只读快照中读取任务计数和索引停止标志，不读取任何历史步骤正文。
     * 参数：
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     * 返回：成功值为任务轻量检查点及计数。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> loadExtractionJobState(const std::string& job_id);
    /*
     * 功能：限量查询最早的待执行步骤，不携带历史输出或错误详情；没有待执行步骤时返回空值。
     * 参数：
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     * 返回：成功值为单步定位/尝试状态可选值；没有待执行步骤时返回成功空值。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::optional<xuyan::domain::ExtractionStep>> nextExtractionStep(const std::string& job_id);
    /*
     * 功能：按任务/序号读取单步定位元数据，不查询 output_json 或 error_message。
     * 参数：
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   ordinal：从1开始的步骤序号，仅定位当前任务内的一个步骤。
     * 返回：成功值为单步定位/尝试状态。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> loadExtractionStepMetadata(const std::string& job_id, int ordinal);
    /*
     * 功能：列出当前工作区的解析任务。
     * 参数：无。
     * 返回：成功值为含完整步骤的任务列表；没有任务时成功返回空列表。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listExtractionJobs();
    /*
     * 功能：仅列出指定世界来源对应的解析任务及步骤。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     * 返回：成功值为任务及其完整步骤列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> listExtractionJobsForWorld(
        const std::string& world_id);
    /*
     * 功能：按预期修订请求取消解析任务。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为任务及其完整步骤。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订请求取消解析任务；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> cancelExtractionJob(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：原子取消并只返回同事务检查点，供大任务调度避免加载全部步骤。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为任务轻量检查点及计数。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：原子取消并只返回同事务检查点，供大任务调度避免加载全部步骤；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> cancelExtractionJobState(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：以事务认领下一待执行步骤并递增尝试次数。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为单步定位/尝试状态。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：以事务认领下一待执行步骤并递增尝试次数；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionStep> claimExtractionStep(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /*
     * 功能：按预期尝试次数写入步骤终态与结果。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   ordinal：从1开始的步骤序号，仅定位当前任务内的一个步骤。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   terminal_status：步骤终态协议值；由结算接口校验，不允许以任意字符串伪造完成。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   error_message：失败详情；成功结算通常为空，不能写入凭据或私有模型原始响应。
     * 返回：成功值为任务及其完整步骤。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期尝试次数写入步骤终态与结果；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> finishExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /*
     * 功能：使用与完整结果相同的幂等事务结算单步，但仅返回任务检查点。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   ordinal：从1开始的步骤序号，仅定位当前任务内的一个步骤。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   terminal_status：步骤终态协议值；由结算接口校验，不允许以任意字符串伪造完成。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   error_message：失败详情；成功结算通常为空，不能写入凭据或私有模型原始响应。
     * 返回：成功值为任务轻量检查点及计数。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：使用与完整结果相同的幂等事务结算单步，但仅返回任务检查点；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> finishExtractionStepState(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /*
     * 功能：将允许重试的步骤恢复为待执行状态。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   ordinal：从1开始的步骤序号，仅定位当前任务内的一个步骤。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     * 返回：成功值为任务及其完整步骤。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将允许重试的步骤恢复为待执行状态；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> retryExtractionStep(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt);
    /*
     * 功能：启动后恢复上次进程中断时遗留的运行中步骤。
     * 参数：无。
     * 返回：成功值为本次恢复的记录数；没有中断记录时为0。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：启动后恢复上次进程中断时遗留的运行中步骤；不执行网络调用。
     */
    xuyan::domain::Result<int> recoverInterruptedExtractionSteps();
    /*
     * 功能：在单一事务中提交步骤输出与经证据校验的候选。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   step_ordinal：当前任务中的从1开始的步骤序号，与提交候选的证据范围对应。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   candidates：经过证据定位校验的候选集合；按值移入，和步骤终态在同一事务提交。
     * 返回：成功值为提交后含完整步骤的单个任务；步骤冲突不是成功空列表。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：在单一事务中提交步骤输出与经证据校验的候选；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> commitExtractionCandidates(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /*
     * 功能：原子提交同一批候选并返回检查点，不读取前序步骤输出。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   step_ordinal：当前任务中的从1开始的步骤序号，与提交候选的证据范围对应。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   candidates：经过证据定位校验的候选集合；按值移入，和步骤终态在同一事务提交。
     * 返回：成功值为提交后的单个任务轻量检查点及计数；步骤冲突不是成功空列表。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：原子提交同一批候选并返回检查点，不读取前序步骤输出；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> commitExtractionCandidatesState(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /*
     * 功能：按审核状态列出抽取候选。
     * 参数：
     *   review_status：审核状态协议值；列表接口按该值过滤，不把它直接当成显示标签。
     * 返回：成功值为候选及审核元数据列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidates(
        const std::string& review_status);
    /*
     * 功能：在 SQLite 中按世界、可选来源和审核状态过滤并限量读取一页候选。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   source_id：小说来源稳定标识；分页候选接口允许空串表示当前世界全部来源。
     *   review_status：审核状态协议值；列表接口按该值过滤，不把它直接当成显示标签。
     *   limit：本页最多条目数，范围1—200；不是整库加载上限。
     *   offset：从零开始的条目偏移，单位是条目而非字节；分页接口校验非负范围。
     * 返回：成功值为候选页和匹配总数。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidatePage> listExtractionCandidatesPage(
        const std::string& world_id, const std::string& source_id, const std::string& review_status,
        int limit, std::int64_t offset);
    /*
     * 功能：在同一只读快照中精确匹配当前世界已确认端点，排除事件、规则、说法和模型假设。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   mention：模型提及的逐字名称或别名；只提供精确匹配建议，不自动选目标。
     *   limit：本页最多身份建议数，范围1—50；不自动选择匹配对象。
     *   offset：从零开始的条目偏移，单位是条目而非字节；分页接口校验非负范围。
     * 返回：成功值为明确端点的精确匹配页。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::RelationEndpointMatchPage> matchRelationEndpoints(
        const std::string& world_id, const std::string& mention, int limit, std::int64_t offset);
    /*
     * 功能：在同一只读快照中按候选名称和别名匹配同世界同类型实体，保留全部同名结果。
     * 参数：
     *   candidate_id：待审候选稳定标识；审核与身份建议以其当前修订为准。
     *   expected_candidate_revision：调用者读取的候选修订；过期、已结束审核等情形不能静默继续写入。
     *   limit：本页最多身份建议数，范围1—50；不自动选择匹配对象。
     *   offset：从零开始的条目偏移，单位是条目而非字节；分页接口校验非负范围。
     * 返回：成功值为候选身份精确匹配页。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::CandidateEntityMatchPage> matchCandidateEntities(
        const std::string& candidate_id, int expected_candidate_revision, int limit, std::int64_t offset);
    /*
     * 功能：限量读取某解析任务产生的候选。
     * 参数：
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   limit：最多返回的任务候选数，范围1—1000；不载入超过上限的候选。
     * 返回：成功值为候选及审核元数据列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> listExtractionCandidatesForJob(
        const std::string& job_id, int limit);
    /*
     * 功能：读取单个抽取候选及其原文证据元数据。
     * 参数：
     *   candidate_id：待审候选稳定标识；审核与身份建议以其当前修订为准。
     * 返回：成功值为候选及审核元数据。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> loadExtractionCandidate(
        const std::string& candidate_id);
    /*
     * 功能：在单一事务中校验明确目标及两侧修订，保存候选接受映射、证据、别名修订和命令日志。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   candidate_id：待审候选稳定标识；审核与身份建议以其当前修订为准。
     *   expected_candidate_revision：调用者读取的候选修订；过期、已结束审核等情形不能静默继续写入。
     *   selection：作者明确选择的目标实体标识和目标修订；必须同世界、同类型且满足来源条件。
     *   provenance_type：接受结果的来源性质协议值；区分作者设定、原文事实、说法和推断。
     * 返回：成功值为候选及审核元数据。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：在单一事务中校验明确目标及两侧修订，保存候选接受映射、证据、别名修订和命令日志；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> acceptCandidateIntoEntity(
        const std::string& command_id, const std::string& candidate_id, int expected_candidate_revision,
        const xuyan::domain::CandidateEntitySelection& selection, const std::string& provenance_type);
    /*
     * 功能：原子保存审核、证据及专用投影；关系端点在事务内按明确ID和预期修订校验。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   candidate：待写入的候选审核状态及字段；原始引文和来源保留，不用审核结果覆盖原文。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   accepted_entity：接受时的通用资料投影；空值表示没有该投影，不能凭空创建样例。
     *   accepted_timeline：可选时间线专用投影，默认无；存在时与审核和证据原子保存。
     *   accepted_graph：可选地点/关系专用投影，默认无；关系端点仍需显式标识和修订。
     * 返回：成功值为审核后的单个候选及新修订；幂等重放返回原命令结果，不返回可选实体。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：原子保存审核、证据及专用投影；关系端点在事务内按明确ID和预期修订校验；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> reviewExtractionCandidate(
        const std::string& command_id, xuyan::domain::ExtractionCandidate candidate, int expected_revision,
        std::optional<xuyan::domain::WorldEntity> accepted_entity,
        std::optional<xuyan::domain::TimelineEvent> accepted_timeline = std::nullopt,
        std::optional<xuyan::domain::CandidateGraphProjection> accepted_graph = std::nullopt);
    /*
     * 功能：按修订保存实体的时间与人物可见范围。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   scope：实体的时间和角色可见范围值，按值写入；不能扩大为无条件全世界可见。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为实体可见范围。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按修订保存实体的时间与人物可见范围；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::EntityRetrievalScope> saveEntityRetrievalScope(
        const std::string& command_id, xuyan::domain::EntityRetrievalScope scope, int expected_revision);
    /*
     * 功能：在请求者可见范围内检索世界实体。
     * 参数：
     *   request：检索关键词、世界、故事时间、请求者权限及结果上限；按值用于过滤。
     * 返回：成功值为权限过滤后的检索命中列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::RetrievalHit>> retrieveEntities(
        xuyan::domain::RetrievalRequest request);
    /*
     * 功能：将指定世界当前已审核资料发布为不可变版本。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   parent_id：父世界版本标识；空值用于没有父版本的首次发布。
     * 返回：成功值为不可变世界版本。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将指定世界当前已审核资料发布为不可变版本；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> publishWorldVersion(
        const std::string& command_id, const std::string& world_id, const std::string& parent_id);
    /*
     * 功能：列出指定世界的已发布版本。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     * 返回：成功值为不可变世界版本列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::WorldVersion>> listWorldVersions(const std::string& world_id);
    /*
     * 功能：按标识读取不可变世界版本。
     * 参数：
     *   version_id：不可变世界版本稳定标识。
     * 返回：成功值为不可变世界版本。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::WorldVersion> loadWorldVersion(const std::string& version_id);
    /*
     * 功能：按故事时间创建世界版本的历史可见快照。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   world_version_id：已发布世界版本标识；历史快照或人物实例以此为固定基线。
     *   story_time：故事内的整数时间坐标，单位由世界时间体系定义，不能用现实UTC或章节序号冒充。
     * 返回：成功值为故事时间下的世界快照。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按故事时间创建世界版本的历史可见快照；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::HistoricalSnapshot> createHistoricalSnapshot(
        const std::string& command_id, const std::string& world_version_id, std::int64_t story_time);
    /*
     * 功能：按预期修订保存时间线事件。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   event：时间线事件值；已知故事时间与未知时间分开，叙事顺序独立保存。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为时间线事件。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订保存时间线事件；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::TimelineEvent> saveTimelineEvent(
        const std::string& command_id, xuyan::domain::TimelineEvent event, int expected_revision);
    /*
     * 功能：按叙事顺序或故事时间列出时间线事件。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   narrative_order：true按叙事顺序读取；false按故事时间读取。
     *   maximum_story_time：可选故事时间上界；无值表示不设置上界，不把未知时间改成0。
     * 返回：成功值为时间线事件列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::TimelineEvent>> listTimelineEvents(
        const std::string& world_id, bool narrative_order, std::optional<std::int64_t> maximum_story_time);
    /*
     * 功能：按预期修订保存实体间有向关系。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   relation：有向实体关系值；端点、维度、强度、证据状态和权限保留各自语义。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值为有向关系。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订保存实体间有向关系；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::DirectedRelation> saveDirectedRelation(
        const std::string& command_id, xuyan::domain::DirectedRelation relation, int expected_revision);
    /*
     * 功能：按故事时间与请求者权限读取实体关系。
     * 参数：
     *   world_id：世界稳定标识；用于限定查询或发布范围，避免跨世界串数据。
     *   entity_id：世界实体稳定标识；按标识定位，不按名称自动选同名资料。
     *   story_time：故事内的整数时间坐标，单位由世界时间体系定义，不能用现实UTC或章节序号冒充。
     *   actor_id：请求者或执行者人物稳定标识；用于角色权限校验。
     *   author_view：true请求作者视角；false按请求者人物可见范围过滤。
     * 返回：成功值为有向关系列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::DirectedRelation>> listDirectedRelations(
        const std::string& world_id, const std::string& entity_id, std::optional<std::int64_t> story_time,
        const std::string& actor_id, bool author_view);
    /*
     * 功能：在单一写事务内按预期修订保存地点、真实性及命令，拒绝跨世界或无法安全确认的父级链。
     * 参数：
     *   command_id：1—512字节稳定命令标识，借用至返回；新摘要绑定全部规范化字段及预期修订。
     *   placement：拥有的地点标注副本；当前实体须未删除且分类为地点；父级为空表示根，所有非空祖先须已标注、同世界且有效。
     *     坐标成对非负或同时未知；底图引用最多1024字节，真实性与证据性质相容；不验证图像存在或坐标上界。
     *   expected_revision：标注自身修订0—整型最大值减1；创建为0，更新必须等于当前值，返回对象原revision被重算。
     * 返回：自有规范化标注、修订加1；同负载重放只读现存标注，可能反映后续编辑，兼容旧摘要但不重新写入。
     * 失败：无效命令/字段/跨世界为validation_failed，失效节点为missing_context，环/未在128个祖先内到根为rule_conflict；
     *   过期修订/不同命令负载分别为revision_conflict/command_conflict；事务内异常为中文storage_error，详情隐藏。
     *   事务前值校验及摘要分配异常可传播。未提交的标注、语义及日志全部回滚。
     * 副作用：仅本连接同线程同步事务写入，不联网、不改变作者实体、历史世界版本或底图文件。
     */
    xuyan::domain::Result<xuyan::domain::LocationPlacement> saveLocationPlacement(
        const std::string& command_id, xuyan::domain::LocationPlacement placement, int expected_revision);
    /*
     * 功能：在单一写事务内按预期修订保存同世界有效地点之间的路线及幂等命令。
     * 参数：
     *   command_id：1—512字节稳定命令标识，借用；新摘要绑定全部字段与expected_revision，不采用分隔符拼接身份。
     *   route：拥有的路线副本，id非空，两端不同且为同世界未删除的当前地点，并均已标注；旧路线ID不能移往另一世界。
     *     travel_minutes为空表示未知，否则为正整数分钟；方向及evidence/assumption证据性质由调用方明确输入。
     *   expected_revision：路线自身修订0—整型最大值减1，首次创建为0，更新必须与当前值一致，不是端点修订。
     * 返回：自有规范化路线、修订加1；重放读取现存路线，不再写入或复活端点；旧摘要未绑定expected_revision，仅保留其只读语义。
     * 失败：无效字段/命令或跨世界为validation_failed，失效/未标注端点为missing_context；过期修订/不同命令为对应冲突。
     *   事务内异常为中文storage_error，详情隐藏，路线及日志一起回滚；事务前字段校验/摘要分配异常可传播。
     * 副作用：仅本连接同线程同步写入，不联网、不修改端点实体、标注或已发布版本；双向标志不生成反向副本。
     */
    xuyan::domain::Result<xuyan::domain::TravelRoute> saveTravelRoute(
        const std::string& command_id, xuyan::domain::TravelRoute route, int expected_revision);
    /*
     * 功能：同一读快照内读取指定世界有效地点及其路线，校验历史父级链但不自动修补。
     * 参数：
     *   world_id：借用的世界稳定标识，精确匹配，空/未建世界通常返回空集合，不回退首世界。
     * 返回：自有地点和路线，分别按身份升序；地点含未删除当前地点及有类型化接受映射、当前仍声明地点的other类说法/假设投影，保留真实性。
     *   作者已改类的节点不显示；路线两端须为同世界有效当前location类型标注，未确认投影不自动成为端点；无匹配为成功空集合。
     * 失败：父级失效/跨世界/环/超128层分别返回校验错误，修订头缺失/删除状态不一致为missing_context，禁止返回部分成功地图。
     *   查询、专用语义读取及分配异常为中文storage_error，详情隐藏，不把失败当空列表。
     * 副作用：同线程只读本连接快照，不删隐藏的历史路线、不修改父级或联网；未分页，最多每地点检查128个祖先，未做规模门禁。
     */
    xuyan::domain::Result<xuyan::domain::MapView> loadMapView(const std::string& world_id);
    /*
     * 功能：创建绑定世界版本的人物实例。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   instance：绑定人物卡与世界版本的人物实例值；不生成预置人物。
     * 返回：成功值为人物实例。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：创建绑定世界版本的人物实例；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> createCharacterInstance(
        const std::string& command_id, xuyan::domain::CharacterInstance instance);
    /*
     * 功能：读取指定人物实例的当前状态。
     * 参数：
     *   instance_id：人物实例稳定标识。
     * 返回：成功值为人物实例。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> loadCharacterInstance(const std::string& instance_id);
    /*
     * 功能：列出某世界版本绑定的人物实例。
     * 参数：
     *   world_version_id：已发布世界版本标识；历史快照或人物实例以此为固定基线。
     * 返回：成功值为人物实例列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterInstance>> listCharacterInstances(
        const std::string& world_version_id);
    /*
     * 功能：按预期修订保存人物实例的记忆数据。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   instance_id：人物实例稳定标识。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   memory_json：人物实例记忆JSON正文；输入是数据，写入须核对预期修订。
     * 返回：成功值为人物实例。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按预期修订保存人物实例的记忆数据；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::CharacterInstance> saveCharacterInstanceMemory(
        const std::string& command_id, const std::string& instance_id, int expected_revision,
        const std::string& memory_json);
    /*
     * 功能：将分支根提交绑定到世界版本及人物实例集合。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   binding：分支根与世界版本、历史快照和人物实例的固定绑定值。
     * 返回：成功值为分支根绑定。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将分支根提交绑定到世界版本及人物实例集合；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> bindBranchRoot(
        const std::string& command_id, xuyan::domain::BranchRootBinding binding);
    /*
     * 功能：读取指定分支的根绑定。
     * 参数：
     *   branch_id：分支稳定标识；只定位该分支，不按显示名称猜测。
     * 返回：成功值为分支根绑定。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::BranchRootBinding> loadBranchRootBinding(const std::string& branch_id);
    /*
     * 功能：创建持久化模拟会话与额度配置。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   session：推演会话与调用额度配置值，创建本身不发起模型请求。
     * 返回：成功值为会话及回合状态。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：创建持久化模拟会话与额度配置；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> createSimulationSession(
        const std::string& command_id, xuyan::domain::SimulationSession session);
    /*
     * 功能：读取指定模拟会话及其回合。
     * 参数：
     *   session_id：持久化推演会话稳定标识。
     * 返回：成功值为会话及回合状态。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> loadSimulationSession(const std::string& session_id);
    /*
     * 功能：列出当前工作区的模拟会话。
     * 参数：无。
     * 返回：成功值为会话及回合状态列表；无匹配记录时成功列表为空。
     * 失败：数据库查询或记录格式失败通过Result.error返回；必须存在的记录缺失返回错误，正常空列表/空可选值与查询失败分别处理。
     * 副作用：只读取当前连接的数据，不改资料、不联网；返回值独立拥有内容。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::SimulationSession>> listSimulationSessions();
    /*
     * 功能：预留一个模拟回合及模型调用额度。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   session_id：持久化推演会话稳定标识。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   actor_id：请求者或执行者人物稳定标识；用于角色权限校验。
     *   request_hash：回合模型请求的冻结摘要；用于核对调用预留身份。
     * 返回：成功值为回合及叙事记录。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：预留一个模拟回合及模型调用额度；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationTurn> reserveSimulationTurn(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& actor_id, const std::string& request_hash);
    /*
     * 功能：将人物意图和新状态作为回合结果原子提交。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   turn_id：已经预留的推演回合稳定标识。
     *   expected_session_revision：回合提交时预期会话修订；不能用步骤尝试次数替代。
     *   intent：经过校验的人物或导演意图值；角色权限与状态提交继续由事务接口核对。
     *   next_state：准备提交的下一状态值；由调用者显式提供，不用模型文本直接替代。
     *   draft_narration：待审叙事文本；与已提交状态分开记录。
     *   input_tokens：本次实际模型输入词元数，单位为词元，不能用字数估算冒充。
     *   output_tokens：本次实际模型输出词元数，单位为词元，用于持久化用量。
     * 返回：成功值为会话及回合状态。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将人物意图和新状态作为回合结果原子提交；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitSimulationTurn(
        const std::string& command_id, const std::string& turn_id, int expected_session_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state,
        const std::string& draft_narration, int input_tokens, int output_tokens);
    /*
     * 功能：将已提交回合的最终叙述写入记录。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   turn_id：已经预留的推演回合稳定标识。
     *   final_narration：已提交回合的最终叙事文本；不改变原回合状态快照。
     * 返回：成功值为回合及叙事记录。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将已提交回合的最终叙述写入记录；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationTurn> finishSimulationNarration(
        const std::string& command_id, const std::string& turn_id, const std::string& final_narration);
    /*
     * 功能：按修订和操作类型暂停、继续或取消模拟会话。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   session_id：持久化推演会话稳定标识。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   action：会话控制协议值，表示暂停、继续或取消；不在此处直接运行模型。
     * 返回：成功值为会话及回合状态。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：按修订和操作类型暂停、继续或取消模拟会话；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> controlSimulationSession(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        const std::string& action);
    /*
     * 功能：将导演干预作为受审计的会话回合提交。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   session_id：持久化推演会话稳定标识。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     *   intent：经过校验的人物或导演意图值；角色权限与状态提交继续由事务接口核对。
     *   next_state：准备提交的下一状态值；由调用者显式提供，不用模型文本直接替代。
     * 返回：成功值为会话及回合状态。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：将导演干预作为受审计的会话回合提交；不执行网络调用。
     */
    xuyan::domain::Result<xuyan::domain::SimulationSession> commitDirectorIntervention(
        const std::string& command_id, const std::string& session_id, int expected_revision,
        xuyan::domain::ActorIntent intent, xuyan::domain::ScenarioState next_state);
    /*
     * 功能：启动后恢复被进程中断的模拟会话与调用预留。
     * 参数：无。
     * 返回：成功值为本次恢复的记录数；没有中断记录时为0。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。
     * 副作用：在当前数据库执行：启动后恢复被进程中断的模拟会话与调用预留；不执行网络调用。
     */
    xuyan::domain::Result<int> recoverInterruptedSimulationSessions();

private:
    /*
     * 功能：共用候选提交事务；结果类型仅决定返回完整快照或轻量检查点。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   step_ordinal：当前任务中的从1开始的步骤序号，与提交候选的证据范围对应。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   candidates：经过证据定位校验的候选集合；按值移入，和步骤终态在同一事务提交。
     * 返回：成功值由JobResult模板决定：完整任务或轻量检查点；两种返回方式使用同一写事务。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：共用候选提交事务；结果类型仅决定返回完整快照或轻量检查点；不执行网络调用。
     */
    template<class JobResult> xuyan::domain::Result<JobResult> commitExtractionCandidatesImpl(
        const std::string& command_id, const std::string& job_id, int step_ordinal, int expected_attempt,
        const std::string& output_json, std::vector<xuyan::domain::ExtractionCandidate> candidates);
    /*
     * 功能：共用步骤结算事务，保证两种结果接口的幂等和修订语义一致。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   ordinal：从1开始的步骤序号，仅定位当前任务内的一个步骤。
     *   expected_attempt：调用者领取步骤时的尝试次数；写终态前必须仍相同，避免迟到结果覆盖重试。
     *   terminal_status：步骤终态协议值；由结算接口校验，不允许以任意字符串伪造完成。
     *   output_json：已校验的步骤输出JSON文本；存储结果，不把它执行为SQL或程序。
     *   error_message：失败详情；成功结算通常为空，不能写入凭据或私有模型原始响应。
     * 返回：成功值由JobResult模板决定：完整任务或轻量检查点；两种返回方式使用同一写事务。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：共用步骤结算事务，保证两种结果接口的幂等和修订语义一致；不执行网络调用。
     */
    template<class JobResult> xuyan::domain::Result<JobResult> finishExtractionStepImpl(
        const std::string& command_id, const std::string& job_id, int ordinal, int expected_attempt,
        const std::string& terminal_status, const std::string& output_json, const std::string& error_message);
    /*
     * 功能：共用取消事务，返回结果的范围不改变取消行为或命令日志。
     * 参数：
     *   command_id：本次写入的稳定命令标识；用于幂等重放，同一标识不得绑定不同请求。
     *   job_id：解析任务稳定标识；任务自身绑定小说来源与世界。
     *   expected_revision：调用者读取的当前修订；创建接口采用首版约定，更新必须与数据库一致。
     * 返回：成功值由JobResult模板决定：完整任务或轻量检查点；两种返回方式使用同一写事务。
     * 失败：输入、业务不变量或存储失败通过Result.error返回，未提交写入由事务回滚。预期修订/版本/尝试与当前值不符时拒绝覆盖。幂等命令身份冲突拒绝重放。
     * 副作用：在当前数据库执行：共用取消事务，返回结果的范围不改变取消行为或命令日志；不执行网络调用。
     */
    template<class JobResult> xuyan::domain::Result<JobResult> cancelExtractionJobImpl(
        const std::string& command_id, const std::string& job_id, int expected_revision);
    /* 本实例独占的 SQLite 连接；初始为空，构造成功后有效，析构关闭；不跨线程共享。 */
    sqlite3* database_{nullptr};

    /*
     * 功能：在构造阶段创建或升级支持的工作区结构；旧格式仅按明确迁移规则处理。
     * 参数：无，使用本实例刚打开的独占连接。
     * 返回：无；成功后连接可用于版本 30 的读写。
     * 失败：未知/损坏结构或 SQL 失败抛异常；写入事务回滚，构造者关闭连接。
     * 副作用：可能创建表和索引、升级已规定的历史字段；不推断或补写作者内容，不联网。
     */
    void migrate();
};

} // namespace xuyan::storage
