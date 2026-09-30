#pragma once

#include "xuyan/domain/extraction_job.h"
#include "xuyan/application/mock_extraction_processor.h"

#include <QObject>
#include <QVariantList>
#include <QThreadPool>
#include <QHash>
#include <QSet>

#include <filesystem>
#include <cstdint>
#include <memory>
#include <functional>

/*
 * 职责：管理持久化解析任务及串行离线/远程批次，展示检查点进度；真实发送必须由显式操作触发。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：析构请求停止后续片并等待自有批次池；全局池元数据任务持值，销毁后放弃通知，已提交结果保留。
 */
class ExtractionJobViewModel final : public QObject {
    Q_OBJECT
    /* 属性：读取当前世界任务列表；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    /* 属性：返回任务服务是否正在执行操作。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：返回是否有全书批次或单步抽样在后台运行。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool running READ running NOTIFY changed)
    /* 属性：读取正在运行的任务标识；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString activeJobId READ activeJobId NOTIFY changed)
    /* 属性：查询是否请求暂停或取消；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool stopping READ stopping NOTIFY changed)
    /* 属性：返回最近一次任务操作的中文错误。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：返回最近一次任务操作的状态说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:

    /*
     * 端口契约：在线程池同步执行；path 是本次库路径借用，job_id 是非空稳定 ID，
     * options 是步骤上限、停止令牌及同步进度回调借用；引用不得在返回后保存。
     * 返回 OfflineBatchResult 或领域错误；异常由 launchBatch 捕获。可写候选/检查点，
     * 远程可发请求且可能计费；暂停/取消在片间生效，禁止另起逃逸线程或保留进度回调。
     */
    using BatchRunner = std::function<xuyan::domain::Result<xuyan::application::OfflineBatchResult>(
        const std::filesystem::path&, const std::string&, const xuyan::application::OfflineBatchOptions&)>;

    /*
     * 功能：绑定任务库、自有串行池及可替换批次端口，恢复中断状态
     * 参数：database_path：输入，本机任务库路径，按值持有；parent：输入，可空 Qt 所有者，默认空；runner：输入，按值拥有同步批次端口，默认空采用正式处理器，须遵守 BatchRunner 契约。
     * 返回：完成初始化，开始后台恢复中断领取。
     * 失败：初始化异常传播；恢复失败写中文 errorText。
     * 副作用：设置自有池最多1线程；恢复旧运行中片为需人工处理状态，不自动重发。
     * 线程与生命周期：GUI 构造；恢复在线程池，GUI 回填；批次由自有池执行，析构等待。
     */
    explicit ExtractionJobViewModel(std::filesystem::path database_path, QObject* parent = nullptr,
                                    BatchRunner runner = {});

    /*
     * 功能：停止后续片段并等待自有工作线程退出
     * 参数：无。
     * 返回：无；释放批次控制、路径和界面快照。
     * 失败：不抛业务异常；取消落盘失败保留原检查点，不能保证最后取消持久化成功。
     * 副作用：请求 stop，等待当前片结束；此前显式取消尝试补落盘，不新发模型请求。
     * 线程与生命周期：GUI 析构可能等待在途请求；成员释放前 waitForDone，已排队 GUI 回调由 QObject 生命周期清理。
     */
    ~ExtractionJobViewModel() override;

    /*
     * 功能：读取当前世界任务列表
     * 参数：无。
     * 返回：列表副本；包含步骤和预算原协议值、中文状态标签及已提交进度，未选世界为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList jobs() const { return jobs_; }

    /*
     * 功能：返回任务服务是否正在执行操作。
     * 参数：无。
     * 返回：返回任务服务是否正在执行操作。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }

    /*
     * 功能：返回是否有全书批次或单步抽样在后台运行。
     * 参数：无。
     * 返回：返回是否有全书批次或单步抽样在后台运行。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool running() const noexcept { return running_; }

    /*
     * 功能：读取正在运行的任务标识
     * 参数：无。
     * 返回：无运行为空；切世界不改变实际运行目标。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString activeJobId() const { return active_job_id_; }

    /*
     * 功能：查询是否请求暂停或取消
     * 参数：无。
     * 返回：控制存在且原子动作非 proceed 为true；不表示在途网络请求已中断。
     * 失败：不抛异常。
     * 副作用：只读原子调度意图。
     * 线程与生命周期：GUI 同步调用，控制共享所有权保持原子对象有效。
     */
    bool stopping() const noexcept;

