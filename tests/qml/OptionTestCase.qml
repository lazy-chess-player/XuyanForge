import QtQuick
import QtTest

/* 中文选项测试辅助；仅测试引擎加载，GUI线程观察真实弹层，不拥有业务模型。 */
TestCase {
    /*
     * 功能：展开真实下拉列表、核对指定行中文并点击，验证显示标签与提交协议值分离。
     * 参数：control为当前用例拥有的下拉控件观察引用；ordinal为零基行号；label为预期中文显示；
     *       value为预期内部协议值，均须对应已存在的选项，控件须在显示窗口中。
     * 返回：无；标签、值及弹层关闭均符合预期才通过。
     * 失败：控件、行缺失或事件未完成时断言失败，禁止只检查模型而跳过实际代理。
     * 副作用：投递鼠标事件并滚动弹层、改变测试控件选择；不调用保存、网络或数据库。
     */
    function chooseOption(control, ordinal, label, value) {
        mouseClick(control, control.width / 2, control.height / 2)
        tryCompare(control.popup, "visible", true)
        const list = control.popup.contentItem
        list.positionViewAtIndex(ordinal, ListView.Center)
        tryVerify(function() { return list.itemAtIndex(ordinal) !== null })
        const row = list.itemAtIndex(ordinal)
        verify(!!row, "Object exists")
        tryCompare(row, "text", label)
        mouseClick(row, row.width / 2, row.height / 2)
        tryCompare(control, "displayText", label)
        tryCompare(control, "currentValue", value)
        tryCompare(control.popup, "visible", false)
    }
}
