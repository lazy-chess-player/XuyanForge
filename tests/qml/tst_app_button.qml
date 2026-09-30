import QtQuick
import QtTest
import "../../ui/qml"

/* 按钮回归宿主，仅测试进程创建，在 GUI 线程拥有临时按钮。 */
Item {
    id: root
    width: 320
    height: 240

    /* 测试专用主题实例，不修改生产设置。 */
    UiTheme { id: theme }
    /* 按钮组件工厂，用例通过临时对象机制创建并清理实例。 */
    Component {
        id: buttonComponent
        /* 测试按钮定义，只有点击计数副作用，不发起业务请求。 */
        AppButton {
            uiTheme: theme
            width: 160
            height: 40
            text: qsTr("测试按钮")
            /* 测试按钮已处理点击次数，单位次，初始 0；点击回调递增，用例读取，实例销毁时释放。 */
            property int clickCount: 0
            /*
             * 功能：记录测试按钮已处理的鼠标点击。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：计数赋值错误由 Qt Test 报告，不模拟业务失败。
             * 副作用：递增本按钮 clickCount，不访问模型、数据库或网络。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: ++clickCount
        }
    }

    /* 按钮点击测试集合，窗口显示后执行真实鼠标事件。 */
    TestCase {
        name: "AppButton"
        when: windowShown

        /*
         * 功能：实例化可用按钮并通过真实鼠标事件核对只收到一次点击。
         * 参数：无。
         * 返回：无；成功由 clickCount 从 0 到 1 的断言表示。
         * 失败：组件创建或点击计数不符合预期时 Qt Test 标记用例失败。
         * 副作用：只创建临时按钮、投递鼠标事件；测试框架在用例结束销毁实例，不调用业务服务。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_enabledButtonReceivesClick() {
            const button = createTemporaryObject(buttonComponent, root)
            verify(!!button, qsTr("组件存在"))
            compare(button.clickCount, 0)
            mouseClick(button, button.width / 2, button.height / 2)
            tryCompare(button, "clickCount", 1)
        }

        /*
         * 功能：禁用按钮后模拟鼠标点击，核对禁用状态不会形成业务点击。
         * 参数：无。
         * 返回：无；计数保持 0 是成功条件。
         * 失败：组件创建或禁用点击仍递增时断言失败。
         * 副作用：仅修改临时按钮 enabled、投递事件，Qt Test 自动清理实例，不读写用户数据。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_disabledButtonIgnoresClick() {
            const button = createTemporaryObject(buttonComponent, root)
            verify(!!button, qsTr("组件存在"))
            button.enabled = false
            mouseClick(button, button.width / 2, button.height / 2)
            tryCompare(button, "clickCount", 0)
        }
    }
}
