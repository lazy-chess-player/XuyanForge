#pragma once

#include "xuyan/domain/source_document.h"
#include "xuyan/domain/evidence.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

/*
 * 职责：按世界维护小说来源、章节和证据；原文只读分窗，不长期加载整书。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class SourceViewModel final : public QObject {
    Q_OBJECT
    /* 属性：返回来源操作是否正在执行。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：返回最近一次来源操作的中文错误。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：返回当前世界的小说列表。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList sourceItems READ sourceItems NOTIFY changed)
    /* 属性：返回当前小说的章节目录。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList chapterItems READ chapterItems NOTIFY changed)
    /* 属性：返回当前小说在当前世界列表中的下标。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    /* 属性：读取当前按需加载的原文窗口；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString previewText READ previewText NOTIFY changed)
    /* 属性：读取所选来源记录的原文内容摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedHash READ selectedHash NOTIFY changed)
    /* 属性：返回当前小说的证据引用列表。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList evidenceItems READ evidenceItems NOTIFY changed)
    /* 属性：返回最近一次来源操作的状态说明。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /* 属性：读取预览高亮起点；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int highlightStart READ highlightStart NOTIFY changed)
    /* 属性：读取预览高亮终点；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int highlightEnd READ highlightEnd NOTIFY changed)
    /* 属性：返回当前章节的零基下标。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedChapterIndex READ selectedChapterIndex NOTIFY changed)
    /* 属性：读取所选来源的稳定 ID；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedSourceId READ selectedSourceId NOTIFY changed)
    /* 属性：返回当前世界的稳定标识。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString worldId READ worldId NOTIFY changed)
    /* 属性：返回当前原文窗口是否仍在后台读取。；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
    /* 属性：读取当前窗口绝对原文起点；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(qlonglong previewStart READ previewStart NOTIFY changed)
    /* 属性：读取当前窗口绝对原文终点；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(qlonglong previewEnd READ previewEnd NOTIFY changed)
    /* 属性：判断本章是否有上一窗口；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool canPreviousWindow READ canPreviousWindow NOTIFY changed)
    /* 属性：判断本章是否有下一窗口；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool canNextWindow READ canNextWindow NOTIFY changed)

public:

    /*
     * 功能：绑定来源库，等待世界选择后读取
     * 参数：database_path：输入，本机数据库路径，按值持有；parent：输入，可空 Qt 所有者，默认空。
     * 返回：完成空来源、章节和预览初始化。
     * 失败：路径/QObject 初始化异常传播。
     * 副作用：不读取正文或创建来源。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit SourceViewModel(std::filesystem::path database_path, QObject* parent = nullptr);


    /*
     * 功能：返回来源操作是否正在执行。
     * 参数：无。
     * 返回：返回来源操作是否正在执行。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }

    /*
     * 功能：返回最近一次来源操作的中文错误。
     * 参数：无。
     * 返回：返回最近一次来源操作的中文错误。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：返回当前世界的小说列表。
     * 参数：无。
     * 返回：返回当前世界的小说列表。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList sourceItems() const { return source_items_; }

    /*
     * 功能：返回当前小说的章节目录。
     * 参数：无。
     * 返回：返回当前小说的章节目录。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList chapterItems() const { return chapter_items_; }

    /*
     * 功能：返回当前小说在当前世界列表中的下标。
     * 参数：无。
     * 返回：返回当前小说在当前世界列表中的下标。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedIndex() const noexcept { return selected_index_; }

    /*
     * 功能：读取当前按需加载的原文窗口
     * 参数：无。
     * 返回：最多50000码点原文；读取中为中文加载提示，未选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString previewText() const { return preview_text_; }

    /*
     * 功能：读取所选来源记录的原文内容摘要
     * 参数：无。
     * 返回：无选择为空，不在访问器重新计算文件摘要。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedHash() const;

    /*
     * 功能：返回当前小说的证据引用列表。
     * 参数：无。
     * 返回：返回当前小说的证据引用列表。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList evidenceItems() const { return evidence_items_; }

    /*
     * 功能：返回最近一次来源操作的状态说明。
     * 参数：无。
     * 返回：返回最近一次来源操作的状态说明。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }

    /*
     * 功能：读取预览高亮起点
     * 参数：无。
     * 返回：窗口内零基 UTF-16 码元偏移，默认0；与末尾共同组成半开范围。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int highlightStart() const noexcept { return highlight_start_; }

    /*
     * 功能：读取预览高亮终点
     * 参数：无。
     * 返回：窗口内不含末尾的 UTF-16 码元偏移，默认0；不是整书码点位置。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int highlightEnd() const noexcept { return highlight_end_; }

    /*
     * 功能：返回当前章节的零基下标。
     * 参数：无。
     * 返回：返回当前章节的零基下标。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedChapterIndex() const noexcept { return selected_chapter_index_; }

    /*
     * 功能：读取所选来源的稳定 ID
     * 参数：无。
     * 返回：无选择返回空，供任务创建和原文定位。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedSourceId() const { return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].id) : QString{}; }

    /*
     * 功能：返回当前世界的稳定标识。
     * 参数：无。
     * 返回：返回当前世界的稳定标识。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString worldId() const { return world_id_; }

    /*
     * 功能：返回当前原文窗口是否仍在后台读取。
     * 参数：无。
     * 返回：返回当前原文窗口是否仍在后台读取。返回独立值快照；数值单位及无选择约定见属性对应成员。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool previewLoading() const noexcept { return preview_loading_; }

    /*
     * 功能：读取当前窗口绝对原文起点
     * 参数：无。
     * 返回：零基 Unicode 码点偏移，初始0。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    qlonglong previewStart() const noexcept { return static_cast<qlonglong>(preview_start_codepoint_); }

    /*
     * 功能：读取当前窗口绝对原文终点
     * 参数：无。
     * 返回：Unicode 码点偏移，终点不含，初始0。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    qlonglong previewEnd() const noexcept { return static_cast<qlonglong>(preview_end_codepoint_); }

    /*
     * 功能：判断本章是否有上一窗口
     * 参数：无。
     * 返回：存在所选章且窗口起点在章起点之后为true；不代表后台读取已结束。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool canPreviousWindow() const noexcept;

    /*
     * 功能：判断本章是否有下一窗口
     * 参数：无。
     * 返回：存在所选章且窗口末尾在章末之前为true；不代表后台读取已结束。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool canNextWindow() const noexcept;


    /*
     * 功能：切世界并使旧来源/预览请求失效
     * 参数：world_id：输入，稳定世界 ID，空表示取消选择。
     * 返回：无。
     * 失败：同值忽略；新世界查询失败显示中文 errorText。
     * 副作用：清来源、章节、证据和窗口，递增两种代次；在途操作完成后补刷新。
     * 线程与生命周期：GUI 调用；旧写入仍作用原目标，不回滚，GUI 拒绝旧世界/窗口结果。
     */
    void setWorldId(QString world_id);

    /*
     * 功能：读取当前世界来源并恢复稳定章节及阅读位置
     * 参数：无。
     * 返回：无。
     * 失败：世界为空清可见内容；busy 时记录补刷新；读取失败保留错误。
     * 副作用：线程池查询来源元数据，GUI 应用并按需加载窗口，发 changed。
     * 线程与生命周期：GUI 捕获世界 ID/代次，线程池独立连接；GUI 按代次隔离切世界结果。
     */
    Q_INVOKABLE void refresh();

    /*
     * 功能：将本地小说导入指定当前世界并关联来源
     * 参数：file_url：输入，本机文件 URL；world_id：输入，非空且必须与当前世界一致。
     * 返回：无。
     * 失败：busy 或非本机忽略；无世界/归属变化报错；编码、资产、导入/关联或查询失败经 errorText 通知。
     * 副作用：写标准化资产及来源、关联世界；成功发 sourceImported，按需预览，不联网。
     * 线程与生命周期：GUI 捕获文件和世界代次，线程池独立服务导入；旧世界完成回调只补刷新，不回填原列表，已写资产不自动回滚。
     */
    Q_INVOKABLE void importFile(const QUrl& file_url, QString world_id);

    /*
     * 功能：按列表索引选择来源
     * 参数：index：输入，当前列表零基有效下标。
     * 返回：无。
     * 失败：越界忽略。
     * 副作用：清延迟 ID 选择意图，重建章节并异步载入首窗。
     * 线程与生命周期：GUI 修改选择，线程池读窗，GUI 按窗口代次回填。
     */
    Q_INVOKABLE void selectSource(int index);

    /*
     * 功能：按稳定 ID 选择或等待目录刷新后选择
     * 参数：source_id：输入，非空来源 ID；世界必须已选择。
     * 返回：无。
     * 失败：空 ID/无世界忽略；不在列表时刷新，失败由刷新提示。
     * 副作用：保存 desired_source_id_；找到即清意图并选中，未找到异步刷新。
     * 线程与生命周期：GUI 调用，异步刷新按世界代次应用。
     */
    Q_INVOKABLE void selectSourceId(QString source_id);

    /*
     * 功能：把预览选区换算为绝对码点并提交字段证据
     * 参数：entity_id：输入，目标条目 ID；field_path：输入，字段路径协议值；selection_start：输入，窗口 UTF-16 起点，非负；selection_end：输入，UTF-16 不含末尾，须大于起点且不超文本长度；provenance_type：输入，来源性质协议值，由服务校验。
     * 返回：无。
     * 失败：busy、预览未完成或无来源忽略；非法选区显示提示；目标/区间/存储失败显示中文错误。
     * 副作用：后台读取原文校验证据并写库，再查询来源证据；GUI 更新列表和状态，不改原文。
     * 线程与生命周期：GUI 完成码元到码点换算后捕获值，线程池执行；世界代次/来源不符时拒绝回填并补刷新，写入不回滚。
     */
    Q_INVOKABLE void createEvidence(QString entity_id, QString field_path, int selection_start,
                                    int selection_end, QString provenance_type);

    /*
     * 功能：定位证据原文窗口并准备 UTF-16 高亮
     * 参数：index：输入，当前证据列表零基有效下标。
     * 返回：无。
     * 失败：越界忽略；无法在50000码点窗口完整显示时提示选择章节。
     * 副作用：必要时切章节和窗口，记录待高亮绝对码点范围；可跨章显示引文。
     * 线程与生命周期：GUI 处理定位，后台读取窗口，GUI 成功加载后高亮；过期回调不得消费新待选区。
     */
    Q_INVOKABLE void selectEvidence(int index);

    /*
     * 功能：选择当前来源章节并读首窗口
     * 参数：index：输入，当前章节零基有效下标。
     * 返回：无。
     * 失败：无来源或越界忽略。
     * 副作用：清待证据范围，设置章节索引，异步读取至多50000码点。
     * 线程与生命周期：GUI 发起，线程池读原文，GUI 按窗口代次应用。
     */
    Q_INVOKABLE void selectChapter(int index);

    /*
     * 功能：读取本章上一段原文
     * 参数：无。
     * 返回：无。
     * 失败：无上一窗口忽略；资产读取失败显示 errorText。
     * 副作用：清待证据选区、切窗，递增预览代次。
     * 线程与生命周期：GUI 调用，线程池读窗，GUI 拒绝迟到结果。
     */
    Q_INVOKABLE void previousWindow();

    /*
     * 功能：读取本章下一段原文
     * 参数：无。
     * 返回：无。
     * 失败：无下一窗口忽略；资产读取失败显示 errorText。
     * 副作用：清待证据选区、切窗，递增预览代次。
     * 线程与生命周期：GUI 调用，线程池读窗，GUI 拒绝迟到结果。
     */
    Q_INVOKABLE void nextWindow();

    /*
     * 功能：保存一章标题/区间并同步相邻共享边界
     * 参数：index：输入，有效零基章节索引；title：输入，去首尾空白后的标题；start_codepoint：输入，绝对 Unicode 码点起点；end_codepoint：输入，绝对码点不含末尾；负数当前按0交服务验证，完整布局须连续覆盖原文。
     * 返回：无。
     * 失败：busy/无来源/无效索引忽略；布局校验、预期章节修订冲突或存储失败显示 errorText。
     * 副作用：后台按 chapter_revision 保存章节布局，原文和证据绝对位置不改；GUI 恢复章节 ID/窗口并发 changed。
     * 线程与生命周期：GUI 捕获来源和世界代次；线程池写元数据，GUI 按代次及来源回填，过期回调只补刷新。
     */
    Q_INVOKABLE void saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint);

