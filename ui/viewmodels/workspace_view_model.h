#pragma once

#include "xuyan/domain/world_entity.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <filesystem>
#include <functional>

/*
 * 职责：维护世界条目分页、修订编辑、可恢复草稿及显式合并/拆分；当前分页查询仍是工作区范围。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class WorkspaceViewModel final : public QObject {
    Q_OBJECT
    /* 属性：查询条目操作是否在途；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：读取条目中文失败提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：读取当前工作区筛选页摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList entityItems READ entityItems NOTIFY changed)
    /* 属性：读取工作区当前搜索匹配总数；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int total READ total NOTIFY changed)
    /* 属性：读取当前页选择位置；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    /* 属性：读取选中已存条目的稳定 ID；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    /* 属性：读取编辑名称，优先使用有效草稿；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
    /* 属性：读取编辑类型，优先使用有效草稿；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY changed)
    /* 属性：读取编辑描述，优先使用有效草稿；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedDescription READ selectedDescription NOTIFY changed)
    /* 属性：读取编辑别名表单文本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedAliases READ selectedAliases NOTIFY changed)
    /* 属性：读取编辑标签表单文本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedTags READ selectedTags NOTIFY changed)
    /* 属性：读取编辑属性 JSON；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedAttributes READ selectedAttributes NOTIFY changed)
    /* 属性：读取选中记录的预期修订；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedRevision READ selectedRevision NOTIFY changed)
    /* 属性：判断当前分页是否有前页；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool hasPreviousPage READ hasPreviousPage NOTIFY changed)
    /* 属性：判断匹配总数是否超过本页末尾；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY changed)
    /* 属性：格式化当前分页计数；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString pageText READ pageText NOTIFY changed)
    /* 属性：判断编辑草稿是否有效可恢复；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool draftAvailable READ draftAvailable NOTIFY changed)
    /* 属性：读取条目写入及合并中文结果提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    /*
     * 功能：绑定条目库并异步读取首个分页
     * 参数：database_path：输入，实例持有的本机数据库路径；parent：可空 Qt 所有者，默认空。
     * 返回：完成初始化并发起首查。
     * 失败：构造分配异常传播，后台失败经 errorText 提示。
     * 副作用：可由服务初始化库；读取25项首屏，不创建资料。
     * 线程与生命周期：GUI 构造，线程池查询，GUI 应用；当前未实现页请求代次隔离。
     */
    explicit WorkspaceViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /*
     * 功能：查询条目操作是否在途
     * 参数：无。
     * 返回：true 禁止再次投递读取/写入，默认false。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }
    /*
     * 功能：读取条目中文失败提示
     * 参数：无。
     * 返回：空表示无当前错误，不含原始异常。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }
    /*
     * 功能：读取当前工作区筛选页摘要
     * 参数：无。
     * 返回：最多25项的 ID、名称、类型、描述、修订列表副本，初始空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList entityItems() const { return items_; }
    /*
     * 功能：读取工作区当前搜索匹配总数
     * 参数：无。
     * 返回：条目个数，初始0；当前查询不按 world_id_ 过滤。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int total() const noexcept { return total_; }
    /*
     * 功能：读取当前页选择位置
     * 参数：无。
     * 返回：零基下标；-1 表示新建状态。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedIndex() const noexcept { return selected_index_; }
    /*
     * 功能：读取选中已存条目的稳定 ID
     * 参数：无。
     * 返回：无选择为空；不从草稿取 ID。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedId() const;
    /*
     * 功能：读取编辑名称，优先使用有效草稿
     * 参数：无。
     * 返回：有效草稿字段，否则记录名称；无选择无草稿为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedName() const;
    /*
     * 功能：读取编辑类型，优先使用有效草稿
     * 参数：无。
     * 返回：内部类型协议值；新建无草稿为 other，不直接显示为标签。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedKind() const;
    /*
     * 功能：读取编辑描述，优先使用有效草稿
     * 参数：无。
     * 返回：草稿或记录原文；无选择无草稿为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedDescription() const;
    /*
     * 功能：读取编辑别名表单文本
     * 参数：无。
     * 返回：有效草稿优先，否则中文逗号连接领域别名；新建为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedAliases() const;
    /*
     * 功能：读取编辑标签表单文本
     * 参数：无。
     * 返回：有效草稿优先，否则中文逗号连接领域标签；新建为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedTags() const;
    /*
     * 功能：读取编辑属性 JSON
     * 参数：无。
     * 返回：有效草稿优先，否则记录 JSON；新建为 {}，不在访问器解析。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedAttributes() const;
    /*
     * 功能：读取选中记录的预期修订
     * 参数：无。
     * 返回：记录修订；新建为0，不取草稿中的旧修订覆盖记录。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedRevision() const noexcept;
    /*
     * 功能：判断当前分页是否有前页
     * 参数：无。
     * 返回：偏移大于0为true；busy 独立控制按钮执行。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool hasPreviousPage() const noexcept { return offset_ > 0; }
    /*
     * 功能：判断匹配总数是否超过本页末尾
     * 参数：无。
     * 返回：当前偏移加本页数量小于总数为true；busy 独立控制执行。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool hasNextPage() const noexcept { return offset_ + static_cast<int>(entities_.size()) < total_; }
    /*
     * 功能：格式化当前分页计数
     * 参数：无。
     * 返回：空为0 / 0；非空为首项–末项 / 总数，均为条目序号。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString pageText() const;
    /*
     * 功能：判断编辑草稿是否有效可恢复
     * 参数：无。
     * 返回：active_draft_ 非空为true；修订不一致的磁盘草稿不恢复。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool draftAvailable() const noexcept { return !active_draft_.isEmpty(); }
    /*
     * 功能：读取条目写入及合并中文结果提示
     * 参数：无。
     * 返回：初始空；成功回调更新。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }

    /*
     * 功能：更新搜索条件并读取首页
     * 参数：query：输入，可空检索词，默认空表示无关键词；kind：输入，可空类型协议值，默认空表示不限类型。
     * 返回：无。
     * 失败：busy 时底层 loadPage 忽略；条件及偏移仍会更新，当前没有排队补刷保证。
     * 副作用：设置搜索条件、偏移0并尽量保留选择，后台读取工作区范围。
     * 线程与生命周期：GUI 发起，线程池读库，GUI 应用；当前未有世界/页请求代次隔离。
     */
    Q_INVOKABLE void refresh(QString query = {}, QString kind = {});
    /*
     * 功能：设置后续新建条目的目标世界
     * 参数：world_id：输入，可空稳定 ID；空时新建被拒绝。
     * 返回：无。
     * 失败：不验证存在性，服务在写入时校验。
     * 副作用：仅保存 world_id_；不清页、不过滤已有工作区列表。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void setWorldId(QString world_id) { world_id_ = std::move(world_id); }
    /*
     * 功能：选择本页条目并恢复对应修订草稿
     * 参数：index：输入，本页零基有效下标。
     * 返回：无。
     * 失败：越界忽略；设置读取错误当前不向界面报告。
     * 副作用：更新选择和 active_draft_，清错误，发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void selectEntity(int index);
    /*
     * 功能：从表单创建当前目标世界条目
     * 参数：name：输入，名称，去首尾空白；kind：输入，条目类型协议值；description：输入，描述；aliases：输入，中英文逗号分隔别名；tags：输入，同样分隔标签；attributes：输入，完整属性 JSON，空按 {}；合法性由应用服务校验。
     * 返回：无。
     * 失败：无世界显示错误；busy 时 runEntity 忽略；字段或存储失败保留草稿并提示。
     * 副作用：线程池创建记录及幂等命令；成功清当前草稿并重载页，不发送网络。
     * 线程与生命周期：GUI 捕获表单/世界值，后台独立服务写入，GUI 更新；当前回调没有世界代次保护。
     */
    Q_INVOKABLE void createEntity(QString name, QString kind, QString description,
                                  QString aliases, QString tags, QString attributes);
    /*
     * 功能：按选中记录修订保存表单，无选择时转新建
     * 参数：name：输入，名称，去首尾空白；kind：输入，条目类型协议值；description：输入，描述；aliases：输入，中英文逗号分隔别名；tags：输入，同样分隔标签；attributes：输入，完整属性 JSON，空按 {}；合法性由应用服务校验。
     * 返回：无。
     * 失败：busy 时忽略；修订冲突/字段/存储失败显示提示并保留草稿。
     * 副作用：保留原 ID、世界及审核状态，后台保存命令；成功清当前草稿并读页。
     * 线程与生命周期：线程池同步写入，GUI 回填；当前选择改变不会作废回调，未覆盖风险交主代理验证。
     */
    Q_INVOKABLE void saveSelected(QString name, QString kind, QString description,
                                  QString aliases, QString tags, QString attributes);
    /*
     * 功能：按当前预期修订软删除条目
     * 参数：无。
     * 返回：无。
     * 失败：无选择或 busy 忽略；修订/存储失败提示。
     * 副作用：后台写软删除和修订历史，成功清草稿并重读列表，不物理清库。
     * 线程与生命周期：GUI 捕获 ID/修订，线程池执行，GUI 回填；销毁不回滚已提交删除。
     */
    Q_INVOKABLE void deleteSelected();
    /*
     * 功能：选择新建编辑状态并恢复新建草稿
     * 参数：无。
     * 返回：无。
     * 失败：设置读失败当前未报告。
     * 副作用：选择-1，加载 __new__ 草稿，清错误并发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void clearSelection();
    /*
     * 功能：读取上一条目页
     * 参数：无。
     * 返回：无。
     * 失败：busy 或首页忽略；读库失败显示提示。
     * 副作用：减偏移，按现有筛选读页并恢复对应草稿。
     * 线程与生命周期：线程池查询，GUI 更新；不等待完成。
     */
    Q_INVOKABLE void previousPage();
    /*
     * 功能：读取下一条目页
     * 参数：无。
     * 返回：无。
     * 失败：busy 或无后页忽略；读库失败显示提示。
     * 副作用：增偏移并后台读页。
     * 线程与生命周期：线程池查询，GUI 更新；不等待完成。
     */
    Q_INVOKABLE void nextPage();
    /*
     * 功能：持久化当前编辑表单但不提交领域记录
     * 参数：name：输入，名称，去首尾空白；kind：输入，条目类型协议值；description：输入，描述；aliases：输入，中英文逗号分隔别名；tags：输入，同样分隔标签；attributes：输入，完整属性 JSON，空按 {}；合法性由应用服务校验。
     * 返回：无。
     * 失败：相同草稿忽略；与记录完全相同则删除草稿；设置同步错误当前未报告。
     * 副作用：设置保存字段、选中修订与最后编辑键，更新内存草稿，发 changed；不联网。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void saveDraft(QString name, QString kind, QString description,
                               QString aliases, QString tags, QString attributes);
    /*
     * 功能：丢弃当前条目的本地编辑草稿
     * 参数：无。
     * 返回：无。
     * 失败：设置同步错误当前未报告。
     * 副作用：删除当前草稿和匹配的最后编辑键，清内存并发 changed；不改记录。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void discardDraft();
    /*
     * 功能：按双方修订显式合并选中条目到目标
     * 参数：target_id：输入，去空白后的稳定目标 ID，不能为空，不能等于源 ID。
     * 返回：无。
     * 失败：busy/无选择/空 ID 忽略；有草稿或相同目标报错；服务校验类型/世界/修订及引用冲突。
     * 副作用：后台原子合并条目和引用，GUI 显示可拆分记录 ID并回首页选择目标。
     * 线程与生命周期：GUI 捕获源 ID/修订；线程池读取目标当前修订后提交；GUI 回填，销毁不回滚事务。
     */
    Q_INVOKABLE void mergeSelectedInto(QString target_id);
    /*
     * 功能：按显式合并记录恢复双方条目及引用
     * 参数：merge_id：输入，去空白后非空稳定合并记录 ID。
     * 返回：无。
     * 失败：busy/空 ID 忽略；后续编辑保护、记录缺失或存储失败经 errorText 通知。
     * 副作用：后台拆分事务及命令记录，GUI 回首页选择源条目。
     * 线程与生命周期：线程池执行，GUI 回填；销毁不回滚已提交拆分。
     */
    Q_INVOKABLE void splitMerge(QString merge_id);

