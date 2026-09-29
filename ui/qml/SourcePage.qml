import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    property string loadedSourceId: ""
    property int loadedChapterIndex: -2
    property string loadedChapterTitle: ""
    property string loadedChapterStart: ""
    property string loadedChapterEnd: ""
    signal navigate(int page)

    // 仅在来源、章节或已保存的章节资料变化时重填表单，保留异步预览期间的手工输入。
    function loadSelectedChapter() {
        const sourceId = sources.selectedSourceId
        const index = sources.selectedChapterIndex
        const chapter = index >= 0 && index < sources.chapterItems.length
            ? sources.chapterItems[index] : null
        const title = chapter === null ? "" : String(chapter.title)
        const start = chapter === null ? "" : String(chapter.start)
        const end = chapter === null ? "" : String(chapter.end)
        if (page.loadedSourceId === sourceId && page.loadedChapterIndex === index
                && page.loadedChapterTitle === title && page.loadedChapterStart === start
                && page.loadedChapterEnd === end) {
            return
        }
        page.loadedSourceId = sourceId
        page.loadedChapterIndex = index
        page.loadedChapterTitle = title
        page.loadedChapterStart = start
        page.loadedChapterEnd = end
        chapterTitle.text = title
        chapterStart.text = start
        chapterEnd.text = end
    }
    Connections {
        target: sources
        // 来源模型有任何状态更新时，仅核对章节快照是否真的变化。
        function onChanged() { page.loadSelectedChapter() }
    }
    Component.onCompleted: loadSelectedChapter()

    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: importDialog
        title: qsTr("导入小说")
        acceptLabel: qsTr("导入")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("小说文本 (*.txt *.md *.markdown)"), qsTr("所有文件 (*)")]
        onAccepted: sources.importFile(selectedFile, workspaceCatalog.activeWorldId)
    }
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                RowLayout {
                    Label { text: qsTr("小说文件"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    AppButton { uiTheme: page.uiTheme; text: qsTr("导入"); enabled: workspaceCatalog.activeWorldId.length > 0; onClicked: importDialog.open() }
                }
                Label {
                    visible: sources.sourceItems.length === 0
                    text: qsTr("尚未导入小说")
                    color: page.uiTheme.muted
                    Layout.fillWidth: true
                }
                ListView {
                    id: sourceList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sources.sourceItems
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: sourceList.width
                        text: modelData.name
                        highlighted: sources.selectedIndex === index
                        onClicked: sources.selectSource(index)
                    }
                }
                Label { text: qsTr("章节"); color: page.uiTheme.text; font.pointSize: 11; font.weight: Font.DemiBold }
                ListView {
                    id: chapterList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sources.chapterItems
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: chapterList.width
                        text: modelData.title
                        highlighted: sources.selectedChapterIndex === index
                        onClicked: sources.selectChapter(index)
                    }
                }
            }
        }
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                RowLayout {
                    Label { text: qsTr("原文预览"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        uiTheme: page.uiTheme
                        text: qsTr("创建解析任务")
                        enabled: sources.selectedSourceId.length > 0
                        onClicked: page.navigate(7)
                    }
                }
                Label {
                    text: sources.statusText.length > 0 ? sources.statusText : qsTr("选择左侧小说或章节查看原文")
                    color: page.uiTheme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label { visible: sources.errorText.length > 0; text: sources.errorText; color: page.uiTheme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    visible: sources.selectedChapterIndex >= 0
                    Label { text: qsTr("章节校对"); color: page.uiTheme.muted }
                    AppField { id: chapterTitle; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("章节标题") }
                    AppField { id: chapterStart; uiTheme: page.uiTheme; Layout.preferredWidth: 110; placeholderText: qsTr("起点"); validator: IntValidator { bottom: 0 } }
                    AppField { id: chapterEnd; uiTheme: page.uiTheme; Layout.preferredWidth: 110; placeholderText: qsTr("终点"); validator: IntValidator { bottom: 1 } }
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("保存章节")
                        enabled: chapterTitle.text.trim().length > 0 && Number(chapterEnd.text) > Number(chapterStart.text)
                        onClicked: sources.saveChapter(sources.selectedChapterIndex, chapterTitle.text,
                                                       Number(chapterStart.text), Number(chapterEnd.text))
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        readOnly: true
                        text: sources.previewText
                        wrapMode: TextEdit.Wrap
                        color: page.uiTheme.text
                        selectionColor: page.uiTheme.accent
                        font.pointSize: 10
                        background: Rectangle { color: page.uiTheme.surfaceAlt; radius: 6 }
                    }
                }
            }
        }
    }
}
