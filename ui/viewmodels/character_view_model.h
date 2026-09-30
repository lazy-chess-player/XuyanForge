#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>

/*
 * 职责：维护本地人物卡选择及版本化编辑；不向模型发送人物内容。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class CharacterViewModel final : public QObject {
    Q_OBJECT
    /* 属性：查询人物卡加载或保存是否在途；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：读取人物卡中文错误提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    /* 属性：读取人物卡摘要目录；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList cardItems READ cardItems NOTIFY changed)
    /* 属性：读取人物卡选择位置；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    /* 属性：读取当前人物卡 ID；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    /* 属性：读取当前人物卡预期版本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(int selectedVersion READ selectedVersion NOTIFY changed)
    /* 属性：读取当前人物卡名称；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString name READ name NOTIFY changed)
    /* 属性：读取当前人物卡摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    /* 属性：读取人物卡价值观列表的表单文本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString values READ values NOTIFY changed)
    /* 属性：读取人物卡性格特征表单文本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString traits READ traits NOTIFY changed)
    /* 属性：读取人物卡长期目标；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString longGoal READ longGoal NOTIFY changed)
    /* 属性：读取人物卡短期目标；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString shortGoal READ shortGoal NOTIFY changed)
    /* 属性：读取人物卡说话风格；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString speechStyle READ speechStyle NOTIFY changed)
    /* 属性：读取人物卡能力 JSON；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString abilities READ abilities NOTIFY changed)
    /* 属性：读取人物卡装备列表文本；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString equipment READ equipment NOTIFY changed)
    /* 属性：读取人物卡背景；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString background READ background NOTIFY changed)
    /* 属性：读取人物卡私密笔记；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString privateNotes READ privateNotes NOTIFY changed)

public:
    /*
     * 功能：绑定人物卡库并异步加载目录
     * 参数：database_path：输入，实例持有的本机数据库路径；parent：可空 Qt 所有者，默认空。
     * 返回：完成成员初始化并开始加载。
     * 失败：路径分配异常传播；后台失败经 errorText 通知。
     * 副作用：读取人物卡，不创建样例。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit CharacterViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /*
     * 功能：查询人物卡加载或保存是否在途
     * 参数：无。
     * 返回：true 表示禁止重复操作，初始false。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }
    /*
     * 功能：读取人物卡中文错误提示
     * 参数：无。
     * 返回：空表示无当前失败，不含原始异常。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }
    /*
     * 功能：读取人物卡摘要目录
     * 参数：无。
     * 返回：含 ID、名称、摘要、版本的列表副本；未加载为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList cardItems() const { return items_; }
    /*
     * 功能：读取人物卡选择位置
     * 参数：无。
     * 返回：零基下标；-1 为新建状态。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedIndex() const noexcept { return selected_index_; }
    /*
     * 功能：读取当前人物卡 ID
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString selectedId() const;
    /*
     * 功能：读取当前人物卡预期版本
     * 参数：无。
     * 返回：无选择为0，保存时据此区分新建与修订。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    int selectedVersion() const noexcept;
    /*
     * 功能：读取当前人物卡名称
     * 参数：无。
     * 返回：无选择为空，不读取表单草稿。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString name() const;
    /*
     * 功能：读取当前人物卡摘要
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString summary() const;
    /*
     * 功能：读取人物卡价值观列表的表单文本
     * 参数：无。
     * 返回：中文逗号连接各项，无选择为空，不翻译用户内容。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString values() const;
    /*
     * 功能：读取人物卡性格特征表单文本
     * 参数：无。
     * 返回：中文逗号连接各项，无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString traits() const;
    /*
     * 功能：读取人物卡长期目标
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString longGoal() const;
    /*
     * 功能：读取人物卡短期目标
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString shortGoal() const;
    /*
     * 功能：读取人物卡说话风格
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString speechStyle() const;
    /*
     * 功能：读取人物卡能力 JSON
     * 参数：无。
     * 返回：无选择返回 []，不解析能力结构。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString abilities() const;
    /*
     * 功能：读取人物卡装备列表文本
     * 参数：无。
     * 返回：中文逗号连接各项，无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString equipment() const;
    /*
     * 功能：读取人物卡背景
     * 参数：无。
     * 返回：无选择为空。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString background() const;
    /*
     * 功能：读取人物卡私密笔记
     * 参数：无。
     * 返回：无选择为空；仅本地内存访问，不导出。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString privateNotes() const;

    /*
     * 功能：重载人物卡并尽量保留选择
     * 参数：无。
     * 返回：无。
     * 失败：busy 时忽略；读取失败显示 errorText。
     * 副作用：异步查询目录，GUI 更新快照和 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void refresh();
    /*
     * 功能：选择目录人物卡
     * 参数：index：输入，零基下标，须在 cards_ 范围。
     * 返回：无。
     * 失败：越界忽略。
     * 副作用：修改选择，清错误并发 changed。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void selectCard(int index);
    /*
     * 功能：切换到新建人物卡状态
     * 参数：无。
     * 返回：无。
     * 失败：无业务失败。
     * 副作用：设选择为-1，清错误并发 changed，不删除人物卡。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    Q_INVOKABLE void clearSelection();
    /*
     * 功能：按当前人物卡版本保存表单或创建新卡
     * 参数：name：名称，去首尾空白；summary：摘要；values：逗号分隔价值观；traits：逗号分隔特征；long_goal：长期目标；short_goal：短期目标；speech_style：说话风格；abilities：能力 JSON，空为 []；equipment：逗号分隔装备；background：背景；private_notes：私密笔记；均为输入值，字段合法性由服务校验。
     * 返回：无。
     * 失败：busy 时忽略；字段无效、版本冲突或存储失败写 errorText。
     * 副作用：后台持久化人物卡版本和命令；成功重载并选中新卡，不联网。
     * 线程与生命周期：GUI 复制表单及当前卡，线程池独立连接保存；GUI 回填。销毁后写入可完成但不通知。
     */
    Q_INVOKABLE void saveCard(QString name, QString summary, QString values, QString traits,
                              QString long_goal, QString short_goal, QString speech_style,
                              QString abilities, QString equipment, QString background,
                              QString private_notes);

