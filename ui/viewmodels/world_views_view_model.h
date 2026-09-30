#pragma once

#include "xuyan/domain/scenario.h"

#include <QObject>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <functional>

/*
 * 职责：维护当前世界的版本、时间线、关系、地图及人物实例摘要；未知数值保持未知，作者显式操作才写入。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class WorldViewsViewModel final : public QObject {
    Q_OBJECT
    /* 属性：读取当前世界版本摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    /* 属性：读取当前世界时间事件摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY changed)
    /* 属性：读取作者视角的当前世界关系摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList relations READ relations NOTIFY changed)
    /* 属性：读取当前世界地点层级及图像坐标摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList locations READ locations NOTIFY changed)
    /* 属性：读取当前世界路线摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed)
    /* 属性：读取当前世界最新版本人物实例摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList instances READ instances NOTIFY changed)
    /* 属性：读取本进程当前世界最近生成快照 ID；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString latestSnapshotId READ latestSnapshotId NOTIFY changed)
    /* 属性：返回当前世界视图是否正在后台读取或写入。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：返回当前世界视图的错误说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：返回当前世界视图的状态说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:

    /*
     * 功能：绑定世界服务路径，初始不查询任何世界
     * 参数：database_path：输入，本机数据库路径，按值持有；parent：输入，可空 Qt 所有者，默认空。
     * 返回：完成空世界视图初始化。
     * 失败：路径或 QObject 初始化异常传播。
     * 副作用：不读库，不造数据。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit WorldViewsViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /*
     * 功能：读取当前世界版本摘要
     * 参数：无。
     * 返回：含 ID、父版本、摘要、成员数及发布时间的列表副本，初始空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList versions() const { return versions_; }

    /*
     * 功能：读取当前世界时间事件摘要
     * 参数：无。
     * 返回：事件列表副本，故事时间未知显示中文未知；叙事序号不冒充故事时间。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList timeline() const { return timeline_; }

    /*
     * 功能：读取作者视角的当前世界关系摘要
     * 参数：无。
     * 返回：保留方向、真实性、证据及可见性协议，未知强度为空 QVariant，已知0保持0。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList relations() const { return relations_; }

    /*
     * 功能：读取当前世界地点层级及图像坐标摘要
     * 参数：无。
     * 返回：未知坐标显示中文未知，已知0保持0；真实性协议保留。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList locations() const { return locations_; }

    /*
     * 功能：读取当前世界路线摘要
     * 参数：无。
     * 返回：未知行程分钟显示未知，方向及证据协议保留。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList routes() const { return routes_; }

    /*
     * 功能：读取当前世界最新版本人物实例摘要
     * 参数：无。
     * 返回：含卡 ID、版本、知识策略及状态协议；另附中文状态，初始空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList instances() const { return instances_; }

    /*
     * 功能：读取本进程当前世界最近生成快照 ID
     * 参数：无。
     * 返回：未生成或切世界为空；不是数据库中全部快照列表。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString latestSnapshotId() const { return latest_snapshot_id_; }

    /*
     * 功能：返回当前世界视图是否正在后台读取或写入。
     * 参数：无。
     * 返回：返回当前世界视图是否正在后台读取或写入。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }

    /*
     * 功能：返回当前世界视图的错误说明。
     * 参数：无。
     * 返回：返回当前世界视图的错误说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：返回当前世界视图的状态说明。
     * 参数：无。
     * 返回：返回当前世界视图的状态说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }


    /*
     * 功能：后台查询当前世界版本、图记录和最新版本实例
     * 参数：无。
     * 返回：无。
     * 失败：busy 或无世界忽略；服务失败显示中文错误；部分成功结果可能保留在本次列表，不当全成功。
     * 副作用：只读服务，GUI 按代次替换六类摘要并发 changed，不发送请求。
     * 线程与生命周期：线程池独立服务查询，GUI 比对 generation；对象销毁或切换丢弃通知。
     */
    Q_INVOKABLE void refresh();

    /*
     * 功能：切世界并使旧读写结果通知失效
     * 参数：world_id：输入，稳定世界 ID，空表示取消选择。
     * 返回：无。
     * 失败：同值忽略；新查询失败经 errorText 提示。
     * 副作用：递增代次，清所有列表及最近快照；非空发起刷新。
     * 线程与生命周期：GUI 修改；旧后台写入仍可提交原目标，过期 GUI 回调丢弃，不回滚。
     */
    void setWorldId(QString world_id);

    /*
     * 功能：显式发布当前世界不可变版本
     * 参数：parent_id：输入，可空父版本 ID，空由服务按版本契约处理。
     * 返回：无。
     * 失败：busy/无世界或服务校验/存储失败显示或忽略，按 run 行为。
     * 副作用：后台发布版本及命令，GUI 显示稳定版本 ID并刷新。
     * 线程与生命周期：GUI 捕获世界/父 ID，线程池执行，GUI 按代次通知，销毁不回滚发布。
     */
    Q_INVOKABLE void publishVersion(QString parent_id);

    /*
     * 功能：按世界版本和故事时间生成历史快照
     * 参数：version_id：输入，目标世界版本 ID，由服务校验；story_time：输入，整数文本，单位为该世界故事时间基准，不是 UTC 或章节序号。
     * 返回：无。
     * 失败：无世界或时间非整数显示错误；busy 忽略；版本/存储失败显示分类错误。
     * 副作用：后台写快照及命令，GUI 更新 latestSnapshotId 和未解条目计数，刷新摘要。
     * 线程与生命周期：线程池执行，GUI 按代次应用；世界改变后旧快照仍可能保存但不回填。
     */
    Q_INVOKABLE void prepareSnapshot(QString version_id, QString story_time);

    /*
     * 功能：显式保存当前世界新时间事件
     * 参数：name：事件名称；story_time：可空整数故事时间，空或解析失败当前保留未知；narrative_order：叙事顺序整数，不代表因果；relative_time：原始相对时间描述；truth_status：真实性协议值；均为输入，由服务进一步校验。
     * 返回：无。
     * 失败：无世界/busy 由 run处理；字段/存储失败显示中文错误。
     * 副作用：后台保存新事件及命令，GUI 刷新，不推断日期。
     * 线程与生命周期：GUI 捕获表单及世界 ID，线程池同步业务回调，GUI 按代次通知。
     */
    Q_INVOKABLE void addTimelineEvent(QString name, QString story_time, int narrative_order,
                                      QString relative_time, QString truth_status);

    /*
     * 功能：显式保存当前世界有向关系
     * 参数：from_id：主语条目 ID；to_id：宾语 ID；dimension：关系维度；strength：整数强度，已知0有效，本入口无法输入未知；visibility：可见性协议；evidence_status：证据状态协议；均为输入。
     * 返回：无。
     * 失败：端点/世界/字段校验或存储失败显示错误，busy忽略。
     * 副作用：服务保存新关系及命令，GUI 刷新；本入口不设自动双向。
     * 线程与生命周期：GUI 捕获值，线程池执行，GUI 按代次通知。
     */
    Q_INVOKABLE void addRelation(QString from_id, QString to_id, QString dimension, int strength,
                                 QString visibility, QString evidence_status);

    /*
     * 功能：显式保存地点父节点和可选图像坐标
     * 参数：location_id：地点 ID；parent_id：可空父地点 ID；x：可空横坐标整数文本，单位由底图定义；y：可空纵坐标整数文本；evidence_status：证据协议；均为输入，只有两坐标都可解析时才保存已知坐标。
     * 返回：无。
     * 失败：无世界/busy 由 run处理；非法坐标当前保持未知；层级环/修订或存储失败显示错误。
     * 副作用：后台保存标注，GUI 刷新；不把缺坐标填0。
     * 线程与生命周期：线程池业务回调同步运行，GUI 按代次通知。
     */
    Q_INVOKABLE void addLocation(QString location_id, QString parent_id, QString x, QString y, QString evidence_status);

    /*
     * 功能：显式保存有向地点行程路线
     * 参数：from_id：起点地点 ID；to_id：终点地点 ID；minutes：可空行程整数文本，单位分钟，空或解析失败保持未知；evidence_status：证据协议；均为输入，数值合法范围由服务校验。
     * 返回：无。
     * 失败：端点/时长/存储失败显示错误，busy 忽略。
     * 副作用：后台保存路线和命令，GUI 刷新，不推定未知行程。
     * 线程与生命周期：GUI 捕获值，线程池执行，GUI 按代次通知。
     */
    Q_INVOKABLE void addRoute(QString from_id, QString to_id, QString minutes, QString evidence_status);

    /*
     * 功能：显式将人物卡版本实例化到世界历史上下文
     * 参数：blueprint_id：卡 ID；blueprint_version：卡版本整数；world_version_id：世界版本 ID；snapshot_id：历史快照 ID；adaptation_json：适配 JSON；knowledge_policy：知识策略协议；均为输入，由实例服务校验。
     * 返回：无。
     * 失败：卡/版本/快照/适配不符或存储失败显示中文错误；存在业务冲突时成功返回需处理实例。
     * 副作用：后台保存人物实例，GUI 报就绪或冲突数量并刷新，不改通用卡。
     * 线程与生命周期：线程池同步业务回调，GUI 按代次通知；销毁不回滚实例。
     */
    Q_INVOKABLE void instantiateCharacter(QString blueprint_id, int blueprint_version, QString world_version_id,
                                          QString snapshot_id, QString adaptation_json, QString knowledge_policy);

    /*
     * 功能：显式固定一个人物实例到分支根上下文
     * 参数：branch_id：分支 ID；world_version_id：世界版本 ID；snapshot_id：快照 ID；history_mode：历史模式协议；instance_id：单个人物实例 ID；均为输入，服务校验归属与一致性。
     * 返回：无。
     * 失败：分支/版本/实例冲突或存储失败显示中文错误，busy忽略。
     * 副作用：后台绑定分支根及命令，GUI 显示根摘要并刷新。
     * 线程与生命周期：线程池执行，GUI 按代次通知。
     */
    Q_INVOKABLE void bindBranch(QString branch_id, QString world_version_id, QString snapshot_id,
                                QString history_mode, QString instance_id);