signals:
    /*
     * 功能：通知界面重读来源、章节、证据与窗口属性
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收者结果。
     * 副作用：按 Qt 连接调用槽，不直接写库。
     * 线程与生命周期：GUI 发出。
     */
    void changed();
    /*
     * 功能：通知当前世界来源导入及关联已成功
     * 参数：无。
     * 返回：无。
     * 失败：导入失败或旧世界回调不发送；后续目录读取失败仍可收到导入成功通知。
     * 副作用：接收方可刷新来源与项目目录。
     * 线程与生命周期：GUI 在导入成功后发出，接收方由 Qt 管理。
     */
    void sourceImported();

private:

    /*
     * 功能：在 GUI 应用当前世界来源查询并恢复阅读上下文
     * 参数：result：输入，拥有来源元数据或错误的结果；keep_id：输入，默认空，期望选择来源 ID。
     * 返回：无。
     * 失败：失败写 errorText 并补刷新；过滤非当前世界来源，不将错误当空成功。
     * 副作用：替换目录，恢复同一来源的稳定章节和绝对窗口，触发按需预览。
     * 线程与生命周期：GUI 调用；所有参数按值接收，不保存查询连接。
     */
    void applyDocuments(xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result,
                        QString keep_id = {});

    /*
     * 功能：重建所选来源章节并恢复阅读窗口
     * 参数：index：输入，有效零基来源索引；chapter_id：输入，默认空，同步借用稳定章节 ID；preferred_window_start：输入，可空绝对码点，默认无，章节 ID 失效时据此重新定位。
     * 返回：无。
     * 失败：索引无效忽略；无章清空预览；位置超出章尾退到有效末窗。
     * 副作用：使旧窗口失效，清证据快照并异步读取新窗。
     * 线程与生命周期：GUI 调用；chapter_id 仅同步查找，后台捕获来源 ID 与边界副本。
     */
    void selectSourceAt(int index, const std::string& chapter_id = {},
                        std::optional<std::size_t> preferred_window_start = std::nullopt);

    /*
     * 功能：补做忙碌期间遗漏的来源刷新
     * 参数：无。
     * 返回：无。
     * 失败：无补刷新标志时忽略；新查询失败由 refresh 通知。
     * 副作用：消费标志并调用 refresh，可再次延迟。
     * 线程与生命周期：仅 GUI 调用，投递后台查询，不等待。
     */
    void refreshAfterWorldChange();

    /*
     * 功能：后台读取原文窗口及来源证据
     * 参数：source_id：输入，按值捕获来源 ID；start_codepoint：输入，绝对码点起点；end_codepoint：输入，绝对码点不含末尾，通常窗口不超50000码点。
     * 返回：无。
     * 失败：正文失败写 errorText 并清待高亮；证据失败返回空证据；异常转中文错误。
     * 副作用：只读资产/库；GUI 按窗口/世界代次、来源及边界更新正文、证据和高亮。
     * 线程与生命周期：线程池独立服务读取，GUI 回填；窗口或来源改变即丢弃结果，对象销毁不通知。
     */
    void loadPreview(const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint);

    /*
     * 功能：切换窗口并使同边界旧请求也失效
     * 参数：start_codepoint：输入，绝对码点起点；end_codepoint：输入，绝对码点不含末尾，须不小于起点。
     * 返回：无。
     * 失败：无来源或逆序忽略；文件错误由 loadPreview 通知。
     * 副作用：递增预览代次，设置加载提示，清高亮并投递读窗。
     * 线程与生命周期：GUI 发起，线程池读取，GUI 按代次应用。
     */
    void showWindow(std::size_t start_codepoint, std::size_t end_codepoint);

    /*
     * 功能：将窗口内绝对码点选区映射为 UTF-16 高亮
     * 参数：start_codepoint：输入，绝对码点起点；end_codepoint：输入，绝对码点不含末尾。
     * 返回：无。
     * 失败：读取中、逆序或范围超窗口时保留零高亮；补充平面字符按两个 UTF-16 码元处理。
     * 副作用：清并计算两个高亮偏移，不读写原文资产，不在此发信号。
     * 线程与生命周期：GUI 同步调用，转换 lambda 只在本函数期间借用预览文本。
     */
    void highlightEvidenceRange(std::size_t start_codepoint, std::size_t end_codepoint);

    /* 来源库本机路径；构造确定，实例持有，后台复制。 */
    std::filesystem::path database_path_;
    /* 当前世界来源元数据及章节目录副本，初始空；GUI 替换，不持有整书正文。 */
    std::vector<xuyan::domain::SourceDocument> documents_;
    /* 与 documents_ 顺序一致的来源摘要，初始空；GUI 构造，界面读取。 */
    QVariantList source_items_;
    /* 当前世界稳定 ID，初始空；GUI 切换，后台复制以校验归属。 */
    QString world_id_;
    /* 世界代次，初始0；每次世界切换递增，GUI 隔离 A→B→A 旧回调。 */
    std::uint64_t world_generation_{0};
    /* 窗口请求代次，初始0；切世界、来源、窗口递增，GUI 排除迟到读窗。 */
    std::uint64_t preview_generation_{0};
    /* 延迟选择的来源 ID，初始空；按 ID 选择设置，手动选择或成功应用时清理。 */
    QString desired_source_id_;
    /* 操作期间遗漏刷新标志，初始false；GUI 在忙碌切换置位，完成后消费。 */
    bool refresh_after_world_change_{false};
    /* 当前来源章节标题及绝对码点半开范围摘要，初始空；GUI 重建。 */
    QVariantList chapter_items_;
    /* 当前来源证据值副本，初始空；GUI 读窗/创建回调更新，无额外分页上限。 */
    std::vector<xuyan::domain::EvidenceReference> evidence_;
    /* 与 evidence_ 对齐的界面摘要，初始空；保留原文及协议值，GUI 构造。 */
    QVariantList evidence_items_;
    /* 来源列表零基选择下标，初始-1；GUI 选择/加载更新。 */
    int selected_index_{-1};
    /* 来源元数据操作在途标志，初始false；不代表独立 preview_loading_，GUI 更新。 */
    bool busy_{false};
    /* 中文来源失败提示，初始空；GUI 清理和回填写入。 */
    QString error_text_;
    /* 仅当前原文窗口或中文加载提示，初始空；最多50000码点正文，GUI 回填。 */
    QString preview_text_;
    /* 导入、证据、章节操作中文说明，初始空；GUI 更新。 */
    QString status_text_;
    /* 窗口内 UTF-16 高亮起点码元偏移，初始0；GUI 转换，QML 读取。 */
    int highlight_start_{0};
    /* 窗口内 UTF-16 高亮不含末尾偏移，初始0；与起点共同定义半开选区。 */
    int highlight_end_{0};
    /* 所选来源章节零基下标，初始-1；GUI 选择或证据定位更新。 */
    int selected_chapter_index_{-1};
    /* 窗口在完整原文内的 Unicode 码点起点，初始0；GUI 切窗设置。 */
    std::size_t preview_start_codepoint_{0};
    /* 窗口绝对码点不含末尾，初始0；不可当成字节或 UTF-16 长度。 */
    std::size_t preview_end_codepoint_{0};
    /* 原文窗口在途标志，初始false；GUI 发起置位，仅有效读窗回调复位。 */
    bool preview_loading_{false};
    /* 待原文加载后高亮的绝对码点半开范围，初始无；GUI 证据定位设置，换窗/完成消费。 */
    std::optional<std::pair<std::size_t, std::size_t>> pending_evidence_range_;
    /* 每次原文窗口最大长度，固定50000 Unicode 码点；限制 QML 正文驻留。 */
    static constexpr std::size_t preview_window_size_{50000};
};