    /*
     * 功能：返回最近一次任务操作的中文错误。
     * 参数：无。
     * 返回：返回最近一次任务操作的中文错误。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：返回最近一次任务操作的状态说明。
     * 参数：无。
     * 返回：返回最近一次任务操作的状态说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }

    /*
     * 功能：切世界立即隐藏旧任务，保留实际在途批次
     * 参数：world_id：输入，稳定世界 ID，可空表示取消选择。
     * 返回：无。
     * 失败：同值忽略；新查询失败显示错误。
     * 副作用：递增世界代次，清列表和提示；忙碌查询完成后补读新世界。
     * 线程与生命周期：GUI 修改选择，批次继续原目标；进度和结束提示仅对所属世界回显。
     */
    void setWorldId(QString world_id);

    /*
     * 功能：后台读取当前世界持久化任务
     * 参数：无。
     * 返回：无。
     * 失败：无世界清列表；busy 时标记补刷；服务失败写 errorText；必要时保留刚结束批次错误。
     * 副作用：设置 busy，读任务并覆盖已提交进度，发 changed，不发送请求。
     * 线程与生命周期：线程池独立服务读库，GUI 比对世界 ID/代次，旧结果仅释放 busy并补刷。
     */
    Q_INVOKABLE void refresh();

    /*
     * 功能：校验来源世界并冻结切片、请求上限及模型连接
     * 参数：source_id：输入，非空当前世界来源 ID；chunk_size：输入，500—50000 Unicode 码点；overlap：输入，非负码点且小于片长一半；max_requests：输入，非负调用硬上限，0的自动估算语义由服务决定；output_token_limit：输入，每请求输出词元上限1—1000000；provider_connection_id：输入，空为离线，非空为冻结远程连接。
     * 返回：无。
     * 失败：busy/无世界/空来源忽略；范围或归属/服务失败写 errorText。
     * 副作用：后台生成步骤与冻结任务参数，不执行模型；GUI 重读列表并报告可恢复片段数。
     * 线程与生命周期：GUI 捕获值及世界代次，线程池读来源后创建；GUI 隔离旧世界结果，不撤销已建任务。
     */
    Q_INVOKABLE void createJob(QString source_id, int chunk_size, int overlap, int max_requests,
                              int output_token_limit, QString provider_connection_id);

    /*
     * 功能：请求取消剩余片段并保留已提交候选
     * 参数：job_id：输入，当前世界可见任务 ID；revision：输入，未运行任务的预期修订；在途批次使用检查点实际修订结算。
     * 返回：无。
     * 失败：非可见、busy 或无世界忽略；持久化修订/存储失败提示。
     * 副作用：当前批次置原子 cancel，当前片提交后取消；其他任务后台按修订取消并刷新。
     * 线程与生命周期：GUI 设置原子意图；工作线程在检查点结算，迟到取消由 finishBatch/析构补结算；不保证在途退款。
     */
    Q_INVOKABLE void cancelJob(QString job_id, int revision);

    /*
     * 功能：显式重新排队一个失败或结果未知的步骤
     * 参数：job_id：输入，可见任务 ID；ordinal：输入，从1开始的步骤序号；attempt：输入，当前预期尝试计数，由服务校验。
     * 返回：无。
     * 失败：busy、该任务运行中、无世界、非可见或序号非正忽略；状态/尝试冲突失败提示。
     * 副作用：后台重排步骤，不重跑已完成片、不恢复已消耗预算；重排本身不发送。
     * 线程与生命周期：线程池写库，GUI 按世界代次回填；后续执行必须再次显式触发。
     */
    Q_INVOKABLE void retryStep(QString job_id, int ordinal, int attempt);

    /*
     * 功能：显式启动离线任务剩余步骤
     * 参数：job_id：输入，当前世界可见且未绑定模型的任务 ID。
     * 返回：无。
     * 失败：非离线任务不执行；其他启动限制由 launchBatch 检查。
     * 副作用：调用同一生命周期受控批次，最多100000步，不联网。
     * 线程与生命周期：GUI 发起，自有串行池执行，GUI 按批次会话应用结果。
     */
    Q_INVOKABLE void runMock(QString job_id);