signals:
    /*
     * 功能：通知界面重读条目页、草稿或状态
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收者结果。
     * 副作用：调用 Qt 连接槽。
     * 线程与生命周期：GUI 发出。
     */
    void changed();

private:
    using PageResult = xuyan::domain::Result<xuyan::domain::EntityPage>;
    using EntityResult = xuyan::domain::Result<xuyan::domain::WorldEntity>;

    /*
     * 功能：生成一次条目操作的幂等命令标识
     * 参数：无。
     * 返回：无花括号的随机 UUID 字符串；每次生成不同。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：使用系统随机源，不写库。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static QString commandId();
    /*
     * 功能：将逗号分隔表单转换为领域列表
     * 参数：value：输入，按值接收，支持中英文逗号；逐项去空白。
     * 返回：UTF-8 项目副本，跳过原始空段；纯空白段可留下空项，由服务校验。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static std::vector<std::string> parseList(QString value);
    /*
     * 功能：将领域条目转为轻量列表摘要
     * 参数：entity：输入，调用期间借用条目。
     * 返回：ID、名称、类型、描述、修订的独立映射；不包含大属性。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static QVariantMap toMap(const xuyan::domain::WorldEntity& entity);
    /*
     * 功能：将编辑表单组装成尚未提交的领域条目
     * 参数：name：输入，名称，去首尾空白；kind：输入，条目类型协议值；description：输入，描述；aliases：输入，中英文逗号分隔别名；tags：输入，同样分隔标签；attributes：输入，完整属性 JSON，空按 {}；合法性由应用服务校验。
     * 返回：领域值，尚无稳定 ID/世界归属；新建或保存入口补充，不在此校验 JSON。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static xuyan::domain::WorldEntity fromForm(QString name, QString kind, QString description,
                                                QString aliases, QString tags, QString attributes);
    /*
     * 功能：后台读取工作区筛选页并恢复选择
     * 参数：query：输入，关键词，空不限；kind：输入，类型协议值，空不限；keep_id：输入，默认空，期望选中的稳定 ID。
     * 返回：无。
     * 失败：busy 时忽略；服务异常转中文错误，读失败保留旧页。
     * 副作用：置 busy，线程池查询；GUI applyPage 更新总数、选择、草稿和 changed。
     * 线程与生命周期：按值捕获路径、偏移及页大小；QPointer 失效不回填，当前无代次校验。
     */
    void loadPage(QString query, QString kind, QString keep_id = {});
    /*
     * 功能：在 GUI 应用条目分页并恢复当前修订草稿
     * 参数：result：输入，拥有分页结果或错误；keep_id：输入，同步借用期望选择 ID，可空。
     * 返回：无。
     * 失败：失败保留旧页并提示；ID 不存在选首项，最后编辑为新建草稿时保持无选择。
     * 副作用：清 busy，更新领域页/摘要/总数及选择，读取设置草稿，发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void applyPage(PageResult result, const QString& keep_id);
    /*
     * 功能：投递一次返回条目的同步写入回调
     * 参数：work：输入，按值拥有回调，接收同步借用路径并返回 EntityResult，不保存路径引用；success_message：输入，成功中文提示。
     * 返回：无。
     * 失败：busy 时忽略；回调异常转安全中文错误；失败不清草稿。
     * 副作用：线程池执行写入，GUI 成功清当前草稿并重读页。
     * 线程与生命周期：回调只持表单值，不访问 GUI；线程池同步运行，GUI 回填，销毁不回滚写入。
     */
    void runEntity(std::function<EntityResult(const std::filesystem::path&)> work, QString success_message);
    /*
     * 功能：计算当前条目草稿设置键
     * 参数：无。
     * 返回：数据库路径摘要/条目 ID，或新建 __new__ 键，不直接暴露完整路径。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString draftKey() const;
    /*
     * 功能：恢复当前选择且修订相符的草稿
     * 参数：无。
     * 返回：无。
     * 失败：草稿修订与所选记录不一致不恢复；设置读取错误当前未报告。
     * 副作用：清 active_draft_ 后读取允许字段，不修改领域记录。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void loadActiveDraft();
    /*
     * 功能：移除当前选择草稿及对应最后编辑标记
     * 参数：无。
     * 返回：无。
     * 失败：设置同步错误当前未报告。
     * 副作用：删除设置键并同步，清内存草稿，不改数据库。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    void clearActiveDraft();

    /* 条目库本机路径，构造确定，实例持有；后台复制。 */
    std::filesystem::path database_path_;
    /* 当前工作区筛选页领域条目，初始空，最多25项；GUI 成功读页替换。 */
    std::vector<xuyan::domain::WorldEntity> entities_;
    /* 与 entities_ 对齐的轻量摘要，初始空；GUI 构造，不含大属性。 */
    QVariantList items_;
    /* 读取/写入在途标志，初始false；GUI 置位和完成回调复位。 */
    bool busy_{false};
    /* 中文安全错误，初始空；GUI 入口清理和失败回填写入。 */
    QString error_text_;
    /* 中文成功提示，初始空；GUI 写入/合并回调设置。 */
    QString status_text_;
    /* 最近搜索关键词，初始空表示不限；GUI refresh 更新，翻页复用。 */
    QString last_query_;
    /* 最近类型筛选协议值，初始空表示不限；GUI 修改，后台复制。 */
    QString last_kind_;
    /* 新建条目的目标世界 ID，初始空；GUI setWorldId 修改；当前读页不按此过滤。 */
    QString world_id_;
    /* 当前工作区筛选匹配条目个数，初始0；GUI 分页结果更新。 */
    int total_{0};
    /* 当前页零基条目偏移，初始0；GUI 搜索/翻页更新，不是字节。 */
    int offset_{0};
    /* 页容量，初始且当前固定使用25项；线程池查询复制。 */
    int page_size_{25};
    /* 当前页零基选择下标，初始-1 表示新建；GUI 选择/读页恢复。 */
    int selected_index_{-1};
    /* 当前有效修订的本地表单草稿映射，初始空；GUI 读取/保存设置，不是已提交事实。 */
    QVariantMap active_draft_;
};
