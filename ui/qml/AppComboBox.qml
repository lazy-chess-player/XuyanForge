import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    required property var uiTheme
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 32
    font.pointSize: 10
    palette.text: uiTheme.text
    palette.button: uiTheme.surface
    palette.base: uiTheme.surface
    palette.highlight: uiTheme.surfaceAlt
    background: Rectangle {
        radius: 6
        color: control.uiTheme.surface
        border.color: control.activeFocus ? control.uiTheme.accent : control.uiTheme.border
        border.width: control.activeFocus ? 2 : 1
    }
    contentItem: Text {
        text: control.displayText
        color: control.uiTheme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: control.uiTheme.muted
        font.pointSize: 14
    }
    delegate: ItemDelegate {
        required property var modelData
        width: control.width
        text: modelData[control.textRole]
        palette.text: control.uiTheme.text
        background: Rectangle {
            color: parent.highlighted ? control.uiTheme.surfaceAlt : control.uiTheme.surface
        }
    }
    popup: Popup {
        y: control.height
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle { color: control.uiTheme.surface; border.color: control.uiTheme.border; radius: 6 }
    }
}
