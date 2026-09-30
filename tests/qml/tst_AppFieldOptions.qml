import QtQuick
import QtTest
import "../../ui/qml"

/* 文本控件菜单的真实鼠标回归，测试字符串仅存在内存，不操作系统剪贴板。 */
Item {
    id: root
    width: 600
    height: 500
    /* 测试主题对象，宿主拥有，清理时恢复深色。 */
    UiTheme { id: theme }
    /* GUI线程测试集合，低版本Qt明确记录不支持内置菜单，而不是假装验证过。 */
    TestCase {
        name: "AppFieldChineseMenu"
        when: windowShown
        /* 功能：提供深浅主题两行。参数：无。返回：主题数据。
         * 失败：分配异常交框架；副作用：无，不创建控件。 */
        function test_contextMenu_data() { return [{tag: "dark", mode: "dark"}, {tag: "light", mode: "light"}] }
        /* 功能：真实右键打开输入框菜单，检查七个动作全部中文，不执行剪贴板动作。
         * 参数：data为主题行，mode须为dark/light；qtVersion由测试入口提供。
         * 返回：无；Qt6.9以下明确跳过，因为该版本尚无此默认菜单。
         * 失败：探针加载、菜单打开超时或任何动作缺失/英文时断言失败。
         * 副作用：只写临时控件文字、焦点和显示菜单；不写文件，不更改剪贴板。 */
        function test_contextMenu(data) {
            const version = qtVersion.split(".").map(Number)
            if (version[0] === 6 && version[1] < 9) { skip(qsTr("该框架版本没有默认文本菜单")); return }
            theme.mode = data.mode
            const component = Qt.createComponent("support/TextMenuProbe.qml")
            verify(component.status === Component.Ready, component.errorString())
            const field = createTemporaryObject(component, root, {uiTheme: theme, x: 40, y: 40, text: qsTr("临时文字")})
            verify(!!field, qsTr("输入控件存在"))
            mouseClick(field, field.width / 2, field.height / 2, Qt.RightButton)
            tryVerify(function() { return field.defaultMenu() !== null })
            const menu = field.defaultMenu()
            tryCompare(menu, "visible", true)
            const expected = [qsTr("撤销"), qsTr("重做"), qsTr("剪切"), qsTr("复制"), qsTr("粘贴"), qsTr("删除"), qsTr("全选")]
            const actual = []
            for (let index = 0; index < menu.count; ++index) {
                const item = menu.itemAt(index)
                if (item && item.text !== undefined && item.text.length > 0) actual.push(item.text)
            }
            compare(actual, expected)
            menu.close()
            tryCompare(menu, "visible", false)
        }
        /* 功能：恢复内存主题。参数：无。返回：无。
         * 失败：赋值错误交框架；副作用：不操作真实设置，临时控件由框架清理。 */
        function cleanup() { theme.mode = "dark" }
    }
}