signals:
    /*
     * 功能：通知人物卡属性发生变化
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收者结果。
     * 副作用：按 Qt 连接调用槽。
     * 线程与生命周期：GUI 发出，Qt 管理接收者生命周期。
     */
    void changed();

private:
    /*
     * 功能：将领域多值列表转为表单文本
     * 参数：values：输入，调用期间借用的 UTF-8 项目列表。
     * 返回：中文逗号连接的文本；空列表返回空串。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static QString join(const std::vector<std::string>& values);
    /*
     * 功能：解析人物表单多值字段
     * 参数：value：输入，按值接收，中英文逗号分隔；跳过原始空分段，逐项去首尾空白。
     * 返回：UTF-8 项目副本；纯空白分段可留下空项，后续由服务校验。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    static std::vector<std::string> split(QString value);
    /*
     * 功能：后台读取人物卡并恢复选择
     * 参数：keep_id：输入，默认空；存在则恢复该卡，否则选择首卡，无记录选择-1。
     * 返回：无。
     * 失败：busy 时忽略；读取失败保留原快照并设置中文 errorText。
     * 副作用：GUI 置 busy，线程池读库，GUI 替换目录和选择，发 changed。
     * 线程与生命周期：使用 QPointer；对象销毁时不回填，不取消已开始查询。
     */
    void loadCards(QString keep_id = {});

    /* 人物卡库本机路径；构造确定，实例持有，后台复制。 */
    std::filesystem::path database_path_;
    /* 人物卡领域副本目录，默认空；成功加载在 GUI 替换，保存复制选中项。 */
    std::vector<xuyan::domain::CharacterBlueprint> cards_;
    /* 人物卡轻量摘要列表，默认空；与 cards_ 顺序一致，GUI 生成。 */
    QVariantList items_;
    /* 零基选择下标，默认-1；-1 表示新建，加载和 GUI 选择更新。 */
    int selected_index_{-1};
    /* 加载或保存在途标志，默认false；GUI 开始置位，完成回调复位。 */
    bool busy_{false};
    /* 人物卡中文失败提示，默认空；GUI 入口清理和结果回填写入。 */
    QString error_text_;
};
