import QtQuick
import QtTest
import "../../ui/qml"

Item {
    id: root
    width: 320
    height: 240

    Component { id: themeComponent; UiTheme {} }

    TestCase {
        name: "UiTheme"
        when: windowShown

        /** @brief 新建主题默认使用深色调色板。 */
        function test_defaultDarkPalette() {
            const theme = createTemporaryObject(themeComponent, root)
            verify(!!theme, "Component exists")
            compare(theme.mode, "dark")
            compare(theme.canvas.toString(), "#111a22")
            compare(theme.text.toString(), "#eaf0f2")
        }

        /** @brief 深浅切换时背景和前景颜色同步更新，切回后恢复原值。 */
        function test_switchPalette() {
            const theme = createTemporaryObject(themeComponent, root)
            verify(!!theme, "Component exists")
            theme.mode = "light"
            compare(theme.canvas.toString(), "#f5f7f8")
            compare(theme.text.toString(), "#172831")
            theme.mode = "dark"
            compare(theme.canvas.toString(), "#111a22")
            compare(theme.text.toString(), "#eaf0f2")
        }
    }
}
