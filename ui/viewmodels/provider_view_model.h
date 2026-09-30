#pragma once

#include <QObject>
#include <QVariantList>

#include <filesystem>

/*
 * 职责：桥接模型连接配置、系统凭据及用户显式发起的探测/结构化自检；目录刷新不联网。
 * 资源与线程：持有工作区路径和界面值快照；公开接口及状态读写限 GUI 线程。
 * 生命周期：由 parent 或组装方管理；后台使用独立服务连接，结果排队回 GUI。
 * 销毁边界：全局线程池操作不因页面销毁自动取消；QPointer 失效时放弃通知，已提交写入不回滚。
 */
class ProviderViewModel final : public QObject {
    Q_OBJECT
    /* 属性：读取连接配置摘要；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    /* 属性：查询连接操作是否在途；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /* 属性：读取模型连接操作状态；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    /* 属性：读取连接中文错误提示；changed 通知重读；默认值及空值见对应访问器和成员。 */
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    /*
     * 功能：绑定连接库并异步加载配置
     * 参数：database_path：输入，本机数据库路径，实例持有；parent：可空 Qt 所有者，默认空。
     * 返回：完成初始化并排队刷新。
     * 失败：路径分配异常传播；查询失败显示中文提示。
     * 副作用：读取连接及凭据是否配置，不发送请求。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    explicit ProviderViewModel(std::filesystem::path database_path, QObject* parent = nullptr);
    /*
     * 功能：读取连接配置摘要
     * 参数：无。
     * 返回：列表副本，含端点、模型及 credentialConfigured 布尔值，不含密钥。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QVariantList connections() const { return connections_; }
    /*
     * 功能：查询连接操作是否在途
     * 参数：无。
     * 返回：true 禁止重复操作，默认false。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    bool busy() const noexcept { return busy_; }
    /*
     * 功能：读取模型连接操作状态
     * 参数：无。
     * 返回：中文提示，可为空；真实模型 ID 保持原值。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString statusText() const { return status_text_; }
    /*
     * 功能：读取连接中文错误提示
     * 参数：无。
     * 返回：空表示无当前失败，不含响应正文或异常细节。
     * 失败：仅值复制或分配异常传播；不执行业务校验。
     * 副作用：只读当前 GUI 快照，不访问数据库、文件或网络。
     * 线程与生命周期：仅 GUI 线程同步调用；返回值独立持有，不保存调用方引用。
     */
    QString errorText() const { return error_text_; }

    /*
     * 功能：后台加载连接和系统凭据配置状态
     * 参数：无。
     * 返回：无。
     * 失败：busy 时忽略；查询失败经 errorText 通知。
     * 副作用：替换连接摘要，发 changed；不发送网络请求。
     * 线程与生命周期：GUI 发起，线程池构造服务及凭据适配器，GUI 回填；销毁后不通知。
     */
    Q_INVOKABLE void refresh();
    /*
     * 功能：保存连接配置及可选系统密钥
     * 参数：id：稳定连接 ID，空交给服务生成；revision：预期修订，0用于新建；name：显示名；kind：内部协议种类；endpoint：接口 URL；model：真实模型标识；data_policy：数据策略协议值；enabled：是否启用；api_key：可选密钥，空表示不替换已有凭据；均为输入值。
     * 返回：无。
     * 失败：busy 时忽略；配置校验、修订冲突、凭据或数据库失败写 errorText。
     * 副作用：系统凭据设施保存非空密钥，数据库只存引用；后台重读列表，GUI finish 通知。
     * 线程与生命周期：GUI 捕获值，线程池同步保存再释放密钥副本；QPointer 失效仅抑制通知，不回滚已写配置。
     */
    Q_INVOKABLE void saveConnection(QString id, int revision, QString name, QString kind,
                                    QString endpoint, QString model, QString data_policy,
                                    bool enabled, QString api_key);
    /*
     * 功能：按预期修订移除连接及对应系统凭据
     * 参数：id：输入，非空稳定连接 ID；revision：输入，当前记录预期修订。
     * 返回：无。
     * 失败：busy 或空 ID 忽略；冲突/凭据/存储失败显示中文提示。
     * 副作用：后台删除配置和凭据、重读目录，GUI 通知。
     * 线程与生命周期：线程池独立服务执行；GUI finish 回填，销毁不撤销已完成删除。
     */
    Q_INVOKABLE void removeConnection(QString id, int revision);
    /*
     * 功能：显式请求接口模型目录以验证端点可达性
     * 参数：id：输入，非空连接 ID，后台从库读取协议和端点。
     * 返回：无。
     * 失败：busy 或空 ID 忽略；缺凭据、10秒超时、认证/网络/HTTP失败显示中文分类提示。
     * 副作用：发送一次模型目录 GET，不发送小说；未修改连接配置，清理工作线程密钥副本。
     * 线程与生命周期：线程池创建网络管理器、回复与局部事件循环；10秒定时中止在途回复，GUI 仅接收状态。对象销毁不自动中断已发请求。
     */
    Q_INVOKABLE void probeConnection(QString id);
    /*
     * 功能：显式执行真实模型结构化生成自检
     * 参数：id：输入，非空连接 ID；必须由用户明确触发，可能计费。
     * 返回：无。
     * 失败：busy 或空 ID 忽略；45秒请求失败、非完成状态或 JSON 无效显示中文提示。
     * 副作用：读取系统凭据并发送自检，不推进推演分支；GUI 显示模型 ID、词元量及毫秒耗时。
     * 线程与生命周期：线程池构造传输，GUI 回填；销毁时放弃通知，不保证厂商请求撤销或退款。
     */
    Q_INVOKABLE void testStructuredGeneration(QString id);

signals:
    /*
     * 功能：通知界面重读连接、忙碌及提示属性
     * 参数：无。
     * 返回：无。
     * 失败：不返回接收者结果。
     * 副作用：调用已连接 Qt 槽。
     * 线程与生命周期：GUI 发出，Qt 管理接收者连接生命周期。
     */
    void changed();

private:
    /*
     * 功能：在 GUI 应用后台连接操作结果
     * 参数：values：输入，按值接收新连接摘要；status：输入，可空中文状态；error：输入，可空安全中文错误。
     * 返回：无。
     * 失败：分配异常传播；不验证远程能力或重试。
     * 副作用：替换目录和提示，清 busy，发 changed。
     * 线程与生命周期：仅 GUI 调用；不保留参数引用，不持有密钥。
     */
    void finish(QVariantList values, QString status, QString error);
    /* 连接配置本机数据库路径；构造确定，实例持有，后台复制。 */
    std::filesystem::path database_path_;
    /* 连接摘要列表，默认空；后台值回填 GUI，含凭据配置布尔值，不持有密钥。 */
    QVariantList connections_;
    /* 连接操作在途标志，默认false；GUI 入口置位，finish 或完成回调复位。 */
    bool busy_{false};
    /* 操作中文状态，初始说明系统凭据保护；GUI 更新，界面读取。 */
    QString status_text_{QStringLiteral("凭据由系统凭据管理器保护")};
    /* 中文安全错误，初始空；不显示底层异常或原始响应，GUI 写入。 */
    QString error_text_;
};
