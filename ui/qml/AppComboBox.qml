import QtQuick
import QtQuick.Controls.Basic

/* 主题化下拉框：父表单拥有，在 GUI 线程读取调用方的模型、显示角色与协议值角色，不保存业务记录。 */
ComboBox {
    id: control
    /* 共享主题观察引用，无默认值，必须由父页面提供且覆盖控件生命周期。 */
    required property var uiTheme
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 32
    font.pointSize: 10
    palette.text: uiTheme.text
    palette.button: uiTheme.surface
    palette.base: uiTheme.surface
    palette.highlight: uiTheme.surfaceAlt
    /* 输入表面及键盘焦点边框，由下拉控件拥有并随主题更新。 */
    background: Rectangle {
        radius: 6
        color: control.uiTheme.surface
        border.color: control.activeFocus ? control.uiTheme.accent : control.uiTheme.border
        border.width: control.activeFocus ? 2 : 1
    }
    /* 当前选项文字，读取 displayText；页面可提供中文空选项或未知值回退。 */
    contentItem: Text {
        text: control.displayText
        color: control.uiTheme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    /* 装饰性展开标记，仅用于指示弹出列表，不承载独立操作。 */
    indicator: Text {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        text: "⌄"
        color: control.uiTheme.muted
        font.pointSize: 14
    }
    /* 每个模型选项的点击代理：仅随弹出列表存活，选项选择由 ComboBox 管理。 */
    delegate: ItemDelegate {
        /* 当前行的零基索引，由列表注入；稳定定位本次弹出选项，供点击及中文文案回归使用。 */
        required property int index
        objectName: "option_" + index
        /* 当前模型行的观察值，由委托模型注入，无默认值；textRole 指定的字段作为显示名称。 */
        required property var modelData
        width: control.width
        text: modelData[control.textRole]
        palette.text: control.uiTheme.text
        /* 选项高亮背景，跟随代理状态；不改变当前协议值。 */
        background: Rectangle {
            color: parent.highlighted ? control.uiTheme.surfaceAlt : control.uiTheme.surface
        }
    }
    /* 下拉弹层：由控件拥有，打开时显示选项，高度上限 280 像素。 */
    popup: Popup {
        y: control.height
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
        padding: 4
        /* 弹出列表只在可见时绑定代理模型，关闭时释放行实例；currentIndex 跟随键盘高亮。 */
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        /* 弹层边界，读取主题色，由弹层拥有。 */
        background: Rectangle { color: control.uiTheme.surface; border.color: control.uiTheme.border; radius: 6 }
    }
}
