import QtQuick
import QtQuick.Controls
import "../../ui/qml"

/* 文件入口回归宿主，页面上下文全部由测试入口的空替身提供；不接受文件选择。 */
Item {
    id: root
    width: 1000
    height: 800
    /* 测试共享主题，由宿主持有，无生产设置写入。 */
    UiTheme { id: theme }
    /* 新建世界真实页面工厂，临时实例由Qt Test释放。 */
    Component { id: homeComponent; HomePage { uiTheme: theme; width: 1000; height: 800 } }
    /* 小说页真实页面工厂，空来源替身不含文本。 */
    Component { id: sourceComponent; SourcePage { uiTheme: theme; width: 1000; height: 800 } }
    /* 工作区页真实页面工厂，不调用创建或打开服务。 */
    Component { id: workspaceComponent; WorkspacePage { uiTheme: theme; width: 1000; height: 800 } }
    /* 侧栏真实组件工厂，无最近工作区，不读取生产路径。 */
    Component { id: sidebarComponent; ProjectSidebar { uiTheme: theme; activePage: 0; width: 280; height: 800 } }
    /* 文件标签与实际弹窗取消测试，GUI线程执行。 */
    OptionTestCase {
        name: "FileChineseOptions"
        when: windowShown
        /* 功能：提供每个文件入口的深浅主题组合。参数：无。返回：八项数据行。
         * 失败：数据分配异常交框架；副作用：无，不创建窗口。 */
        function test_labels_data() {
            return [
                {tag: "home-dark", factory: homeComponent, dialog: "novelDialog", title: qsTr("选择要解析的小说"), accept: qsTr("选择"), mode: "dark"},
                {tag: "home-light", factory: homeComponent, dialog: "novelDialog", title: qsTr("选择要解析的小说"), accept: qsTr("选择"), mode: "light"},
                {tag: "source-dark", factory: sourceComponent, dialog: "importDialog", title: qsTr("导入小说"), accept: qsTr("导入"), mode: "dark"},
                {tag: "source-light", factory: sourceComponent, dialog: "importDialog", title: qsTr("导入小说"), accept: qsTr("导入"), mode: "light"},
                {tag: "workspace-dark", factory: workspaceComponent, dialog: "openDialog", title: qsTr("打开工作区"), accept: qsTr("打开"), mode: "dark"},
                {tag: "workspace-light", factory: workspaceComponent, dialog: "openDialog", title: qsTr("打开工作区"), accept: qsTr("打开"), mode: "light"},
                {tag: "sidebar-dark", factory: sidebarComponent, dialog: "workspaceDialog", title: qsTr("打开工作区"), accept: qsTr("打开"), mode: "dark"},
                {tag: "sidebar-light", factory: sidebarComponent, dialog: "workspaceDialog", title: qsTr("打开工作区"), accept: qsTr("打开"), mode: "light"}
            ]
        }
        /* 功能：创建真实页面核对文件标题、按钮及每项筛选中文，实际打开再取消。
         * 参数：data为用例行，factory为页面工厂、dialog为对象名称，其余为预期中文及主题。
         * 返回：无；失败：页面缺失、框架打开超时或文本不符时断言失败。
         * 副作用：读取当前目录元数据并显示非原生选择框，调用reject关闭；不接受路径或写文件。 */
        function test_labels(data) {
            theme.mode = data.mode
            const page = createTemporaryObject(data.factory, root)
            verify(!!page, qsTr("组件存在"))
            const dialog = findChild(page, data.dialog)
            verify(!!dialog, qsTr("文件框存在"))
            compare(dialog.title, data.title)
            compare(dialog.acceptLabel, data.accept)
            compare(dialog.rejectLabel, qsTr("取消"))
            verify(dialog.nameFilters.length > 0, qsTr("筛选项存在"))
            for (const filter of dialog.nameFilters) verify(/^[\u4e00-\u9fff]/.test(filter), qsTr("筛选使用中文标签"))
            dialog.open()
            tryCompare(dialog, "visible", true)
            dialog.reject()
            tryCompare(dialog, "visible", false)
        }
        /* 功能：实际打开新建工作区，核对标准按钮中文并取消，不调用创建。
         * 参数：无。返回：无；失败：按钮缺失或取消不能关闭时断言失败。
         * 副作用：打开临时弹窗并点击取消；模型没有创建入口，零数据库写入。 */
        function test_createWorkspaceCancel() {
            const page = createTemporaryObject(workspaceComponent, root)
            verify(!!page, qsTr("组件存在"))
            const dialog = findChild(page, "createDialog")
            verify(!!dialog, qsTr("弹窗存在"))
            dialog.open()
            tryCompare(dialog, "visible", true)
            compare(dialog.standardButton(Dialog.Ok).text, qsTr("确定"))
            const cancel = dialog.standardButton(Dialog.Cancel)
            compare(cancel.text, qsTr("取消"))
            mouseClick(cancel, cancel.width / 2, cancel.height / 2)
            tryCompare(dialog, "visible", false)
        }
        /* 功能：恢复测试主题。参数：无。返回：无。
         * 失败：赋值错误交框架；副作用：只恢复内存，临时组件由框架销毁。 */
        function cleanup() { theme.mode = "dark" }
    }
}
