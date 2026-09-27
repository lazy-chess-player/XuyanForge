import QtQuick

Rectangle {
    required property var uiTheme
    radius: 8
    color: uiTheme.surface
    border.color: uiTheme.border
}
