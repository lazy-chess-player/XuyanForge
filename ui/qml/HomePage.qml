import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 新建世界入口：GUI 线程随 Loader 页面存活，只持有用户输入及文件选择地址，导入交给目录模型。 */
Flickable {
    id: page
    /* 外部共享主题观察引用，无默认值，必须覆盖页面生命周期。 */
    required property var uiTheme
    /* 功能：请求主窗口切页。参数：page 为内部整数路由。返回：无。
     * 失败：无接收者时无动作。副作用：接收者可切页；GUI 线程发出，页面销毁后断开。 */
    signal navigate(int page)
    contentWidth: width
    contentHeight: content.implicitHeight + 96
    clip: true

    /* 功能：从文件地址提取显示用文件名，不改变交给导入服务的完整地址。
     * 参数：fileUrl 为输入的文件选择 URL，须非空且可调用 toString；空字符串返回空名称。
     * 返回：URL最后一段解码后的名称；百分号转义不合法时返回未解码名称。
     * 失败：解码异常在本函数回退；null、undefined 或缺少 toString 的输入错误向调用方传播。
     * 副作用：无，不读取文件、不创建世界、不发送网络请求。
     * 线程与生命周期：GUI 线程同步调用，不保存地址或安排回调。
     */
    function novelFileName(fileUrl) {
        const name = fileUrl.toString().split("/").pop()
        try { return decodeURIComponent(name) } catch (error) { return name }
    }

    /* 小说文件选择框，页面拥有；仅选择地址，取消不导入，完整 URL 在创建时交给模型。 */
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
        /*
         * 功能：确认小说地址后提取显示文件名，完整 selectedFile 仍留给创建动作。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：解码错误由 novelFileName 回退；非法 URL 对象按该函数传播错误。
         * 副作用：只更新 selectedNovel.text，不读取或导入小说。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: selectedNovel.text = page.novelFileName(selectedFile)
    }

    /* 新建表单布局，宽度上限 680 像素，随滚动页面存活。 */
    ColumnLayout {
        id: content
        x: 56
        y: 48
        width: Math.max(0, Math.min(page.width - 112, 680))
        spacing: 10

        /* 新建入口标题，使用可提取的中文翻译词条。 */
        Label {
            text: qsTr("新建世界")
            color: page.uiTheme.text
            font.pointSize: 19
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        /* 创建前操作说明，不代表任何导入或解析已发生。 */
        Label {
            text: qsTr("给世界命名并选择本地小说。创建后先校对章节，再开始解析。")
            color: page.uiTheme.muted
            font.pointSize: 10
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        /* 标题与表单间的装饰分隔线。 */
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: 16
            Layout.bottomMargin: 18
            color: page.uiTheme.border
        }
        /* 世界名称字段标签，提示用户填写自己的名称。 */
        Label { text: qsTr("世界名称"); color: page.uiTheme.text; font.pointSize: 10; Layout.fillWidth: true }
        /* 世界名称输入，初始空，由用户修改，创建动作读取；不提供样例默认值。 */
        AppField {
            id: worldName
            objectName: "newWorldNameField"
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            placeholderText: qsTr("输入世界名称")
        }
        /* 小说路径字段标签，显示与名称输入分组。 */
        Label { text: qsTr("小说文件"); color: page.uiTheme.text; font.pointSize: 10; Layout.fillWidth: true; Layout.topMargin: 16 }
        /* 文件名与选择动作的横向布局。 */
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            /* 只读文件名输入，初始空；文件确认写入，不缓存小说正文。 */
            AppField {
                id: selectedNovel
                objectName: "selectedNovelField"
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                readOnly: true
                placeholderText: qsTr("尚未选择小说")
            }
            /* 选择文件入口，点击仅打开页面的文件框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("选择小说");
                /*
                 * 功能：显示本地小说选择框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不选择，不创建世界；组件错误由 Qt 报告。
                 * 副作用：打开 novelDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: novelDialog.open() }
        }
        /* 本地存储和显式发送边界说明，原文不会因选择文件而发送。 */
        Label {
            text: qsTr("支持纯文本和标记文本。原文保存在本地，只有确认开始模型解析或抽样后才发送片段。")
            color: page.uiTheme.muted
            font.pointSize: 9
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        /* 创建入口，非空名称且已选择文件时启用；busy 期间阻止重复导入。 */
        AppButton {
            objectName: "createWorldButton"
            uiTheme: page.uiTheme
            text: workspaceCatalog.busy ? qsTr("正在导入…") : qsTr("创建并导入小说")
            primary: true
            Layout.topMargin: 20
            enabled: worldName.text.trim().length > 0 && selectedNovel.text.length > 0 && !workspaceCatalog.busy
            /*
             * 功能：使用 worldName.text 及 novelDialog.selectedFile 创建世界并导入用户小说。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：名称、地址、解码或导入失败由目录模型报告，busy 期间禁止重复入口。
             * 副作用：请求模型本地创建并导入，不触发远程解析；导入异步状态由模型持有。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: workspaceCatalog.createWorld(worldName.text, novelDialog.selectedFile)
        }
        /* 导入期间的目录模型状态，非忙碌时隐藏。 */
        Label {
            visible: workspaceCatalog.busy
            text: workspaceCatalog.statusText
            color: page.uiTheme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        /* 导入错误文字，直接展示目录模型中文错误，不伪装创建成功。 */
        Label {
            visible: workspaceCatalog.errorText.length > 0
            text: workspaceCatalog.errorText
            color: page.uiTheme.danger
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
