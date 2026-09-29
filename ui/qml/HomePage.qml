import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Flickable {
    id: page
    required property var uiTheme
    signal navigate(int page)
    contentWidth: width
    contentHeight: content.implicitHeight + 96
    clip: true

    /* 功能：从文件地址提取显示用文件名，不改变交给导入服务的完整地址。
     * 参数：fileUrl 为文件选择框返回的URL，可转换为字符串。
     * 返回：URL最后一段解码后的名称；百分号转义不合法时返回未解码名称。
     * 副作用：无，不读取文件、不创建世界、不发送网络请求。
     */
    function novelFileName(fileUrl) {
        const name = fileUrl.toString().split("/").pop()
        try { return decodeURIComponent(name) } catch (error) { return name }
    }

    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: novelDialog
        title: qsTr("选择要解析的小说")
        acceptLabel: qsTr("选择")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("小说文本 (*.txt *.md *.markdown)"), qsTr("所有文件 (*)")]
        // 只显示文件名；创建时仍将完整本地地址交给导入服务。
        onAccepted: selectedNovel.text = page.novelFileName(selectedFile)
    }

    ColumnLayout {
        id: content
        x: 56
        y: 48
        width: Math.max(0, Math.min(page.width - 112, 680))
        spacing: 10

        Label {
            text: qsTr("新建世界")
            color: page.uiTheme.text
            font.pointSize: 19
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("给世界命名并选择本地小说。创建后先校对章节，再开始解析。")
            color: page.uiTheme.muted
            font.pointSize: 10
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 16
            Layout.bottomMargin: 18
            color: page.uiTheme.border
        }
        Label { text: qsTr("世界名称"); color: page.uiTheme.text; font.pointSize: 10; Layout.fillWidth: true }
        AppField {
            id: worldName
            objectName: "newWorldNameField"
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            placeholderText: qsTr("输入世界名称")
        }
        Label { text: qsTr("小说文件"); color: page.uiTheme.text; font.pointSize: 10; Layout.fillWidth: true; Layout.topMargin: 16 }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppField {
                id: selectedNovel
                objectName: "selectedNovelField"
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                readOnly: true
                placeholderText: qsTr("尚未选择小说")
            }
            AppButton { uiTheme: page.uiTheme; text: qsTr("选择小说"); onClicked: novelDialog.open() }
        }
        Label {
            text: qsTr("支持纯文本和标记文本。原文保存在本地，只有确认开始模型解析或抽样后才发送片段。")
            color: page.uiTheme.muted
            font.pointSize: 9
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "createWorldButton"
            uiTheme: page.uiTheme
            text: workspaceCatalog.busy ? qsTr("正在导入…") : qsTr("创建并导入小说")
            primary: true
            Layout.topMargin: 20
            enabled: worldName.text.trim().length > 0 && selectedNovel.text.length > 0 && !workspaceCatalog.busy
            onClicked: workspaceCatalog.createWorld(worldName.text, novelDialog.selectedFile)
        }
        Label {
            visible: workspaceCatalog.busy
            text: workspaceCatalog.statusText
            color: page.uiTheme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: workspaceCatalog.errorText.length > 0
            text: workspaceCatalog.errorText
            color: page.uiTheme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
