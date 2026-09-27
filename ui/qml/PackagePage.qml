import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    FileDialog {
        id: importDialog
        title: qsTr("导入世界包")
        fileMode: FileDialog.OpenFile
        onAccepted: packages.importWorld(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("导出世界包")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("世界包 (*.zip)")]
        onAccepted: packages.exportWorld(selectedFile)
    }
    FolderDialog {
        id: backupDialog
        title: qsTr("选择备份保存位置")
        onAccepted: packages.createBackup(selectedFolder)
    }
    FolderDialog {
        id: restoreDialog
        title: qsTr("选择备份目录")
        onAccepted: packages.restoreBackup(selectedFolder)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16
        Label { text: qsTr("包与备份"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        Label { text: qsTr("导入或导出世界包；备份前请确认目标位置。密钥不会写入包。")
                color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            AppButton { uiTheme: page.uiTheme; text: qsTr("导入世界包"); onClicked: importDialog.open() }
            AppButton { uiTheme: page.uiTheme; text: qsTr("导出世界包"); onClicked: exportDialog.open() }
        }
        RowLayout {
            AppButton { uiTheme: page.uiTheme; text: qsTr("创建备份"); onClicked: backupDialog.open() }
            AppButton { uiTheme: page.uiTheme; text: qsTr("恢复备份"); onClicked: restoreDialog.open() }
        }
        Label { text: packages.errorText.length > 0 ? packages.errorText : packages.statusText;
                color: packages.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted;
                wrapMode: Text.Wrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
}