signals:

    /*
     * 功能：通知界面重读六类世界视图及状态
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收方结果。
     * 副作用：调用 Qt 已连接槽，不额外写库。
     * 线程与生命周期：GUI 发出。
     */
    void changed();
private:
    using StringResult = xuyan::domain::Result<std::string>;

    /*
     * 功能：投递当前世界的一次同步业务写回调
     * 参数：work：输入，按值拥有回调；接收只在调用期间有效的本机路径引用，返回 StringResult，成功值须为安全中文提示，不访问 GUI。
     * 返回：无。
     * 失败：busy忽略，无世界报错；回调异常转中文失败，服务失败不触发刷新。
     * 副作用：线程池业务写入，GUI 按代次应用提示并刷新。
     * 线程与生命周期：GUI 捕获代次，线程池同步执行回调；QPointer失效或代次变化只抑制通知，不撤销提交。
     */
    void run(std::function<StringResult(const std::filesystem::path&)> work);
    /* 世界视图库本机路径，构造确定，实例持有，后台复制。 */
    std::filesystem::path database_path_;
    /* 当前世界版本摘要，初始空；GUI 有效刷新替换，切世界清空。 */
    QVariantList versions_;
    /* 当前世界事件摘要，初始空；GUI 回填，时间未知保留，无额外分页上限。 */
    QVariantList timeline_;
    /* 作者视角关系摘要，初始空；GUI 回填，协议/方向/未知强度不改。 */
    QVariantList relations_;
    /* 地点层级与可选坐标摘要，初始空；GUI 回填，未知不补零。 */
    QVariantList locations_;
    /* 有向路线及可选行程分钟摘要，初始空；GUI 回填。 */
    QVariantList routes_;
    /* 当前世界最新版本人物实例摘要，初始空；GUI 回填，与历史快照选择不是同一过滤器。 */
    QVariantList instances_;
    /* 本进程当前世界最近生成快照 ID，初始空；GUI 成功生成设置，切世界清空。 */
    QString latest_snapshot_id_;
    /* 当前代次读写在途标志，初始false；GUI置位/有效回调复位，切世界直接复位。 */
    bool busy_{false};
    /* 世界视图中文失败提示，初始空；GUI有效回调设置，不显示异常正文。 */
    QString error_text_;
    /* 世界视图中文状态，初始提示选世界；GUI 写成功/快照回调更新。 */
    QString status_text_{tr("请选择世界以查看世界视图")};
    /* 界面当前世界稳定 ID，初始空；GUI 切换，后台按值捕获。 */
    QString world_id_;
    /* 读写上下文代次，初始0；刷新/写入/切世界递增，GUI 拒绝迟到结果。 */
    std::uint64_t generation_{0};
};
