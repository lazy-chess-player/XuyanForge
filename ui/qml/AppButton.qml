import QtQuick
import QtQuick.Controls.Basic

/* 统一操作按钮：在 GUI 线程随父页面创建和销毁，读取共享主题；业务动作由调用方连接 clicked 信号。 */
Button {
    id: control
    /* 外部主题对象的观察引用，无默认值；调用方必须提供且其生命周期须覆盖按钮。 */
    required property var uiTheme
    /* 主操作强调标志，无单位；默认 false，由页面设置，背景和文字共同读取。 */
    property bool primary: false
    /* 轻量导航样式，无单位；默认 false，由页面设置，决定高度、边距和文字对齐。 */
    property bool quiet: false
    /* 当前导航项标志，无单位；默认 false，由页面路由设置，驱动高亮而不改变路由。 */
    property bool currentPage: false

    implicitHeight: quiet ? 38 : 40
    opacity: enabled ? 1 : 0.45
    leftPadding: quiet ? 10 : 14
    rightPadding: quiet ? 10 : 14
    focusPolicy: Qt.StrongFocus
    font.pointSize: 10
    font.weight: Font.Medium

    /* 按钮背景：读取按下、悬停及键盘焦点，显示主题边框；由控件拥有。 */
    background: Rectangle {
        radius: 6
        color: control.primary ? control.uiTheme.accent
                               : control.quiet
                                   ? (control.currentPage || control.hovered || control.down
                                       ? control.uiTheme.surfaceAlt : "transparent")
                                   : control.down ? control.uiTheme.surfaceAlt : control.uiTheme.surface
        border.width: control.activeFocus ? 2 : control.quiet ? 0 : 1
        border.color: control.activeFocus ? control.uiTheme.accent : control.uiTheme.border
    }
    /* 按钮标题：显示调用方提供的翻译词条或用户名称，省略溢出文字。 */
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? control.uiTheme.onAccent
                               : control.currentPage ? control.uiTheme.accent : control.uiTheme.text
        horizontalAlignment: control.quiet ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
