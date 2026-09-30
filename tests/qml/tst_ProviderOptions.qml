import QtQuick
import "../../ui/qml"

/* 连接设置中文选项宿主；使用空内存连接列表，选择类型不发网络请求。 */
Item {
    id: root
    width: 1400
    height: 900
    /* GUI线程拥有的共享测试主题，数据行选择颜色模式。 */
    UiTheme { id: theme }
    /* 真实连接页工厂，测试框架在每项断言完成后释放实例。 */
    Component { id: pageComponent; ProviderPage { uiTheme: theme; width: 1400; height: 900 } }
    OptionTestCase {
        name: "ProviderChineseOptions"
        when: windowShown
        /* 功能：列出三种服务类型在深浅主题中的预期显示和协议。
         * 参数：无。返回：六条数据；失败：分配异常由框架报告。
         * 副作用：只构造测试预期值，不创建连接、端点或凭据。 */
        function test_choices_data() {
            const rows = []
            const labels = [qsTr("深度求索"), qsTr("兼容接口"), qsTr("本地接口")]
            const values = ["deepseek", "openai-compatible", "local"]
            for (const mode of ["dark", "light"])
                for (let index = 0; index < labels.length; ++index)
                    rows.push({tag: mode + index, mode: mode, ordinal: index, label: labels[index], value: values[index]})
            return rows
        }
        /* 功能：实际展开并点击每个服务选项，核对中文与内部值。
         * 参数：data为数据行，含主题、行号、预期显示和值，仅本次使用。
         * 返回：无；失败：页面加载、选项显示或点击结果不符时断言失败。
         * 副作用：只改变页面选择，不保存、不探测服务，GUI线程执行。 */
        function test_choices(data) {
            theme.mode = data.mode
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            const choice = findChild(page, "kindChoice")
            verify(!!choice, "Object exists")
            chooseOption(choice, data.ordinal, data.label, data.value)
        }
        /* 功能：读取未知服务协议时保留中文回退，不回显英文协议或自动替换。
         * 参数：无。返回：无；失败：回退不符时断言失败。
         * 副作用：调用真实页面的只读填表函数，提供空测试值，不保存或访问系统凭据。 */
        function test_unknownProvider() {
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, "Component exists")
            page.selectConnection({id: "", revision: 0, name: "", endpoint: "", model: "", kind: "unrecognized-test-provider"})
            const choice = findChild(page, "kindChoice")
            verify(!!choice, "Object exists")
            compare(choice.displayText, qsTr("未知服务类型，请选择"))
        }
        /* 功能：恢复共享主题，避免影响其他用例。参数：无。返回：无。
         * 失败：赋值异常交框架；副作用：仅改变测试主题，页面由临时对象机制销毁。 */
        function cleanup() { theme.mode = "dark" }
    }
}
