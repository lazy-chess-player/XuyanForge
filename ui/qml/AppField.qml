import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    required property var uiTheme
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 12
    color: uiTheme.text
    placeholderTextColor: uiTheme.muted
    selectionColor: uiTheme.accent
    font.pointSize: 10
    background: Rectangle {
        radius: 6
        color: control.uiTheme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.uiTheme.accent : control.uiTheme.border
    }
}
