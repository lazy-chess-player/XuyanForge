#pragma once

#include "xuyan/domain/extraction_candidate.h"

#include <QObject>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <vector>

/*
 * 职责：维护当前世界候选分页、选择和人工审核，不自动接受模型结果。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class CandidateReviewViewModel final : public QObject {
    Q_OBJECT
    /* 属性：返回当前世界且符合审核筛选的候选摘要。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    /* 属性：返回当前选中的候选索引；无选择时为 -1。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    /* 属性：返回当前候选的稳定标识。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    /* 属性：读取候选类型协议值；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY changed)
    /* 属性：返回当前候选的可编辑名称。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
    /* 属性：返回当前候选的结构化字段。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedFields READ selectedFields NOTIFY changed)
    /* 属性：返回当前候选的原文引文。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedQuote READ selectedQuote NOTIFY changed)
    /* 属性：读取候选来源性质协议值；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedProvenance READ selectedProvenance NOTIFY changed)
    /* 属性：返回当前候选关联的小说来源标识。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedSource READ selectedSource NOTIFY changed)
    /* 属性：格式化所选候选的绝对原文范围；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedRange READ selectedRange NOTIFY changed)
    /* 属性：返回当前候选的审核修订号。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedRevision READ selectedRevision NOTIFY changed)
    /* 属性：返回当前审核状态筛选条件。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString filter READ filter NOTIFY changed)
    /* 属性：返回当前候选列表所属的世界标识。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString worldId READ worldId NOTIFY changed)
    /* 属性：返回是否正在读取候选或提交审核。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：返回最近一次操作的错误说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：返回最近一次操作的状态说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /* 属性：返回从零开始的当前候选页索引。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(qlonglong pageIndex READ pageIndex NOTIFY changed)
    /* 属性：计算匹配当前筛选的候选页数；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(qlonglong pageCount READ pageCount NOTIFY changed)
    /* 属性：返回当前世界符合筛选条件的候选总数。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(qlonglong totalCount READ totalCount NOTIFY changed)
    /* 属性：返回是否可以读取上一页。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool canPreviousPage READ canPreviousPage NOTIFY changed)
    /* 属性：返回是否可以读取下一页。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool canNextPage READ canNextPage NOTIFY changed)
public:

    /*
     * 功能：绑定候选库且等待世界选择
     * 参数：database_path：输入，实例持有的本机数据库路径；parent：输入，可空 Qt 所有者，默认空。
     * 返回：完成空候选页初始化。
     * 失败：路径或 QObject 初始化异常传播。
     * 副作用：不查询或创建资料。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit CandidateReviewViewModel(std::filesystem::path database_path, QObject* parent = nullptr);


    /*
     * 功能：返回当前世界且符合审核筛选的候选摘要。
     * 参数：无。
     * 返回：返回当前世界且符合审核筛选的候选摘要。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList candidates() const { return items_; }

    /*
     * 功能：返回当前选中的候选索引；无选择时为 -1。
     * 参数：无。
     * 返回：返回当前选中的候选索引；无选择时为 -1。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedIndex() const noexcept { return selected_index_; }

    /*
     * 功能：返回当前候选的稳定标识。
     * 参数：无。
     * 返回：返回当前候选的稳定标识。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedId() const;

    /*
     * 功能：读取候选类型协议值
     * 参数：无。
     * 返回：原始类型值，无选择为空；供编辑器判断，不直接显示。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedType() const;

    /*
     * 功能：返回当前候选的可编辑名称。
     * 参数：无。
     * 返回：返回当前候选的可编辑名称。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedName() const;

    /*
     * 功能：返回当前候选的结构化字段。
     * 参数：无。
     * 返回：返回当前候选的结构化字段。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedFields() const;

    /*
     * 功能：返回当前候选的原文引文。
     * 参数：无。
     * 返回：返回当前候选的原文引文。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedQuote() const;

    /*
     * 功能：读取候选来源性质协议值
     * 参数：无。
     * 返回：原始性质值，无选择为空，供审核表单选择。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedProvenance() const;

    /*
     * 功能：返回当前候选关联的小说来源标识。
     * 参数：无。
     * 返回：返回当前候选关联的小说来源标识。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedSource() const;

    /*
     * 功能：格式化所选候选的绝对原文范围
     * 参数：无。
     * 返回：Unicode 码点起点–终点，终点不含；无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedRange() const;

    /*
     * 功能：返回当前候选的审核修订号。
     * 参数：无。
     * 返回：返回当前候选的审核修订号。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedRevision() const noexcept;

    /*
     * 功能：返回当前审核状态筛选条件。
     * 参数：无。
     * 返回：返回当前审核状态筛选条件。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString filter() const { return filter_; }

    /*
     * 功能：返回当前候选列表所属的世界标识。
     * 参数：无。
     * 返回：返回当前候选列表所属的世界标识。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString worldId() const { return world_id_; }

    /*
     * 功能：返回是否正在读取候选或提交审核。
     * 参数：无。
     * 返回：返回是否正在读取候选或提交审核。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }

    /*
     * 功能：返回最近一次操作的错误说明。
     * 参数：无。
     * 返回：返回最近一次操作的错误说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：返回最近一次操作的状态说明。
     * 参数：无。
     * 返回：返回最近一次操作的状态说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }

    /*
     * 功能：返回从零开始的当前候选页索引。
     * 参数：无。
     * 返回：返回从零开始的当前候选页索引。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    qlonglong pageIndex() const noexcept { return page_index_; }

    /*
     * 功能：计算匹配当前筛选的候选页数
     * 参数：无。
     * 返回：空集合为0；否则按100项向上取整。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    qlonglong pageCount() const noexcept { return total_count_ == 0 ? 0 : (total_count_ - 1) / page_size_ + 1; }

    /*
     * 功能：返回当前世界符合筛选条件的候选总数。
     * 参数：无。
     * 返回：返回当前世界符合筛选条件的候选总数。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    qlonglong totalCount() const noexcept { return total_count_; }

    /*
     * 功能：返回是否可以读取上一页。
     * 参数：无。
     * 返回：返回是否可以读取上一页。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool canPreviousPage() const noexcept { return !busy_ && page_index_ > 0; }

    /*
     * 功能：返回是否可以读取下一页。
     * 参数：无。
     * 返回：返回是否可以读取下一页。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool canNextPage() const noexcept { return !busy_ && page_index_ + 1 < pageCount(); }


    /*
     * 功能：重读当前候选页并尽量保留选择
     * 参数：无。
     * 返回：无。
     * 失败：忙碌或无世界忽略；读库失败写入中文 errorText。
     * 副作用：投递 load，更新列表及 changed。
     * 线程与生命周期：GUI 发起，线程池读库，GUI 按请求代次回填。
     */
    Q_INVOKABLE void refresh();

    /*
     * 功能：切世界并清空旧候选页
     * 参数：world_id：输入，稳定世界 ID；空表示取消选择。
     * 返回：无。
     * 失败：同值忽略；新查询失败写 errorText。
     * 副作用：递增代次，重置页及审核标志；非空则读页。
     * 线程与生命周期：GUI 调用；旧审核可能已提交，作废通知不撤销旧事务。
     */
    Q_INVOKABLE void setWorldId(QString world_id);

    /*
     * 功能：切换审核筛选并回首页
     * 参数：status：输入，审核协议值；合法性由服务验证。
     * 返回：无。
     * 失败：审核中或同值忽略；非法值产生服务失败。
     * 副作用：使旧读页失效，清空选择并异步读页。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void setFilter(QString status);

    /*
     * 功能：选择本页候选
     * 参数：index：输入，零基下标，须位于本页范围。
     * 返回：无。
     * 失败：越界或重复选择忽略。
     * 副作用：更新选择并发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void selectCandidate(int index);

    /*
     * 功能：读取上一候选页
     * 参数：无。
     * 返回：无。
     * 失败：忙碌或首页忽略；查询错误显示中文提示。
     * 副作用：减页号、清空选择并投递读页。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void previousPage();

    /*
     * 功能：读取下一候选页
     * 参数：无。
     * 返回：无。
     * 失败：忙碌或末页忽略；查询错误显示中文提示。
     * 副作用：增页号、清空选择并投递读页。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void nextPage();

    /*
     * 功能：校验所选候选来源世界并按修订提交人工审核
     * 参数：status：目标审核协议值；name：编辑名称；fields_json：完整字段 JSON；provenance_type：来源性质协议值；均为输入，由服务按候选版本验证。
     * 返回：无。
     * 失败：忙碌、未选择或无世界忽略；归属变化、修订冲突、字段无效或存储失败显示中文分类错误。
     * 副作用：服务原子提交候选、接受投影和证据；接受成功发 candidateAccepted，随后重载当前页。
     * 线程与生命周期：GUI 捕获 ID、修订及表单值，线程池写库；GUI 按请求代次隔离旧世界结果，丢弃回调不回滚事务。
     */
    Q_INVOKABLE void reviewSelected(QString status, QString name, QString fields_json, QString provenance_type);

