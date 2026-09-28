import QtQuick
import QtTest
import "../../ui/qml"

Item {
    id: root
    width: 1000
    height: 900
    UiTheme { id: theme }
    QtObject {
        id: emptySources
        property var sourceItems: []
        property int selectedIndex: -1
    }
    QtObject { id: emptyProviders; property var connections: [] }
    QtObject {
        id: backend
        property var jobs: []
        property bool busy: false
        property bool running: false
        property bool stopping: false
        property string activeJobId: ""
        property string statusText: ""
        property string errorText: ""
        property int startCount: 0
        property int pauseCount: 0
        property int cancelCount: 0
        property int sampleCount: 0
        /** @brief 只记录点击，不发送网络请求或写入用户工作区。 */
        function startJob(id) { startCount++; running = true; activeJobId = id }
        /** @brief 模拟检查点暂停的等待状态。 */
        function pauseJob(id) { pauseCount++; stopping = true }
        /** @brief 模拟取消确认，不删除任何真实数据。 */
        function cancelJob(id, revision) { cancelCount++; running = false; activeJobId = "" }
        /** @brief 记录单步入口，验证打开页面不会自动抽样。 */
        function runRemoteSample(id) { sampleCount++ }
    }
    Component {
        id: taskComponent
        TaskPage {
            width: 1000; height: 900
            uiTheme: theme
            jobModel: backend
            sourceModel: emptySources
            providerModel: emptyProviders
        }
    }
    TestCase {
        name: "TaskPageControls"
        when: windowShown
        /** @brief 每条用例使用独立内存状态，测试数据只存在于测试文件。 */
        function init() {
            backend.jobs = []
            backend.busy = false; backend.running = false; backend.stopping = false
            backend.activeJobId = ""; backend.startCount = 0; backend.pauseCount = 0
            backend.cancelCount = 0; backend.sampleCount = 0
        }
        /** @brief 提供无原文、无凭据的单个可运行任务快照。 */
        function setJob(remote, completed) {
            backend.jobs = [{ id: "owned-ui-job", sourceId: "", status: "queued", revision: 1,
                total: 4, completed: completed, cancelRequested: false, paused: completed > 0,
                estimatedTokens: 100, maxRequests: 8, consumedRequests: completed,
                outputTokenLimit: 2048, providerConnectionId: remote ? "owned-provider" : "",
                modelId: qsTr("测试模型"), priceKnown: false, problemOrdinal: 0, problemAttempt: 0,
                steps: [{ ordinal: completed + 1, status: "ready", start: 0, end: 100 }] }]
        }
        /** @brief 空白任务页与刷新数据不应调用任何解析入口。 */
        function test_noAutomaticSend() {
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "Component exists")
            setJob(true, 0)
            compare(backend.startCount, 0)
            compare(backend.sampleCount, 0)
        }
        /** @brief 离线开始可直接执行，运行时暂停可用而再次开始禁用。 */
        function test_offlineStartAndPause() {
            setJob(false, 0)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "Component exists")
            const start = findChild(page, "startJob_owned-ui-job")
            const pause = findChild(page, "pauseJob_owned-ui-job")
            verify(!!start, "Object exists"); verify(!!pause, "Object exists")
            mouseClick(start)
            tryCompare(backend, "startCount", 1)
            tryCompare(start, "enabled", false)
            tryCompare(pause, "enabled", true)
            mouseClick(pause)
            tryCompare(backend, "pauseCount", 1)
            tryCompare(pause, "enabled", false)
        }
        /** @brief 模型任务先确认；返回不执行，再次明确确认才启动一次。 */
        function test_remoteConfirmation() {
            setJob(true, 0)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "Component exists")
            const start = findChild(page, "startJob_owned-ui-job")
            const dialog = findChild(page, "fullConfirm")
            const back = findChild(page, "dismissFullRun")
            const confirm = findChild(page, "confirmFullRun")
            verify(!!start, "Object exists"); verify(!!dialog, "Object exists")
            verify(!!back, "Object exists"); verify(!!confirm, "Object exists")
            mouseClick(start)
            tryCompare(dialog, "visible", true)
            tryCompare(backend, "startCount", 0)
            mouseClick(back)
            tryCompare(dialog, "visible", false)
            tryCompare(backend, "startCount", 0)
            mouseClick(start)
            tryCompare(dialog, "visible", true)
            mouseClick(confirm)
            tryCompare(backend, "startCount", 1)
            tryCompare(dialog, "visible", false)
        }
        /** @brief 暂停后的任务显示继续入口，模型续跑仍需要确认。 */
        function test_resumeNeedsConfirmation() {
            setJob(true, 1)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "Component exists")
            const start = findChild(page, "startJob_owned-ui-job")
            const dialog = findChild(page, "fullConfirm")
            verify(!!start, "Object exists"); verify(!!dialog, "Object exists")
            compare(start.text, qsTr("继续解析"))
            mouseClick(start)
            tryCompare(dialog, "visible", true)
            tryCompare(dialog, "remaining", 3)
            tryCompare(backend, "startCount", 0)
        }
        /** @brief 运行中即使列表刷新忙碌也能取消，且必须通过确认弹窗。 */
        function test_cancelWhileRunningAndRefreshing() {
            setJob(false, 0)
            backend.running = true; backend.activeJobId = "owned-ui-job"; backend.busy = true
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "Component exists")
            const cancel = findChild(page, "cancelJob_owned-ui-job")
            const dialog = findChild(page, "cancelConfirm")
            const confirm = findChild(page, "confirmCancelRun")
            verify(!!cancel, "Object exists"); verify(!!dialog, "Object exists"); verify(!!confirm, "Object exists")
            compare(cancel.enabled, true)
            mouseClick(cancel)
            tryCompare(dialog, "visible", true)
            tryCompare(backend, "cancelCount", 0)
            mouseClick(confirm)
            tryCompare(backend, "cancelCount", 1)
            tryCompare(backend, "running", false)
        }
    }
}
