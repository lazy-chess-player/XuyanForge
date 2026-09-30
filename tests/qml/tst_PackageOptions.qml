import QtQuick
import QtQuick.Controls
import "../../ui/qml"

/* 包页文件动作与覆盖确认中文回归，packages为测试入口注入的内存替身，任何目标均不写磁盘。 */
Item {
    id: root
    width: 1000
    height: 800
    /* 宿主拥有的深浅主题对象，用例结束恢复深色。 */
    UiTheme { id: theme }
    /* 真实包页面工厂，仅创建控件，业务上下文为无文件能力的替身。 */
    Component { id: pageComponent; PackagePage { uiTheme: theme; width: 1000; height: 800 } }
    OptionTestCase {
        name: "PackageChineseOptions"
        when: windowShown
        /* 功能：重置覆盖分支与导出计数。参数：无。返回：无。
         * 失败：替身字段不存在由测试报告；副作用：仅改测试内存，零文件写入。 */
        function init() { packages.destinationPresent = false; packages.exportCount = 0 }
        /* 功能：提供文件框深浅色用例。参数：无。返回：两项主题行。
         * 失败：分配异常交框架；副作用：无，不打开窗口。 */
        function test_fileActions_data() { return [{tag: "dark", mode: "dark"}, {tag: "light", mode: "light"}] }
        /* 功能：实际打开四个文件/目录框，核对标题、接受/取消及每个文件筛选中文。
         * 参数：data为框架提供的主题行，mode须为dark/light。返回：无。
         * 失败：框缺失、无法打开/取消或标签不符时失败。
         * 副作用：创建真实页面、读取目录元数据并取消选择，不接受文件或执行包操作。 */
        function test_fileActions(data) {
            theme.mode = data.mode
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            const rows = [
                {name: "importDialog", title: qsTr("导入世界包"), accept: qsTr("导入")},
                {name: "exportDialog", title: qsTr("导出世界包"), accept: qsTr("保存")},
                {name: "backupDialog", title: qsTr("选择备份保存位置"), accept: qsTr("选择")},
                {name: "restoreDialog", title: qsTr("选择备份目录"), accept: qsTr("选择")}
            ]
            for (const row of rows) {
                const dialog = findChild(page, row.name)
                verify(!!dialog, "Object exists")
                compare(dialog.title, row.title)
                compare(dialog.acceptLabel, row.accept)
                compare(dialog.rejectLabel, qsTr("取消"))
                if (dialog.nameFilters !== undefined) {
                    verify(dialog.nameFilters.length > 0, qsTr("筛选项存在"))
                    for (const filter of dialog.nameFilters) verify(/^[\u4e00-\u9fff]/.test(filter), qsTr("筛选使用中文标签"))
                }
                dialog.open()
                tryCompare(dialog, "visible", true)
                dialog.reject()
                tryCompare(dialog, "visible", false)
            }
            compare(packages.exportCount, 0)
        }
        /* 功能：实际打开已有目标的中文确认，点击返回后必须零导出。
         * 参数：无。返回：无；失败：中文动作缺失、返回后仍导出或残留快照时失败。
         * 副作用：只向内存替身分派测试URL，鼠标关闭真实弹窗，不写文件。 */
        function test_overwriteReturn() {
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            packages.destinationPresent = true
            page.requestExport(Qt.resolvedUrl("仅内存目标.zip"))
            const dialog = findChild(page, "overwriteConfirm")
            verify(!!dialog, "Object exists")
            tryCompare(dialog, "visible", true)
            compare(dialog.title, qsTr("确认替换文件"))
            compare(dialog.standardButton(Dialog.Yes).text, qsTr("替换"))
            const back = dialog.standardButton(Dialog.No)
            verify(!!back, "Object exists")
            compare(back.text, qsTr("返回"))
            compare(packages.exportCount, 0)
            mouseClick(back, back.width / 2, back.height / 2)
            tryCompare(dialog, "visible", false)
            tryCompare(packages, "exportCount", 0)
            tryCompare(page, "pendingExportFile", "")
        }
        /* 功能：点击明确替换后只发送一次导出动作，并清空确认快照。
         * 参数：无。返回：无；失败：重复导出、目标变化或中文按钮不符时失败。
         * 副作用：点击真实弹窗，仅更新内存导出计数；不存在文件操作。 */
        function test_overwriteAccept() {
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            packages.destinationPresent = true
            const target = Qt.resolvedUrl("仅内存目标.zip")
            page.requestExport(target)
            const dialog = findChild(page, "overwriteConfirm")
            verify(!!dialog, "Object exists")
            tryCompare(dialog, "visible", true)
            const accept = dialog.standardButton(Dialog.Yes)
            verify(!!accept, "Object exists")
            compare(accept.text, qsTr("替换"))
            mouseClick(accept, accept.width / 2, accept.height / 2)
            tryCompare(packages, "exportCount", 1)
            tryCompare(packages, "lastExportFile", target)
            tryCompare(page, "pendingExportFile", "")
        }
        /* 功能：不存在目标直接导出一次，不能错误显示替换确认。
         * 参数：无。返回：无；失败：未导出或弹窗误开时失败。
         * 副作用：仅调用内存替身，不生成文件或目录，GUI线程执行。 */
        function test_newDestination() {
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            page.requestExport(Qt.resolvedUrl("仅内存目标.zip"))
            const dialog = findChild(page, "overwriteConfirm")
            verify(!!dialog, "Object exists")
            compare(packages.exportCount, 1)
            compare(dialog.visible, false)
        }
        /* 功能：清理替身分支和计数。参数：无。返回：无。
         * 失败：赋值异常交框架；副作用：只恢复内存，临时页面自动销毁。 */
        function cleanup() { packages.destinationPresent = false; packages.exportCount = 0; theme.mode = "dark" }
    }
}
