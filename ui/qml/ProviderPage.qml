import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    property string selectedId: ""
    property int selectedRevision: 0

    // 只把非敏感连接参数填入表单；密钥由用户重新输入，不回显。
    function selectConnection(connection) {
        selectedId = connection.id
        selectedRevision = connection.revision
        nameField.text = connection.name
        endpointField.text = connection.endpoint
        modelField.text = connection.model
        kindChoice.currentIndex = Math.max(0, kindChoice.indexOfValue(connection.kind))
        keyField.text = ""
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: qsTr("已保存连接"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                Label { visible: providers.connections.length === 0; text: qsTr("尚未配置模型连接。离线解析不需要连接。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: connectionsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: providers.connections
                    delegate: ItemDelegate {
                        required property var modelData
                        width: connectionsList.width
                        text: modelData.name
                        onClicked: page.selectConnection(modelData)
                    }
                }
                AppButton {
                    uiTheme: page.uiTheme; text: qsTr("新建连接")
                    onClicked: { page.selectedId = ""; page.selectedRevision = 0; nameField.text = ""; endpointField.text = ""; modelField.text = ""; keyField.text = "" }
                }
            }
        }
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            Flickable {
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: form.implicitHeight + 16
                clip: true
                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: 10
                    Label { text: qsTr("连接设置"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("自定义连接名称") }
                    Label { text: qsTr("服务类型"); color: page.uiTheme.muted }
                    AppComboBox {
                        id: kindChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: [ {"label": qsTr("深度求索"), "value": "deepseek"},
                                 {"label": qsTr("兼容接口"), "value": "openai-compatible"},
                                 {"label": qsTr("本地接口"), "value": "local"} ]
                        textRole: "label"
                        valueRole: "value"
                    }
                    Label { text: qsTr("接口地址"); color: page.uiTheme.muted }
                    AppField { id: endpointField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("填写加密连接地址") }
                    Label { text: qsTr("模型名称"); color: page.uiTheme.muted }
                    AppField { id: modelField; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("填写模型名称") }
                    Label { text: qsTr("密钥（留空则保持现有密钥）"); color: page.uiTheme.muted }
                    AppField { id: keyField; uiTheme: page.uiTheme; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: qsTr("仅保存在系统凭据管理器") }
                    RowLayout {
                        AppButton {
                            uiTheme: page.uiTheme; text: qsTr("保存连接"); primary: true
                            enabled: nameField.text.trim().length > 0 && endpointField.text.trim().length > 0 && modelField.text.trim().length > 0
                            onClicked: {
                                providers.saveConnection(page.selectedId, page.selectedRevision, nameField.text,
                                                         kindChoice.currentValue, endpointField.text, modelField.text,
                                                         kindChoice.currentValue === "local" ? "local_only" : "remote_allowed",
                                                         true, keyField.text)
                                keyField.text = ""
                            }
                        }
                        AppButton { uiTheme: page.uiTheme; text: qsTr("测试连接"); enabled: page.selectedId.length > 0; onClicked: providers.probeConnection(page.selectedId) }
                    }
                    Label { text: qsTr("连接测试会访问所填服务；小说片段仅在你确认开始模型解析或抽样后发送。")
                            color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: providers.errorText.length > 0 ? providers.errorText : providers.statusText; color: providers.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
