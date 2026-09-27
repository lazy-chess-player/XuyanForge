import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    required property var uiTheme
    property bool primary: false
    property bool quiet: false
    property bool currentPage: false

    implicitHeight: quiet ? 38 : 40
    opacity: enabled ? 1 : 0.45
    leftPadding: quiet ? 10 : 14
    rightPadding: quiet ? 10 : 14
    focusPolicy: Qt.StrongFocus
    font.pointSize: 10
    font.weight: Font.Medium

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
