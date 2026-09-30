import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 来源与章节校对页：GUI 线程观察当前世界的来源模型，持有章节输入快照，不缓存全书正文。 */
Item {
    id: page
    /* 共享主题观察引用，无默认值，调用方须保持其生命周期覆盖页面。 */
    required property var uiTheme
    /* 上次填表的来源稳定标识，初始空；loadSelectedChapter 写入，用于隔离跨来源草稿。 */
    property string loadedSourceId: ""
    /* 上次填表的从零章节索引，初始 -2 表示尚未加载，模型 -1 表示无选择；仅快照函数写入。 */
    property int loadedChapterIndex: -2
    /* 上次已保存的章节标题，初始空；对比模型变化，避免仅预览刷新就覆盖手工标题。 */
    property string loadedChapterTitle: ""
    /* 上次章节起点的十进制字符串，单位为全文 Unicode 码点，初始空表示无章节。 */
    property string loadedChapterStart: ""
    /* 上次章节排他终点的十进制字符串，单位为全文 Unicode 码点，初始空表示无章节。 */
    property string loadedChapterEnd: ""
    /* 功能：请求切页。参数：page 为内部整数路由。返回：无。
     * 失败：无接收者则无动作。副作用：接收者可切页；GUI 线程随页面信号连接存活。 */
    signal navigate(int page)

    /*
     * 功能：核对选中章节的已保存快照，仅在来源、索引或保存字段变化时重填三个编辑框。
     * 参数：无。返回：无。
     * 失败：索引越界按无章节处理，清空表单；依赖 sources 提供有效的列表和字段。
     * 副作用：更新 loaded 系列属性和标题、起止输入，不保存章节、不改变原文；相同快照保留手工草稿。
     * 线程与生命周期：GUI 线程同步执行，由组件完成或模型 changed 调用；只观察外部模型，不安排异步回调。
     */
    function loadSelectedChapter() {
        const sourceId = sources.selectedSourceId
        const index = sources.selectedChapterIndex
        /* 仅观察当前模型行；不存在章节时用 null，禁止由旧输入猜测章节范围。 */
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
    /* 来源模型信号观察器，页面拥有连接且不拥有模型，销毁时自动断开。 */
    Connections {
        target: sources
        /* 功能：模型状态变化时核对章节快照。参数：无。返回：无。
         * 失败：同 loadSelectedChapter。副作用：必要时重填草稿；GUI 线程随 Connections 存活。 */
        function onChanged() { page.loadSelectedChapter() }
    }
    /*
     * 功能：页面创建完毕后核对当前章节并初始化草稿。
     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
     * 返回：无。失败：边界和缺失章节处理遵循 loadSelectedChapter 契约。
     * 副作用：必要时填入当前章节快照，不保存章节。
     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
     */
    Component.onCompleted: loadSelectedChapter()

    /* 导入文件框，确认后将完整 URL 和当前世界 ID 交给来源模型。 */
    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: importDialog
        objectName: "importDialog"
        title: qsTr("导入小说")
        acceptLabel: qsTr("导入")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("小说文本 (*.txt *.md *.markdown)"), qsTr("所有文件 (*)")]
        /*
         * 功能：确认导入用户选择的本地小说到当前 activeWorldId。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：文件、编码、世界身份或导入错误由 sources 展示。
         * 副作用：将 selectedFile 完整 URL 和当前世界身份交给模型，模型负责本地资产和列表更新。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: sources.importFile(selectedFile, workspaceCatalog.activeWorldId)
    }
    /* 来源选择与原文校对左右布局，保留现有分区比例。 */
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        /* 来源与章节导航面板，宽度 260 像素，不持有正文副本。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            /* 导航内容纵向布局，面板拥有。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                /* 小说列表标题和导入动作的横向布局。 */
                RowLayout {
                    /* 小说文件分区标题，独立于用户文件名。 */
                    Label { text: qsTr("小说文件"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 弹性空白，将导入按钮置于标题右侧。 */
                    Item { Layout.fillWidth: true }
                    /* 导入入口，仅已有选中世界时可打开文件框。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("导入"); enabled: workspaceCatalog.activeWorldId.length > 0;
                        /*
                         * 功能：显示本地小说导入选择框。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：未选择或取消时不导入，对话框错误由 Qt 报告。
                         * 副作用：打开 importDialog，不发送原文。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: importDialog.open() }
                }
                /* 空来源提示，仅依赖真实 sourceItems 长度，不创建资料。 */
                Label {
                    visible: sources.sourceItems.length === 0
                    text: qsTr("尚未导入小说")
                    color: page.uiTheme.muted
                    Layout.fillWidth: true
                }
                /* 当前世界来源列表，模型只读来自 sources，行实例由列表管理。 */
                ListView {
                    id: sourceList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sources.sourceItems
                    /* 来源行代理，读取名称和当前行索引，点击请求模型选择来源。 */
                    delegate: ItemDelegate {
                        /* 来源元数据行，由来源列表注入，无默认值；只读显示用户文件名。 */
                        required property var modelData
                        /* 来源列表从零行索引，由列表注入；选择入口读取，不当作稳定来源身份。 */
                        required property int index
                        width: sourceList.width
                        text: modelData.name
                        highlighted: sources.selectedIndex === index
                        /*
                         * 功能：按来源列表当前 index 请求选择来源。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：越界或失效身份由 sources 处理。
                         * 副作用：改变模型选择并触发预览更新，不修改原文。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: sources.selectSource(index)
                    }
                }
                /* 章节列表标题，使用中文翻译接口。 */
                Label { text: qsTr("章节"); color: page.uiTheme.text; font.pointSize: 11; font.weight: Font.DemiBold }
                /* 当前来源章节列表，读取 chapterItems，行实例随列表可见区存活。 */
                ListView {
                    id: chapterList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sources.chapterItems
                    /* 章节行代理，显示原始标题，点击按当前索引选择章节。 */
                    delegate: ItemDelegate {
                        /* 章节元数据行，由章节列表注入，无默认值；只读原始标题和模型保存范围。 */
                        required property var modelData
                        /* 章节从零行索引，由章节列表注入；当前高亮和 selectChapter 读取。 */
                        required property int index
                        width: chapterList.width
                        text: modelData.title
                        highlighted: sources.selectedChapterIndex === index
                        /*
                         * 功能：按章节列表当前 index 请求选择章节。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：越界或预览读取失败由 sources 处理。
                         * 副作用：更新模型章节选择及有界预览，不保存边界。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: sources.selectChapter(index)
                    }
                }
            }
        }
        /* 原文预览与边界编辑面板，占据导航外剩余空间。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            /* 预览面板内容布局，保持固定编辑区与弹性正文区。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                /* 原文标题与解析任务入口的横向布局。 */
                RowLayout {
                    /* 原文预览分区标题，不代表全书已装入页面。 */
                    Label { text: qsTr("原文预览"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 弹性空白，保留任务入口靠右位置。 */
                    Item { Layout.fillWidth: true }
                    /* 解析任务导航入口，需要已选择来源，点击不执行解析。 */
                    AppButton {
                        uiTheme: page.uiTheme
                        text: qsTr("创建解析任务")
                        enabled: sources.selectedSourceId.length > 0
                        /*
                         * 功能：请求进入解析任务页，内部路由 7。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无接收者时保持原页。
                         * 副作用：发出 navigate 信号，不创建或执行任务。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: page.navigate(7)
                    }
                }
                /* 原文窗口状态说明；没有状态时给出中文选择提示。 */
                Label {
                    text: sources.statusText.length > 0 ? sources.statusText : qsTr("选择左侧小说或章节查看原文")
                    color: page.uiTheme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                /* 来源服务错误展示，错误非空时显示，不包含页面生成的正文。 */
                Label { visible: sources.errorText.length > 0; text: sources.errorText; color: page.uiTheme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 章节边界草稿行，仅选中章节时显示。 */
                RowLayout {
                    Layout.fillWidth: true
                    visible: sources.selectedChapterIndex >= 0
                    /* 章节校对字段组标签。 */
                    Label { text: qsTr("章节校对"); color: page.uiTheme.muted }
                    /* 标题草稿，初始由快照函数填入，用户编辑后在保存时读取。 */
                    AppField { id: chapterTitle; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("章节标题") }
                    /* 起点草稿，全文 Unicode 码点位置，初始由快照填入；整数校验器只允许非负值。 */
                    AppField { id: chapterStart; uiTheme: page.uiTheme; Layout.preferredWidth: 110; placeholderText: qsTr("起点");
                        /* 起点整数校验器，最小为第 0 Unicode 码点，最大值沿用 Qt 整数上限；不验证章节连续性。 */
                        validator: IntValidator { bottom: 0 } }
                    /* 排他终点草稿，全文 Unicode 码点位置，初始由快照填入；整数校验器至少为 1。 */
                    AppField { id: chapterEnd; uiTheme: page.uiTheme; Layout.preferredWidth: 110; placeholderText: qsTr("终点");
                        /* 排他终点整数校验器，最小 1 Unicode 码点，最大值沿用 Qt 整数上限；起止顺序由表单及模型验证。 */
                        validator: IntValidator { bottom: 1 } }
                    /* 保存章节动作，将标题及码点范围交给模型，后端负责连续覆盖和修订检查。 */
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("保存章节")
                        enabled: chapterTitle.text.trim().length > 0 && Number(chapterEnd.text) > Number(chapterStart.text)
                        /*
                         * 功能：保存当前章节草稿，将标题及全文码点起止值交给 sources。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：非法边界、修订冲突或保存失败由模型报告，不在回调猜测修复。
                         * 副作用：调用 saveChapter，模型负责连续覆盖校验及已保存章节刷新，原文保持只读。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: sources.saveChapter(sources.selectedChapterIndex, chapterTitle.text,
                                                       Number(chapterStart.text), Number(chapterEnd.text))
                    }
                }
                /* 有界原文窗口的滚动容器，不额外加载整个小说。 */
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    /* 只读原文窗口，绑定 previewText；保留文本换行和可选中行为。 */
                    TextArea {
                        readOnly: true
                        text: sources.previewText
                        wrapMode: TextEdit.Wrap
                        color: page.uiTheme.text
                        selectionColor: page.uiTheme.accent
                        font.pointSize: 10
                        /* 原文显示区次级主题背景，由只读文本控件拥有。 */
                        background: Rectangle { color: page.uiTheme.surfaceAlt; radius: 6 }
                    }
                }
            }
        }
    }
}