signals:
    /*
     * 功能：通知界面重读候选属性
     * 参数：无。
     * 返回：无。
     * 失败：无接收者处理结果。
     * 副作用：按 Qt 连接类型调用槽，不写库。
     * 线程与生命周期：GUI 发出，Qt 管理接收者连接生命周期。
     */
    void changed();
    /*
     * 功能：通知当前请求的候选已接受
     * 参数：无。
     * 返回：无。
     * 失败：审核失败或代次过期不发送。
     * 副作用：接收者可刷新世界投影。
     * 线程与生命周期：GUI 在服务提交后发送，不等待接收方刷新。
     */
    void candidateAccepted();

private:

    /*
     * 功能：借用当前所选领域候选
     * 参数：无。
     * 返回：有效索引返回非拥有指针，无选择返回空。
     * 失败：不抛异常，检查上下界。
     * 副作用：只读候选容器。
     * 线程与生命周期：GUI 同步调用；列表替换、清空或实例销毁后指针失效。
     */
    const xuyan::domain::ExtractionCandidate* selected() const noexcept;

    /*
     * 功能：转换当前候选页为界面摘要
     * 参数：candidates：输入，调用期间借用的候选数组，正常最多100项。
     * 返回：独立列表，含引文、修订及绝对码点半开范围，协议值与中文审核标签分离。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionCandidate>& candidates);

    /*
     * 功能：后台按世界和审核筛选读取一页候选
     * 参数：keep_id：输入，默认空；若返回页有此 ID 则恢复选择，否则选首项。
     * 返回：无。
     * 失败：忙碌或无世界忽略；末页变空则退到有效末页；失败写 errorText。
     * 副作用：递增请求代次并置 busy，GUI 更新总数、页、选择和 changed。
     * 线程与生命周期：线程池独立连接查询，GUI 按代次拒绝旧世界、筛选或页结果；销毁后放弃通知。
     */
    void load(QString keep_id = {});

    /* 本机数据库路径；构造确定，实例持有，后台复制。 */
    std::filesystem::path database_path_;
    /* 当前页领域候选，初始空，最多100项；GUI 有效读页替换。 */
    std::vector<xuyan::domain::ExtractionCandidate> candidates_;
    /* 与 candidates_ 对齐的 QML 摘要，初始空；GUI 生成，界面只读。 */
    QVariantList items_;
    /* 当前页零基选择下标，默认-1 表示无选择；GUI 更新。 */
    int selected_index_{-1};
    /* 审核筛选协议，默认 candidate；GUI 修改，后台复制查询。 */
    QString filter_{QStringLiteral("candidate")};
    /* 当前世界 ID，默认空表示不查询；GUI 切换。 */
    QString world_id_;
    /* 请求代次，默认0，无单位；读页/审核/切换递增，拒绝迟到结果。 */
    std::uint64_t request_generation_{0};
    /* 固定页容量100项；查询及页数计算共用。 */
    static constexpr int page_size_ = 100;
    /* 零基页号，默认0；GUI 翻页更新，切世界/筛选归零。 */
    qlonglong page_index_{0};
    /* 当前筛选匹配候选个数，默认0；成功分页查询回填。 */
    qlonglong total_count_{0};
    /* 读页或审核在途标志，默认false；GUI 开始置位，有效回调复位。 */
    bool busy_{false};
    /* 审核在途标志，默认false；阻止审核途中切筛选，结束或切世界复位。 */
    bool reviewing_{false};
    /* 中文失败提示，默认空；入口清理，GUI 有效回调写入。 */
    QString error_text_;
    /* 候选页/审核中文状态；初始提示选世界，GUI 更新。 */
    QString status_text_{QStringLiteral("请选择世界以查看待校对候选")};
};
