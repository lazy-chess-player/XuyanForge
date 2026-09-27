import QtQuick
import QtTest
import "../../ui/qml"

Item {
    id: root
    width: 320
    height: 240

    UiTheme { id: theme }
    Component {
        id: buttonComponent
        AppButton {
            uiTheme: theme
            width: 160
            height: 40
            text: qsTr("测试按钮")
            property int clickCount: 0
            onClicked: ++clickCount
        }
    }

    TestCase {
        name: "AppButton"
        when: windowShown

        /** @brief 可用按钮收到鼠标点击后触发一次点击处理。 */
        function test_enabledButtonReceivesClick() {
            const button = createTemporaryObject(buttonComponent, root)
            verify(!!button, "Component exists")
            compare(button.clickCount, 0)
            mouseClick(button, button.width / 2, button.height / 2)
            tryCompare(button, "clickCount", 1)
        }

        /** @brief 禁用按钮不会把鼠标点击转化为业务动作。 */
        function test_disabledButtonIgnoresClick() {
            const button = createTemporaryObject(buttonComponent, root)
            verify(!!button, "Component exists")
            button.enabled = false
            mouseClick(button, button.width / 2, button.height / 2)
            tryCompare(button, "clickCount", 0)
        }
    }
}
