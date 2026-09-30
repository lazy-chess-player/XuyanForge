import QtQuick
import "../../ui/qml"

/* 世界资料中文选项宿主；真实页面只读取测试入口注入的空内存表单。 */
Item {
    id: root
    width: 1400
    height: 900
    /* 宿主拥有的主题；数据行用例显式选择深浅色，用例结束恢复深色。 */
    UiTheme { id: theme }
    /* 真实资料页工厂；临时对象在各用例结束后自动销毁。 */
    Component { id: pageComponent; EntityPage { uiTheme: theme; width: 1400; height: 900 } }
    OptionTestCase {
        name: "EntityChineseOptions"
        when: windowShown
        /* 功能：提供真实九种类型与两种主题的交互组合。参数：无。
         * 返回：18条预期中文/协议值数据；失败：分配异常由测试报告。
         * 副作用：仅创建测试预期值，不向程序提供世界或条目。 */
        function test_choices_data() {
            const labels = [qsTr("人物"), qsTr("势力"), qsTr("地点"), qsTr("物品"), qsTr("规则"),
                            qsTr("事件"), qsTr("文化"), qsTr("技术"), qsTr("其他")]
            const values = ["character", "faction", "location", "item", "rule", "event", "culture", "technology", "other"]
            const rows = []
            for (const mode of ["dark", "light"])
                for (let index = 0; index < labels.length; ++index)
                    rows.push({tag: mode + index, mode: mode, ordinal: index, label: labels[index], value: values[index]})
            return rows
        }
        /* 功能：逐项点击资料类型，核对弹出项、选中显示及内部值。
         * 参数：data为上方数据行，含主题、行号、中文和协议值；只读至返回。
         * 返回：无；失败：页面或控件缺失、中文和值不符时断言失败。
         * 副作用：只改变空表单页面的选择，不保存资料，GUI线程完成。 */
        function test_choices(data) {
            theme.mode = data.mode
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            const choice = findChild(page, "kindChoice")
            verify(!!choice, "Object exists")
            chooseOption(choice, data.ordinal, data.label, data.value)
        }
        /* 功能：未知类型须显示中文回退，不泄露协议值或偷偷选第一项。
         * 参数：无。返回：无；失败：未使用中文回退时断言失败。
         * 副作用：修改测试表单内存并发通知；cleanup恢复，不访问数据库。 */
        function test_unknownType() {
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            const choice = findChild(page, "kindChoice")
            verify(!!choice, "Object exists")
            workspace.selectedKind = "unrecognized-test-type"
            workspace.changed()
            tryCompare(choice, "displayText", qsTr("未知类型，请选择"))
        }
        /* 功能：隔离用例主题与选择状态。参数：无。返回：无。
         * 失败：替身属性更新异常由测试报告；副作用：仅恢复内存，临时页面由框架释放。 */
        function cleanup() { theme.mode = "dark"; workspace.selectedKind = "other"; workspace.changed() }
    }
}
