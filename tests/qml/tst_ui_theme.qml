import QtQuick
import QtTest
import "../../ui/qml"

/* 主题回归宿主，仅测试目标创建，GUI 线程拥有临时组件，不接触工作区文件。 */
Item {
    id: root
    width: 320
    height: 240

    /* 主题组件工厂及临时主题定义，createTemporaryObject 负责用例结束清理。 */
    Component { id: themeComponent; UiTheme {} }

    /* 主题测试集合，窗口就绪后在 GUI 线程断言默认色与切换行为。 */
    TestCase {
        name: "UiTheme"
        when: windowShown

        /*
         * 功能：创建独立主题实例，核对默认 dark 与深色画布、文字令牌。
         * 参数：无。
         * 返回：无；三个默认值断言表示成功。
         * 失败：主题创建失败或默认颜色改变时 Qt Test 标记失败。
         * 副作用：仅创建临时 QtObject 并读取属性，测试框架负责释放，不修改应用设置。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_defaultDarkPalette() {
            const theme = createTemporaryObject(themeComponent, root)
            verify(!!theme, qsTr("组件存在"))
            compare(theme.mode, "dark")
            compare(theme.canvas.toString(), "#111a22")
            compare(theme.text.toString(), "#eaf0f2")
        }

        /*
         * 功能：在独立实例切换 light 后恢复 dark，核对画布和文字随主题变化。
         * 参数：无。
         * 返回：无；两次主题颜色断言均通过才成功。
         * 失败：创建失败或令牌未同步到约定颜色时断言失败。
         * 副作用：只写临时主题 mode，结束自动清理，不修改真实目录模型。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_switchPalette() {
            const theme = createTemporaryObject(themeComponent, root)
            verify(!!theme, qsTr("组件存在"))
            theme.mode = "light"
            compare(theme.canvas.toString(), "#f5f7f8")
            compare(theme.text.toString(), "#172831")
            theme.mode = "dark"
            compare(theme.canvas.toString(), "#111a22")
            compare(theme.text.toString(), "#eaf0f2")
        }
    }
}