    /*
     * 功能：用户明确触发至多一个远程片段
     * 参数：job_id：输入，可见、已冻结模型连接且可排队任务 ID。
     * 返回：无。
     * 失败：启动限制或服务失败经批次结果通知；未知结果不自动重试。
     * 副作用：调用 launchBatch 上限1；可能计费，候选保持待审。
     * 线程与生命周期：自有池执行在途请求，GUI 回填；调用方须先获得用户明确确认。
     */
    Q_INVOKABLE void runRemoteSample(QString job_id);

    /*
     * 功能：显式解析任务全部剩余排队片段
     * 参数：job_id：输入，可见且 queued 的任务 ID；远程任务要求界面事先确认。
     * 返回：无。
     * 失败：取消中、busy、已运行或归属/连接改变不执行；失败/未知停止，不自动重试。
     * 副作用：调用 launchBatch 上限100000；离线不联网，远程可能计费，每片独立检查点。
     * 线程与生命周期：GUI 发起，自有串行池执行；退出停止后续片并等待当前片结束。
     */
    Q_INVOKABLE void startJob(QString job_id);

    /*
     * 功能：请求当前批次在检查点暂停
     * 参数：job_id：输入，必须等于当前运行任务且属于选中世界。
     * 返回：无。
     * 失败：目标不符忽略；已请求取消不得降级为暂停。
     * 副作用：原子 proceed→pause，更新中文等待提示，不中断已发请求。
     * 线程与生命周期：GUI 修改原子意图，工作线程同步检查点回调读取。
     */
    Q_INVOKABLE void pauseJob(QString job_id);

    /*
     * 功能：从原持久化检查点继续剩余片段
     * 参数：job_id：输入，可见排队任务 ID；远程继续仍要求显式确认。
     * 返回：无。
     * 失败：与 startJob 一致；失败/未知须先人工处理，不自动重发。
     * 副作用：复用任务冻结配置、预算及完成步骤，不重置已用次数。
     * 线程与生命周期：GUI 发起，自有池继续，GUI 应用会话结果。
     */
    Q_INVOKABLE void resumeJob(QString job_id);

    /*
     * 功能：本地抽样复核候选原文证据和审核计数
     * 参数：job_id：输入，当前世界可见任务 ID。
     * 返回：无。
     * 失败：busy/无世界/不可见忽略；服务失败提示。
     * 副作用：最多100项只读复核，不发送；显示证据定位及审核计数，不冒称精确率/召回率。
     * 线程与生命周期：线程池只读查询，GUI 比对世界代次回填。
     */
    Q_INVOKABLE void auditJob(QString job_id);
signals:
    /*
     * 功能：通知任务列表、运行/停止状态及提示变化
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收方结果。
     * 副作用：调用 Qt 连接槽，不额外调度片段。
     * 线程与生命周期：GUI 发出；后台只通过排队回调更新界面。
     */
    void changed();
private:

    friend struct ExtractionJobViewModelTestAccess;
    /* 一次批次的跨线程共享控制，定义在实现中；只共享原子动作和停止源，不共享 GUI 或连接。 */
    struct RunControl;

    /*
     * 功能：启动一次受控串行批次并隔离上下文变化
     * 参数：job_id：输入，可见 queued 任务；maximum_steps：输入，1—100000步骤上限；require_remote：输入，true 要求已绑定模型，false 沿冻结连接自动选择离线/远程。
     * 返回：无。
     * 失败：busy/已运行/无世界/不可见/非queued/取消中忽略；归属或连接改变返回失败且不发送；处理异常显示中文错误。
     * 副作用：持共享控制，设运行状态；每片同步进度排队GUI、取消结算、最终刷新。
     * 线程与生命周期：自有池同步执行 runner，不访问 GUI 状态；this 由析构 waitForDone 保活，GUI 按 run_generation_ 和世界应用。
     */
    void launchBatch(QString job_id, int maximum_steps, bool require_remote);

    /*
     * 功能：应用批次结束并补结算末尾迟到取消
     * 参数：result：输入，拥有批次结果或错误；session：输入，启动会话代次；job_id：输入，运行任务 ID；operation_world：输入，原目标世界 ID；cancellation_settled：输入，默认false，true 表示已做最后补取消，不再补排。
     * 返回：无。
     * 失败：会话过期忽略；取消补保存失败保留检查点并显示安全中文错误。
     * 副作用：必要时再次投递自有池结算；终结清运行控制，保存暂停标志，刷新当前世界。
     * 线程与生命周期：仅 GUI 调用；补取消仍保持 running防重入，自有池执行，GUI 同一会话回填，析构等待。
     */
    void finishBatch(xuyan::domain::Result<xuyan::application::OfflineBatchResult> result,
                     std::uint64_t session, QString job_id, QString operation_world,
                     bool cancellation_settled = false);

