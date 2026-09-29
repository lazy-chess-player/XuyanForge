import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: openDialog
        title: qsTr("打开工作区")
        acceptLabel: qsTr("打开")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("工作区数据库 (*.sqlite *.db)")]
        onAccepted: workspaceCatalog.openWorkspace(selectedFile)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16
        Label { text: qsTr("工作区管理"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        Label { text: qsTr("当前工作区：%1").arg(workspaceCatalog.currentPath); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            AppButton { uiTheme: page.uiTheme; text: qsTr("打开工作区"); onClicked: openDialog.open() }
            AppButton { uiTheme: page.uiTheme; text: qsTr("新建工作区"); onClicked: createDialog.open() }
        }
        Label { text: qsTr("最近打开"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
        Label { visible: workspaceCatalog.recentWorkspaces.length === 0; text: qsTr("暂无最近工作区"); color: page.uiTheme.muted }
        Repeater {
            model: workspaceCatalog.recentWorkspaces
            RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Label { text: modelData.name; color: page.uiTheme.text; Layout.fillWidth: true }
                AppButton { uiTheme: page.uiTheme; text: qsTr("打开"); onClicked: workspaceCatalog.switchToRecent(index) }
                AppButton { uiTheme: page.uiTheme; text: qsTr("从列表移除"); onClicked: workspaceCatalog.forgetRecent(index) }
            }
        }
        Item { Layout.fillHeight: true }
    }
    Dialog {
        id: createDialog
        title: qsTr("新建工作区")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: AppField { id: workspaceName; uiTheme: page.uiTheme; placeholderText: qsTr("工作区名称"); implicitWidth: 320 }
        onAccepted: workspaceCatalog.createWorkspace(workspaceName.text)
    }
}
