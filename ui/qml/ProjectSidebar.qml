import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: sidebar
    required property var uiTheme
    required property int activePage
    readonly property bool hasWorld: workspaceCatalog.worlds.length > 0
    signal navigate(int page)

    color: uiTheme.sidebar
    border.color: uiTheme.border

    FileDialog {
        id: workspaceDialog
        title: qsTr("打开工作区")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("工作区数据库 (*.sqlite *.db)")]
        onAccepted: workspaceCatalog.openWorkspace(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Label {
            text: qsTr("叙演工坊")
            color: sidebar.uiTheme.text
            font.pointSize: 13
            font.weight: Font.DemiBold
            Layout.topMargin: 6
        }
        AppButton {
            uiTheme: sidebar.uiTheme
            text: qsTr("新建世界")
            primary: sidebar.activePage === 10
            Layout.fillWidth: true
            Layout.topMargin: 18
            onClicked: sidebar.navigate(10)
        }
        Label {
            text: qsTr("项目")
            color: sidebar.uiTheme.muted
            font.pointSize: 9
            Layout.topMargin: 20
            Layout.bottomMargin: 4
        }
        Label {
            visible: !sidebar.hasWorld
            text: qsTr("暂无项目")
            color: sidebar.uiTheme.muted
            font.pointSize: 10
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ListView {
            id: worldList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: workspaceCatalog.worlds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: worldDelegate
                required property var modelData
                required property int index
                width: worldList.width
                height: 38
                text: modelData.name
                font.pointSize: 10
                highlighted: workspaceCatalog.activeWorldId === modelData.id
                contentItem: Text {
                    text: worldDelegate.text
                    font: worldDelegate.font
                    color: worldDelegate.highlighted ? sidebar.uiTheme.accent : sidebar.uiTheme.text
                    leftPadding: 10
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    radius: 5
                    color: worldDelegate.highlighted || worldDelegate.hovered
                        ? sidebar.uiTheme.surfaceAlt : "transparent"
                    border.width: worldDelegate.activeFocus ? 2 : 0
                    border.color: sidebar.uiTheme.accent
                }
                onClicked: {
                    // 选中项目，并请求打开项目记录的小说来源。
                    workspaceCatalog.selectWorld(index)
                    sources.selectSourceId(modelData.sourceId)
                    sidebar.navigate(2)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 8; Layout.bottomMargin: 6; color: sidebar.uiTheme.border }
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("小说与章节"); quiet: true; currentPage: sidebar.activePage === 2; visible: sidebar.hasWorld; Layout.fillWidth: true; onClicked: sidebar.navigate(2) }
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("解析任务"); quiet: true; currentPage: sidebar.activePage === 7; visible: sidebar.hasWorld; Layout.fillWidth: true; onClicked: sidebar.navigate(7) }
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("校对中心"); quiet: true; currentPage: sidebar.activePage === 8; visible: sidebar.hasWorld; Layout.fillWidth: true; onClicked: sidebar.navigate(8) }
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("世界资料"); quiet: true; currentPage: sidebar.activePage === 1; visible: sidebar.hasWorld; Layout.fillWidth: true; onClicked: sidebar.navigate(1) }
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("更多工具"); quiet: true; Layout.fillWidth: true; onClicked: toolsMenu.open() }
        Menu {
            id: toolsMenu
            palette.window: sidebar.uiTheme.surface
            palette.text: sidebar.uiTheme.text
            background: Rectangle { color: sidebar.uiTheme.surface; border.color: sidebar.uiTheme.border; radius: 6 }
            MenuItem { text: qsTr("人物卡"); onTriggered: sidebar.navigate(3) }
            MenuItem { text: qsTr("世界视图"); onTriggered: sidebar.navigate(9) }
            MenuItem { text: qsTr("模型连接"); onTriggered: sidebar.navigate(5) }
            MenuItem { text: qsTr("包与备份"); onTriggered: sidebar.navigate(4) }
            MenuItem { text: qsTr("工作区管理"); onTriggered: sidebar.navigate(6) }
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                uiTheme: sidebar.uiTheme
                text: qsTr("打开工作区")
                quiet: true
                Layout.fillWidth: true
                onClicked: workspaceDialog.open()
            }
            AppButton {
                uiTheme: sidebar.uiTheme
                text: qsTr("最近")
                quiet: true
                enabled: workspaceCatalog.recentWorkspaces.length > 0
                onClicked: recentMenu.open()
            }
            Menu {
                id: recentMenu
                palette.window: sidebar.uiTheme.surface
                palette.text: sidebar.uiTheme.text
                background: Rectangle { color: sidebar.uiTheme.surface; border.color: sidebar.uiTheme.border; radius: 6 }
                Repeater {
                    model: workspaceCatalog.recentWorkspaces
                    MenuItem {
                        required property var modelData
                        required property int index
                        text: modelData.name
                        onTriggered: workspaceCatalog.switchToRecent(index)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                id: themeButton
                objectName: "themeButton"
                uiTheme: sidebar.uiTheme
                quiet: true
                text: workspaceCatalog.themeId === "dark" ? qsTr("切换浅色主题") : qsTr("切换深色主题")
                Layout.fillWidth: true
                onClicked: workspaceCatalog.setThemeId(workspaceCatalog.themeId === "dark" ? "light" : "dark")
            }
        }
    }
}