    /*
     * 功能：合并最近已提交进度到当前任务卡
     * 参数：无。
     * 返回：无。
     * 失败：无业务失败，容器分配异常传播。
     * 副作用：仅更新 jobs_；较旧修订列表不能覆盖更新进度，另附暂停显示标志。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void overlayProgress();

    /*
     * 功能：在 GUI 映射当前世界任务读结果
     * 参数：jobs：输入，拥有任务数组或错误的结果。
     * 返回：无。
     * 失败：失败保留旧列表并显示中文错误。
     * 副作用：成功转换列表并覆盖进度，发 changed，不改库。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void applyListing(xuyan::domain::Result<std::vector<xuyan::domain::ExtractionJob>> jobs);

    /*
     * 功能：补做异步操作期间遗漏的任务刷新
     * 参数：无。
     * 返回：无。
     * 失败：无标志忽略，refresh 忙碌时可再次延迟。
     * 副作用：消费标志并调用 refresh。
     * 线程与生命周期：GUI 调用，线程池查询，不同步等待。
     */
    void refreshAfterWorldChange();

    /*
     * 功能：将领域任务转换为界面任务及步骤列表
     * 参数：jobs：输入，调用期间借用的当前世界任务数组，无额外数量上限。
     * 返回：独立列表；保留协议、修订、调用硬上限、词元统计及绝对码点范围，中文状态另列。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionJob>& jobs);
    /* 任务库本机路径，构造确定，实例持有；后台复制。 */
    std::filesystem::path database_path_;
    /* 当前世界任务界面列表，默认空；GUI 列表回填及进度覆盖，无额外分页上限。 */
    QVariantList jobs_;
    /* 当前界面世界 ID，默认空；GUI 切换，空时不查询。 */
    QString world_id_;
    /* 世界代次，默认0；切世界递增，GUI 查询回调比对。 */
    std::uint64_t world_generation_{0};
    /* 遗漏刷新标志，默认false；busy 时刷新/切世界置位，完成后消费。 */
    bool refresh_after_world_change_{false};
    /* 恢复/列表/创建/取消/重试/复核在途标志，默认false；不同于 running_，GUI 更新。 */
    bool busy_{false};
    /* 下一次刷新保留批次失败提示标志，默认false；finishBatch置位，refresh消费。 */
    bool retain_error_on_refresh_{false};
    /* 实例拥有的批次串行线程池，最大1线程；析构停止调度并 waitForDone，禁止 detached。 */
    QThreadPool worker_pool_;
    /* 实例拥有的同步执行端口，默认正式 executeBatch；工作线程复制使用，禁止回调逃逸。 */
    BatchRunner batch_runner_;
    /* GUI和自有工作线程共享的运行控制，默认空；一次批次一份，终结复位，含原子动作及停止源。 */
    std::shared_ptr<RunControl> run_control_;
    /* 是否有批次或补取消尚未终结，默认false；GUI 启动置位，finishBatch有效终结复位。 */
    bool running_{false};
    /* 实际运行任务 ID，默认空；GUI 启动/结束更新，切世界不更改。 */
    QString active_job_id_;
    /* 实际批次目标世界 ID，默认空；与界面选择区分，GUI 用于控制/提示归属。 */
    QString active_world_id_;
    /* 批次会话代次，默认0；GUI 每次启动递增，进度/终结回调拒绝旧会话。 */
    std::uint64_t run_generation_{0};
    /* 按任务 ID 的最近已提交计数/修订映射，默认空；GUI 进度回调写入，列表覆盖读取。 */
    QHash<QString, QVariantMap> progress_;
    /* 本进程已暂停任务 ID 集合，默认空；GUI 终结时添加、再次启动移除，不冒充持久化状态。 */
    QSet<QString> paused_jobs_;
    /* 任务操作中文失败提示，默认空；GUI 回填，批次失败可跨下一次刷新保留。 */
    QString error_text_;
    /* 任务中文状态，初始队列就绪；GUI 设置，不展示模型原始错误。 */
    QString status_text_{tr("任务队列已就绪")};
};
