import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
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
                Label { text: qsTr("人物卡"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                Label { visible: characters.cardItems.length === 0; text: qsTr("还没有人物卡。可从空白卡片开始。")
                        color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: cards
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: characters.cardItems
                    clip: true
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: cards.width
                        text: modelData.name
                        highlighted: characters.selectedIndex === index
                        onClicked: characters.selectCard(index)
                    }
                }
                AppButton { uiTheme: page.uiTheme; text: qsTr("新建人物卡"); onClicked: characters.clearSelection() }
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
                    Label { text: qsTr("人物设定"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.name }
                    Label { text: qsTr("概述"); color: page.uiTheme.muted }
                    AppField { id: summaryField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.summary }
                    Label { text: qsTr("性格特征"); color: page.uiTheme.muted }
                    AppField { id: traitsField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.traits }
                    Label { text: qsTr("长期目标"); color: page.uiTheme.muted }
                    AppField { id: goalField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.longGoal }
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("保存人物卡"); primary: true
                        enabled: nameField.text.trim().length > 0
                        onClicked: characters.saveCard(nameField.text, summaryField.text, characters.values,
                                                        traitsField.text, goalField.text, characters.shortGoal,
                                                        characters.speechStyle, characters.abilities, characters.equipment,
                                                        characters.background, characters.privateNotes)
                    }
                    Label { visible: characters.errorText.length > 0; text: characters.errorText; color: page.uiTheme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
