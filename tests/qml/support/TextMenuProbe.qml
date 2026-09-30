import QtQuick
import QtQuick.Controls.Basic
import "../../../ui/qml"

/* Qt6.9及以上文本菜单探针；仅测试条件加载，低版本不会编译此文件，生产模块不包含它。 */
AppField {
    id: control
    width: 320
    /* 功能：读取框架默认文本右键菜单，供测试通过公开接口检查真实动作。
     * 参数：无。返回：框架拥有的Menu观察引用，首次右键前可为空。
     * 失败：仅在支持ContextMenu的Qt版本加载，违反前置条件由QML报告。
     * 副作用：只读附加属性，不修改剪贴板或业务数据；GUI线程同步调用。 */
    function defaultMenu() { return control.ContextMenu.menu }
}
