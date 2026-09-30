import QtQuick
import QtQuick.Controls.Basic

/* 单行编辑控件：GUI 线程中由父表单拥有，仅维护输入状态，保存与校验策略由调用方提供。 */
TextField {
    id: control
    /* 调用方必须提供的共享主题观察引用；随页面存活，无本地默认主题或业务数据。 */
    required property var uiTheme
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 12
    color: uiTheme.text
    placeholderTextColor: uiTheme.muted
    selectionColor: uiTheme.accent
    font.pointSize: 10
    /* 输入背景与焦点边框：读取控件焦点及主题，不修改文字或业务草稿。 */
    background: Rectangle {
        radius: 6
        color: control.uiTheme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.uiTheme.accent : control.uiTheme.border
    }
}
