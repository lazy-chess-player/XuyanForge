import QtQuick
import "../../ui/qml"

/* 真实侧栏工具菜单中文回归，目录为空，不创建项目。 */
Item {
    id: root
    width: 600
    height: 900
    /* 宿主拥有的主题，供深浅菜单回归使用。 */
    UiTheme { id: theme }
    /* 侧栏临时工厂，使用空目录上下文。 */
    Component { id: pageComponent; ProjectSidebar { uiTheme: theme; activePage: 0; width: 300; height: 860 } }
    OptionTestCase {
        name: "SidebarChineseOptions"
        when: windowShown
        /* 功能：每种主题都打开真实菜单，逐项检查中文显示。
         * 参数：无。返回：无；失败：菜单无法打开或任一标题不符时断言失败。
         * 副作用：只打开和关闭菜单，不触发导航或业务动作，GUI线程执行。 */
        function test_menuItems() {
            const labels = [qsTr("人物卡"), qsTr("世界视图"), qsTr("模型连接"), qsTr("包与备份"), qsTr("工作区管理")]
            for (const mode of ["dark", "light"]) {
                theme.mode = mode
                const sidebar = createTemporaryObject(pageComponent, root)
                verify(!!sidebar, "Component exists")
                const menu = findChild(sidebar, "toolsMenu")
                verify(!!menu, "Object exists")
                menu.open()
                tryCompare(menu, "visible", true)
                compare(menu.count, labels.length)
                for (let index = 0; index < labels.length; ++index)
                    compare(menu.itemAt(index).text, labels[index])
                menu.close()
                tryCompare(menu, "visible", false)
            }
        }
        /* 功能：恢复主题。参数：无。返回：无。失败：赋值异常由框架报告。
         * 副作用：只恢复内存，所有临时菜单与侧栏由测试框架销毁。 */
        function cleanup() { theme.mode = "dark" }
    }
}
