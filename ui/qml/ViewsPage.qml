import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: page
    required property var uiTheme
    contentWidth: width
    contentHeight: content.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: content
        x: 24
        y: 24
        width: page.width - 48
        spacing: 14
        Label { text: qsTr("世界视图"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        Label { text: qsTr("版本、时间线、关系与地点均来自当前工作区，不预置任何资料。")
                color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            AppButton { uiTheme: page.uiTheme; text: qsTr("刷新"); onClicked: worldViews.refresh() }
            AppButton { uiTheme: page.uiTheme; text: qsTr("发布世界版本"); enabled: workspaceCatalog.activeWorldId.length > 0; onClicked: worldViews.publishVersion("") }
        }
        Label { text: worldViews.errorText.length > 0 ? worldViews.errorText : worldViews.statusText;
                color: worldViews.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted;
                wrapMode: Text.Wrap; Layout.fillWidth: true }
        Repeater {
            model: [ {"title": qsTr("世界版本"), "items": worldViews.versions},
                     {"title": qsTr("时间线"), "items": worldViews.timeline},
                     {"title": qsTr("关系"), "items": worldViews.relations},
                     {"title": qsTr("地点"), "items": worldViews.locations} ]
            SectionPanel {
                required property var modelData
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                implicitHeight: detail.implicitHeight + 32
                ColumnLayout {
                    id: detail
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    Label { text: modelData.title; color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Label { text: modelData.items.length === 0 ? qsTr("暂无记录") : qsTr("共 %1 条记录").arg(modelData.items.length);
                            color: page.uiTheme.muted }
                }
            }
        }
    }
}
