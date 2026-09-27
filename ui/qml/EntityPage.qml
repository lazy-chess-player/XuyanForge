import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    Connections {
        target: workspace
        // 选中另一条资料时同步类型选择，不把内部类型码直接显示给用户。
        function onChanged() { kindChoice.currentIndex = Math.max(0, kindChoice.indexOfValue(workspace.selectedKind)) }
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: qsTr("条目"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                AppField { id: query; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("搜索名称"); onAccepted: workspace.refresh(text) }
                AppButton { uiTheme: page.uiTheme; text: qsTr("搜索"); onClicked: workspace.refresh(query.text) }
                Label { visible: workspace.entityItems.length === 0; text: qsTr("暂无世界资料。解析并接受候选后会显示在这里。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: entityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: workspace.entityItems
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: entityList.width
                        text: modelData.name
                        highlighted: workspace.selectedIndex === index
                        onClicked: workspace.selectEntity(index)
                    }
                }
                RowLayout {
                    AppButton { uiTheme: page.uiTheme; text: qsTr("上一页"); enabled: workspace.hasPreviousPage; onClicked: workspace.previousPage() }
                    Label { text: workspace.pageText; color: page.uiTheme.muted }
                    AppButton { uiTheme: page.uiTheme; text: qsTr("下一页"); enabled: workspace.hasNextPage; onClicked: workspace.nextPage() }
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
                    Label { text: qsTr("资料编辑"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedName }
                    Label { text: qsTr("类型"); color: page.uiTheme.muted }
                    AppComboBox {
                        id: kindChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: [
                            {"label": qsTr("人物"), "value": "character"},
                            {"label": qsTr("势力"), "value": "faction"},
                            {"label": qsTr("地点"), "value": "location"},
                            {"label": qsTr("物品"), "value": "item"},
                            {"label": qsTr("规则"), "value": "rule"},
                            {"label": qsTr("事件"), "value": "event"},
                            {"label": qsTr("文化"), "value": "culture"},
                            {"label": qsTr("技术"), "value": "technology"},
                            {"label": qsTr("其他"), "value": "other"}
                        ]
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: 8
                    }
                    Label { text: qsTr("描述"); color: page.uiTheme.muted }
                    AppField { id: descriptionField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedDescription }
                    Label { text: qsTr("别名（逗号分隔）"); color: page.uiTheme.muted }
                    AppField { id: aliasesField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedAliases }
                    Label { text: qsTr("标签（逗号分隔）"); color: page.uiTheme.muted }
                    AppField { id: tagsField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedTags }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            uiTheme: page.uiTheme; text: qsTr("保存")
                            enabled: nameField.text.trim().length > 0
                            onClicked: {
                                if (workspace.selectedIndex < 0)
                                    workspace.createEntity(nameField.text, kindChoice.currentValue, descriptionField.text,
                                                           aliasesField.text, tagsField.text, "{}")
                                else
                                    workspace.saveSelected(nameField.text, kindChoice.currentValue, descriptionField.text,
                                                           aliasesField.text, tagsField.text, workspace.selectedAttributes)
                            }
                        }
                        AppButton { uiTheme: page.uiTheme; text: qsTr("新建空白条目"); onClicked: workspace.clearSelection() }
                    }
                    Label { text: workspace.errorText.length > 0 ? workspace.errorText : workspace.statusText; color: workspace.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
