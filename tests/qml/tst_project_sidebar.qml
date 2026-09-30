import QtQuick
import QtTest
import "../../ui/qml"

/* 侧栏回归宿主，仅测试进程使用，GUI 线程拥有临时页面与主题。 */
Item {
    id: root
    width: 320
    height: 700

    /* 跟随测试目录替身主题的实例，宿主拥有，不使用真实用户设置。 */
    UiTheme { id: theme; mode: workspaceCatalog.themeId }
    /* 侧栏组件工厂，用例结束由临时对象机制清理。 */
    Component {
        id: sidebarComponent
        /* 测试侧栏定义，固定新建路由，不注入任何项目资料。 */
        ProjectSidebar {
            uiTheme: theme
            activePage: 10
            width: 244
            height: 680
        }
    }

    /* 侧栏空态与主题点击回归集合，窗口显示后执行。 */
    TestCase {
        name: "ProjectSidebar"
        when: windowShown

        /*
         * 功能：在每个侧栏用例前把测试目录替身恢复为深色，消除用例执行顺序影响。
         * 参数：无。
         * 返回：无。
         * 失败：依赖测试目录替身可设置主题；异常由测试框架报告。
         * 副作用：改变内存主题及共享测试主题绑定，不写真实用户设置。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function init() {
            workspaceCatalog.setThemeId("dark")
        }

        /*
         * 功能：在用例结束或断言失败后恢复测试目录替身为深色。
         * 参数：无。
         * 返回：无。
         * 失败：恢复调用异常由 Qt Test 报告，不掩盖原断言失败。
         * 副作用：只恢复替身主题；临时侧栏由框架清理，不访问业务工作区。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function cleanup() {
            workspaceCatalog.setThemeId("dark")
        }

        /*
         * 功能：创建空侧栏，核对 hasWorld 与目录世界数量均为空。
         * 参数：无。
         * 返回：无；hasWorld 为 false 且世界数为 0 才成功。
         * 失败：实例创建失败或出现预置项目时断言失败。
         * 副作用：只创建并读取临时侧栏，测试框架自动销毁，不生成世界。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_emptyProjectList() {
            const sidebar = createTemporaryObject(sidebarComponent, root)
            verify(!!sidebar, qsTr("组件存在"))
            compare(sidebar.hasWorld, false)
            compare(workspaceCatalog.worlds.length, 0)
        }

        /*
         * 功能：真实点击主题按钮完成深色到浅色再回深色，核对模型标识、文案与背景同步。
         * 参数：无。
         * 返回：无；每次状态、主题和按钮文字断言均成立才成功。
         * 失败：按钮缺失或任一响应未在 tryCompare 等待期内到达时断言失败。
         * 副作用：投递鼠标事件，只修改替身主题，cleanup 恢复深色，临时侧栏自动释放。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_themeButtonSwitchesSidebarPalette() {
            const sidebar = createTemporaryObject(sidebarComponent, root)
            verify(!!sidebar, qsTr("组件存在"))
            const themeButton = findChild(sidebar, "themeButton")
            verify(!!themeButton, qsTr("对象存在"))
            compare(sidebar.color.toString(), "#0d161e")
            compare(themeButton.text, qsTr("切换浅色主题"))
            mouseClick(themeButton, themeButton.width / 2, themeButton.height / 2)
            tryCompare(workspaceCatalog, "themeId", "light")
            tryCompare(theme, "mode", "light")
            tryCompare(sidebar, "color", theme.sidebar)
            tryCompare(themeButton, "text", qsTr("切换深色主题"))
            mouseClick(themeButton, themeButton.width / 2, themeButton.height / 2)
            tryCompare(workspaceCatalog, "themeId", "dark")
            tryCompare(theme, "mode", "dark")
            tryCompare(sidebar, "color", theme.sidebar)
            tryCompare(themeButton, "text", qsTr("切换浅色主题"))
        }
    }
}
