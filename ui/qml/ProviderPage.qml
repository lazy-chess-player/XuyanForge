import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 模型连接设置页：GUI 线程维护非敏感表单和瞬时密钥输入，连接保存、凭据存储及探测由外部模型负责。 */
Item {
    id: page
    /* 共享主题观察引用，无默认值，由窗口提供并覆盖页面生命周期。 */
    required property var uiTheme
    /* 正在编辑的连接稳定标识，初始空表示新建；选择或新建动作写入，保存和探测读取。 */
    property string selectedId: ""
    /* 当前连接的预期修订，无单位，初始 0 表示新建；选择动作写入，保存交给后端校验冲突。 */
    property int selectedRevision: 0

    /*
     * 功能：将所选连接的非敏感字段载入编辑框，并清除旧密钥输入。
     * 参数：connection 为输入的模型行，须含 id、revision、name、endpoint、model、kind；只观察不持有。
     * 返回：无。失败：未知 kind 显示中文回退并要求重新选择；缺失模型行导致访问异常向调用方传播。
     * 副作用：更新选中身份、预期修订和表单，清空密钥；不读取凭据、不保存、不探测网络。
     * 线程与生命周期：GUI 线程同步处理列表点击，不缓存行对象；页面销毁时输入控件一起释放。
     */
    function selectConnection(connection) {
        selectedId = connection.id
        selectedRevision = connection.revision
        nameField.text = connection.name
        endpointField.text = connection.endpoint
        modelField.text = connection.model
        kindChoice.currentIndex = kindChoice.indexOfValue(connection.kind)
        keyField.text = ""
    }

    /* 连接列表与设置区左右布局。 */
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        /* 已保存连接面板，宽度 280 像素，不显示密钥。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            /* 连接选择与新建入口的纵向布局。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                /* 已保存连接列表标题。 */
                Label { text: qsTr("已保存连接"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                /* 无连接时的中文说明，保持离线解析可用。 */
                Label { visible: providers.connections.length === 0; text: qsTr("尚未配置模型连接。离线解析不需要连接。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 连接元数据列表，只读绑定 connections。 */
                ListView {
                    id: connectionsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: providers.connections
                    /* 连接行代理，只展示连接名称，点击载入非敏感表单。 */
                    delegate: ItemDelegate {
                        /* 连接元数据行，由列表注入，无默认值；只观察身份、修订及非敏感设置，选择时复制入表单。 */
                        required property var modelData
                        width: connectionsList.width
                        text: modelData.name
                        /*
                         * 功能：点击连接行后将本行元数据交给 selectConnection。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：空或损坏行按该函数契约传播访问错误，未知服务类型保留未选状态。
                         * 副作用：更新非敏感表单并清空旧密钥，不访问网络。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: page.selectConnection(modelData)
                    }
                }
                /* 新建连接入口，重置身份、修订及输入框，不保存或访问服务。 */
                AppButton {
                    uiTheme: page.uiTheme; text: qsTr("新建连接")
                    /*
                     * 功能：清空所选连接身份、修订和输入，准备新建连接。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：仅本地属性赋值，异常由 QML 报告。
                     * 副作用：重置 selectedId、selectedRevision 及五个输入框，不保存连接或读取凭据。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: { page.selectedId = ""; page.selectedRevision = 0; nameField.text = ""; endpointField.text = ""; modelField.text = ""; keyField.text = "" }
                }
            }
        }
        /* 连接设置面板，占据右侧剩余空间。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            /* 设置字段滚动容器，用户输入仅随页面存活。 */
            Flickable {
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: form.implicitHeight + 16
                clip: true
                /* 非敏感配置和瞬时密钥的纵向表单。 */
                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: 10
                    /* 连接设置标题。 */
                    Label { text: qsTr("连接设置"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 用户自定义连接名称标签。 */
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    /* 名称输入，初始空，选择连接后载入模型名称。 */
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("自定义连接名称") }
                    /* 服务类型标签，显示中文，提交稳定协议值。 */
                    Label { text: qsTr("服务类型"); color: page.uiTheme.muted }
                    /* 服务类型选项，label/value 分离；未知类型不自动替换为第一项。 */
                    AppComboBox {
                        id: kindChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: [ {"label": qsTr("深度求索"), "value": "deepseek"},
                                 {"label": qsTr("兼容接口"), "value": "openai-compatible"},
                                 {"label": qsTr("本地接口"), "value": "local"} ]
                        textRole: "label"
                        valueRole: "value"
                        displayText: currentIndex < 0 ? qsTr("未知服务类型，请选择") : currentText
                    }
                    /* 接口地址标签，用户 URL 按原值保存。 */
                    Label { text: qsTr("接口地址"); color: page.uiTheme.muted }
                    /* 端点输入，初始空，不预置任何联网地址。 */
                    AppField { id: endpointField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("填写加密连接地址") }
                    /* 模型标识标签，真实标识不翻译。 */
                    Label { text: qsTr("模型名称"); color: page.uiTheme.muted }
                    /* 模型输入，初始空，不提供生产样例或默认模型。 */
                    AppField { id: modelField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("填写模型名称") }
                    /* 密钥输入语义说明，空值交给后端保持现有凭据。 */
                    Label { text: qsTr("密钥（留空则保持现有密钥）"); color: page.uiTheme.muted }
                    /* 密码输入框，初始空；不从系统回读密钥，保存提交后清空，页面销毁释放输入。 */
                    AppField { id: keyField; uiTheme: page.uiTheme; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("仅保存在系统凭据管理器") }
                    /* 保存与连接探测操作行。 */
                    RowLayout {
                        /* 保存连接入口，提交显式类型、预期修订及输入，系统凭据由模型管理。 */
                        AppButton {
                            uiTheme: page.uiTheme; text: qsTr("保存连接"); primary: true
                            enabled: kindChoice.currentIndex >= 0 && nameField.text.trim().length > 0 && endpointField.text.trim().length > 0 && modelField.text.trim().length > 0
                            /*
                             * 功能：保存当前连接，提交预期修订、类型协议、端点、真实模型标识及瞬时密钥。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：名称或配置无效、修订冲突、凭据或保存失败由 providers 报告。
                             * 副作用：请求保存及系统凭据更新；调用返回后清空 keyField，无论模型报告成功或失败均不回显密钥。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: {
                                providers.saveConnection(page.selectedId, page.selectedRevision, nameField.text,
                                                         kindChoice.currentValue, endpointField.text, modelField.text,
                                                         kindChoice.currentValue === "local" ? "local_only" : "remote_allowed",
                                                         true, keyField.text)
                                keyField.text = ""
                            }
                        }
                        /* 显式连接探测入口，只有所选身份非空才可调用；不发送小说。 */
                        AppButton { uiTheme: page.uiTheme; text: qsTr("测试连接"); enabled: page.selectedId.length > 0;
                            /*
                             * 功能：对 selectedId 执行用户明确的连接探测。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：端点、凭据或传输错误由 providers 报告。
                             * 副作用：可能访问所填服务；不传小说，网络生命周期归模型而非列表代理。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: providers.probeConnection(page.selectedId) }
                    }
                    /* 网络边界说明，区分连接探测和明确确认后的小说发送。 */
                    Label { text: qsTr("连接测试会访问所填服务；小说片段仅在你确认开始模型解析或抽样后发送。")
                            color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    /* 连接服务状态反馈，优先显示中文错误。 */
                    Label { text: providers.errorText.length > 0 ? providers.errorText : providers.statusText; color: providers.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
