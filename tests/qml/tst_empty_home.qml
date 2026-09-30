import QtQuick
import QtTest
import "../../ui/qml"

/* 空首页回归宿主，仅测试目标使用，GUI 线程创建临时页面，不写真实工作区。 */
Item {
    id: root
    width: 900
    height: 640

    /* 首页测试共享主题，宿主销毁时释放。 */
    UiTheme { id: theme }
    /* 首页组件工厂及主题注入定义，测试框架管理临时页面寿命。 */
    Component { id: homeComponent; HomePage { uiTheme: theme } }

    /* 空首页测试集合，窗口显示后核对禁用创建行为。 */
    TestCase {
        name: "EmptyHome"
        when: windowShown

        /*
         * 功能：创建空首页，核对名称与小说均为空且创建入口禁用。
         * 参数：无。
         * 返回：无；三个空态断言均成立才成功。
         * 失败：页面或具名控件不存在、空态或可用性不符合要求时断言失败。
         * 副作用：创建临时页面并读取输入框；用例结束自动销毁，不创建世界或读取小说。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_emptyStateCannotCreate() {
            const page = createTemporaryObject(homeComponent, root,
                                               { width: 900, height: 640 })
            verify(!!page, qsTr("组件存在"))
            const worldName = findChild(page, "newWorldNameField")
            const novel = findChild(page, "selectedNovelField")
            const submit = findChild(page, "createWorldButton")
            verify(!!worldName, qsTr("对象存在"))
            verify(!!novel, qsTr("对象存在"))
            verify(!!submit, qsTr("对象存在"))
            compare(worldName.text, qsTr(""))
            compare(novel.text, qsTr(""))
            compare(submit.enabled, false)
        }

        /*
         * 功能：依次写入中文、数字与特殊字符名称，核对缺少小说时点击不能创建世界。
         * 参数：无。
         * 返回：无；输入往返、禁用状态及 createCount 为 0 的断言表示成功。
         * 失败：组件或控件缺失、文字被改写、无文件却触发创建时断言失败。
         * 副作用：仅修改临时页面输入并投递点击；目录端口为测试替身，页面由测试框架清理。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_nameEntryStillNeedsNovel() {
            const page = createTemporaryObject(homeComponent, root,
                                               { width: 900, height: 640 })
            verify(!!page, qsTr("组件存在"))
            const worldName = findChild(page, "newWorldNameField")
            const submit = findChild(page, "createWorldButton")
            verify(!!worldName, qsTr("对象存在"))
            verify(!!submit, qsTr("对象存在"))
            worldName.focus = true
            worldName.text = qsTr("测试世界")
            compare(worldName.text, qsTr("测试世界"))
            worldName.text = qsTr("2026")
            compare(worldName.text, qsTr("2026"))
            worldName.text = qsTr("#世界")
            compare(worldName.text, qsTr("#世界"))
            compare(submit.enabled, false)
            mouseClick(submit, submit.width / 2, submit.height / 2)
            tryCompare(workspaceCatalog, "createCount", 0)
        }

        /*
         * 功能：核对文件名解析保留中文和空格，非法百分号转义回退原名称，空地址不会伪造文件名。
         * 参数：无。返回：无；三种地址的显示名称断言均符合约定才成功。
         * 失败：页面创建失败、URL 解码或回退语义改变时 Qt Test 标记失败。
         * 副作用：仅创建临时首页并调用纯文件名函数，不读文件、不创建世界；测试框架清理页面。
         * 线程与生命周期：GUI 线程同步执行，输入为测试临时字符串，不含小说内容。
         */
        function test_novelFileNameDecodingAndFallback() {
            const page = createTemporaryObject(homeComponent, root,
                                               { width: 900, height: 640 })
            verify(!!page, "组件存在")
            compare(page.novelFileName("file:///local/%E4%B8%AD%E6%96%87%20%E6%96%87%E4%BB%B6.txt"),
                    qsTr("中文 文件.txt"))
            compare(page.novelFileName("file:///local/%invalid.txt"), "%invalid.txt")
            compare(page.novelFileName(""), "")
        }
    }
}
