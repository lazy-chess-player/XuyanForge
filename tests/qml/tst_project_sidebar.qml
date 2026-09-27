import QtQuick
import QtTest
import "../../ui/qml"

Item {
    id: root
    width: 320
    height: 700

    UiTheme { id: theme; mode: workspaceCatalog.themeId }
    Component {
        id: sidebarComponent
        ProjectSidebar {
            uiTheme: theme
            activePage: 10
            width: 244
            height: 680
        }
    }

    TestCase {
        name: "ProjectSidebar"
        when: windowShown

        /** @brief 每个用例从确定的深色状态开始，避免执行顺序影响断言。 */
        function init() {
            workspaceCatalog.setThemeId("dark")
        }

        /** @brief 即使用例中途失败，也恢复下一用例所需的主题状态。 */
        function cleanup() {
            workspaceCatalog.setThemeId("dark")
        }

        /** @brief 没有任何项目时侧栏保持空项目状态，不生成测试世界。 */
        function test_emptyProjectList() {
            const sidebar = createTemporaryObject(sidebarComponent, root)
            verify(!!sidebar, "Component exists")
            compare(sidebar.hasWorld, false)
            compare(workspaceCatalog.worlds.length, 0)
        }

        /** @brief 点击主题按钮后状态、文案和侧栏背景均随之切换。 */
        function test_themeButtonSwitchesSidebarPalette() {
            const sidebar = createTemporaryObject(sidebarComponent, root)
            verify(!!sidebar, "Component exists")
            const themeButton = findChild(sidebar, "themeButton")
            verify(!!themeButton, "Object exists")
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
