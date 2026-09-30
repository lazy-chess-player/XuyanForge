import QtQuick
import QtTest
import "../../ui/qml"

/* 任务页回归宿主，仅测试进程使用，在 GUI 线程拥有模型替身及临时页面，不读小说或凭据。 */
Item {
    id: root
    width: 1000
    height: 900
    /* 测试共享主题，宿主拥有，随宿主释放。 */
    UiTheme { id: theme }
    /* 空来源内存替身，仅提供列表与选择索引，不调用导入服务。 */
    QtObject {
        id: emptySources
        /* 测试来源列表，初始空；只供页面只读，不含正文，不调用导入。 */
        property var sourceItems: []
        /* 测试来源从零选择索引，初始 -1 表示无来源；本组用例保持未选。 */
        property int selectedIndex: -1
    }
    /* 空连接内存替身，connections 初始空数组，只供任务页读取，宿主拥有，无网络端口。 */
    QtObject { id: emptyProviders; property var connections: [] }
    /* 任务内存替身，计数用户动作并模拟状态，不创建真实任务或后台线程。 */
    QtObject {
        id: backend
        /* 内存任务快照数组，初始空；init 清空、setJob 生成单项，任务卡片读取，不写数据库。 */
        property var jobs: []
        /* 模拟列表刷新忙碌，无单位，初始 false；init 与取消用例修改，按钮可用性读取。 */
        property bool busy: false
        /* 模拟是否有批次运行，无单位，初始 false；开始、取消及用例更新，不拥有线程。 */
        property bool running: false
        /* 模拟等待检查点停止，无单位，初始 false；暂停设为 true，init 恢复，暂停按钮读取。 */
        property bool stopping: false
        /* 模拟运行任务身份，初始空；开始与用例写入，取消及 init 清空，卡片核对身份。 */
        property string activeJobId: ""
        /* 替身中文状态说明，初始空；只供页面反馈读取，本组用例不伪造进度说明。 */
        property string statusText: ""
        /* 替身错误说明，初始空；页面读取，本组用例不生成原文或敏感错误。 */
        property string errorText: ""
        /* 开始接口调用次数，单位次，初始 0；startJob 递增，init 清零，用例验证确认边界。 */
        property int startCount: 0
        /* 暂停接口调用次数，单位次，初始 0；pauseJob 递增，init 清零，用例读取。 */
        property int pauseCount: 0
        /* 取消接口调用次数，单位次，初始 0；cancelJob 递增，init 清零，用例核对退出及确认行为。 */
        property int cancelCount: 0
        /* 单步抽样接口调用次数，单位次，初始 0；runRemoteSample 递增，init 清零，未确认必须为零。 */
        property int sampleCount: 0
        /*
         * 功能：模拟开始动作，记录一次调用并把内存替身标记为正在执行。
         * 参数：id 为输入的测试任务稳定标识，测试使用非空字符串；不校验存在性，不持有真实任务。
         * 返回：无。
         * 失败：本替身不校验任务或预算；属性赋值异常由 Qt Test 报告，不模拟真实服务成功保证。
         * 副作用：递增 startCount、设置 running 和 activeJobId；没有线程、网络或数据库写入。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function startJob(id) { startCount++; running = true; activeJobId = id }
        /*
         * 功能：模拟检查点暂停请求，核对界面进入正在停止状态。
         * 参数：id 为输入测试任务身份，仅保持调用签名，本替身不读取或校验。
         * 返回：无。
         * 失败：不模拟真实任务失败；赋值错误交给测试框架。
         * 副作用：递增 pauseCount 并设 stopping 为 true，不取消任何网络请求，不管理线程。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function pauseJob(id) { pauseCount++; stopping = true }
        /*
         * 功能：模拟确认取消后停止当前内存任务，供取消按钮用例断言。
         * 参数：id 为输入测试任务身份；revision 为输入预期修订，无单位；本替身均不校验，仅保留接口签名。
         * 返回：无。
         * 失败：不模拟真实修订冲突或取消失败；赋值异常由测试框架报告。
         * 副作用：递增 cancelCount、清除 running 与 activeJobId；不删除数据，没有后台线程。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function cancelJob(id, revision) { cancelCount++; running = false; activeJobId = "" }
        /*
         * 功能：记录单步抽样入口是否被调用，验证确认退出不会发送。
         * 参数：id 为输入的测试任务身份，本替身不读取或校验，不接触原文。
         * 返回：无。
         * 失败：不执行远程服务或模拟响应；赋值异常交给 Qt Test。
         * 副作用：仅递增 sampleCount，无网络、费用或持久化副作用。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function runRemoteSample(id) { sampleCount++ }
    }
    /* 任务页组件工厂，临时页面寿命由 Qt Test 管理。 */
    Component {
        id: taskComponent
        /* 任务页测试定义，显式注入三类内存替身，隔绝真实来源、连接及任务服务。 */
        TaskPage {
            width: 1000; height: 900
            uiTheme: theme
            jobModel: backend
            sourceModel: emptySources
            providerModel: emptyProviders
        }
    }
    /* 任务页交互集合，GUI 线程验证确认、返回、暂停和取消，没有网络请求。 */
    TestCase {
        name: "TaskPageControls"
        when: windowShown
        /*
         * 功能：清空任务替身的队列、运行状态及动作计数，让每条用例从独立内存状态开始。
         * 参数：无。
         * 返回：无。
         * 失败：替身属性不可写时异常由 Qt Test 标记初始化失败。
         * 副作用：只重置 backend 内存，不创建工作区；此前临时页面由框架按用例清理。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function init() {
            backend.jobs = []
            backend.busy = false; backend.running = false; backend.stopping = false
            backend.activeJobId = ""; backend.startCount = 0; backend.pauseCount = 0
            backend.cancelCount = 0; backend.sampleCount = 0
            backend.statusText = ""; backend.errorText = ""
        }
        /*
         * 功能：生成一条无正文、无凭据的可运行测试任务，替换内存列表以驱动卡片。
         * 参数：remote 为输入布尔值，true 使用测试连接身份、false 离线；completed 为已提交片数，测试范围 0—3，无默认值。
         * 返回：无。
         * 失败：不校验参数范围；越界会形成与本组约定不符的快照，测试调用方须提供合法值。
         * 副作用：替换 backend.jobs 单项数组和步骤元数据，不建真实任务，不发送网络请求。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function setJob(remote, completed) {
            /* 快照只存在于本测试替身，不创建正文、数据库、远程任务或真实模型结果。 */
            backend.jobs = [{
                /* 测试任务稳定身份，固定非空；页面按钮命名和替身运行身份使用。 */
                id: "owned-ui-job",
                /* 来源身份为空，本用例不需要或读取原文。 */
                sourceId: "",
                /* 持久化状态协议固定 queued；运行标志另由替身模拟，页面中文映射读取。 */
                status: "queued",
                /* 预期修订固定 1，无单位；取消确认读取，替身不校验冲突。 */
                revision: 1,
                /* 本任务总片数固定 4，单位片，仅供进度与剩余范围说明。 */
                total: 4,
                /* 已完成片数由调用方输入，不包含实际小说处理。 */
                completed: completed,
                /* 初始没有取消请求，取消接口只改内存运行标志。 */
                cancelRequested: false,
                /* 已完成片数非零时模拟暂停，驱动“继续解析”文案。 */
                paused: completed > 0,
                /* 输入估算固定 100 词元，只供布局，不代表真实发送用量。 */
                estimatedTokens: 100,
                /* 测试预算上限 8 次，保证本组合法快照仍可操作。 */
                maxRequests: 8,
                /* 模拟已消耗次数与 completed 一致，单位次，不计真实费用。 */
                consumedRequests: completed,
                /* 每次输出上限固定 2048 词元，界面只读展示。 */
                outputTokenLimit: 2048,
                /* remote 控制测试连接身份，空身份表示离线，不指向任何真实端点。 */
                providerConnectionId: remote ? "owned-provider" : "",
                /* 模型显示名称仅测试使用，不作为真实模型协议值。 */
                modelId: qsTr("测试模型"),
                /* 价格保持未知，不伪造金额或费率。 */
                priceKnown: false,
                /* 问题步骤序号为 0，表示本组正常快照没有失败步骤。 */
                problemOrdinal: 0,
                /* 问题尝试号为 0，未发生问题调用，不允许自动重试。 */
                problemAttempt: 0,
                /* 单项步骤只提供确认元数据，不持有或截取正文；测试过程中不修改。 */
                steps: [{
                    /* 下一待处理片的一基序号，由已完成片数推导。 */
                    ordinal: completed + 1,
                    /* 步骤协议值 ready，只供确认函数选择，不触发调度。 */
                    status: "ready",
                    /* 测试范围起点为全文第 0 Unicode 码点，不是 UTF-16 偏移。 */
                    start: 0,
                    /* 测试范围排他终点为第 100 Unicode 码点，无实际小说资产。 */
                    end: 100
                }]
            }]
        }
        /*
         * 功能：创建空页后更新远程任务列表，核对初始化及数据刷新不调用批次或抽样。
         * 参数：无。
         * 返回：无；startCount 与 sampleCount 均为 0 才成功。
         * 失败：页面创建失败或列表刷新造成自动发送入口调用时断言失败。
         * 副作用：创建临时页面并设置内存列表；框架清理页面，不启动真实线程或请求。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_noAutomaticSend() {
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            setJob(true, 0)
            compare(backend.startCount, 0)
            compare(backend.sampleCount, 0)
        }

        /*
         * 功能：核对已知状态使用中文标签，未知或空状态给出待确认提示而不回显内部英文码。
         * 参数：无。返回：无；各状态文案及零发送计数断言成立才成功。
         * 失败：组件创建、状态中文回退或只读映射行为改变时断言失败。
         * 副作用：仅创建临时页面并调用状态映射，不填充任务、不启动后台或网络；框架负责释放。
         * 线程与生命周期：GUI 线程同步执行，所有输入为测试状态字符串。
         */
        function test_statusLabelsAndUnknownFallback() {
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, "组件存在")
            compare(page.statusLabel("queued"), qsTr("待处理"))
            compare(page.statusLabel("running"), qsTr("进行中"))
            compare(page.statusLabel("unknown"), qsTr("结果未知"))
            compare(page.statusLabel("future-state"), qsTr("状态待确认"))
            compare(page.statusLabel(""), qsTr("状态待确认"))
            compare(backend.startCount, 0)
            compare(backend.sampleCount, 0)
        }
        /*
         * 功能：点击离线开始及暂停，核对一次开始、重复开始禁用与暂停后入口禁用。
         * 参数：无。
         * 返回：无；计数与可用性均按预期变化才成功。
         * 失败：页面或按钮缺失，或事件后状态未在等待期内更新时失败。
         * 副作用：只操作内存替身与临时控件，Qt Test 清理页面，无真实任务或费用。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_offlineStartAndPause() {
            setJob(false, 0)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const start = findChild(page, "startJob_owned-ui-job")
            const pause = findChild(page, "pauseJob_owned-ui-job")
            verify(!!start, qsTr("对象存在")); verify(!!pause, qsTr("对象存在"))
            mouseClick(start)
            tryCompare(backend, "startCount", 1)
            tryCompare(start, "enabled", false)
            tryCompare(pause, "enabled", true)
            mouseClick(pause)
            tryCompare(backend, "pauseCount", 1)
            tryCompare(pause, "enabled", false)
        }
        /*
         * 功能：点击远程开始，先返回保持零调用，再次打开并明确确认只启动一次。
         * 参数：无。
         * 返回：无；弹窗可见性和 startCount 的阶段断言表示成功。
         * 失败：具名控件缺失或返回导致调用、确认未关闭弹窗等情况断言失败。
         * 副作用：投递鼠标事件并改变内存替身，临时弹窗随页面清理，不访问网络。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_remoteConfirmation() {
            setJob(true, 0)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const start = findChild(page, "startJob_owned-ui-job")
            const dialog = findChild(page, "fullConfirm")
            const back = findChild(page, "dismissFullRun")
            const confirm = findChild(page, "confirmFullRun")
            verify(!!start, qsTr("对象存在")); verify(!!dialog, qsTr("对象存在"))
            verify(!!back, qsTr("对象存在")); verify(!!confirm, qsTr("对象存在"))
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
        /*
         * 功能：构造已完成一片的暂停任务，核对继续文字和三片剩余范围，仍需确认。
         * 参数：无。
         * 返回：无；弹窗显示且 startCount 保持 0 才成功。
         * 失败：控件缺失、剩余片数错误或续跑未经确认启动时断言失败。
         * 副作用：设置内存快照、打开临时弹窗，Qt Test 清理实例；不执行真实续跑。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_resumeNeedsConfirmation() {
            setJob(true, 1)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const start = findChild(page, "startJob_owned-ui-job")
            const dialog = findChild(page, "fullConfirm")
            verify(!!start, qsTr("对象存在")); verify(!!dialog, qsTr("对象存在"))
            compare(start.text, qsTr("继续解析"))
            mouseClick(start)
            tryCompare(dialog, "visible", true)
            tryCompare(dialog, "remaining", 3)
            tryCompare(backend, "startCount", 0)
        }
        /*
         * 功能：设置单步弹窗公开范围并按退出键关闭，核对抽样与批次入口仍为零。
         * 参数：无。
         * 返回：无；弹窗隐藏且两个计数均为 0 才成功。
         * 失败：弹窗不存在、退出键未关闭或出现发送入口调用时断言失败。
         * 副作用：只写临时确认字段、转移焦点并投递键盘事件，不加载正文；框架清理页面。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_remoteEscapeDoesNotSend() {
            setJob(true, 0)
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const dialog = findChild(page, "remoteConfirm")
            verify(!!dialog, qsTr("对象存在"))
            // 只准备弹窗的公开状态，关闭路径不访问未命名按钮或真实网络端口。
            dialog.jobId = "owned-ui-job"
            dialog.ordinal = 1; dialog.startCodepoint = 0; dialog.endCodepoint = 100
            dialog.open()
            tryCompare(dialog, "visible", true)
            tryCompare(backend, "sampleCount", 0)
            dialog.contentItem.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "visible", false)
            tryCompare(backend, "sampleCount", 0)
            tryCompare(backend, "startCount", 0)
        }
        /*
         * 功能：在模拟运行中打开取消确认，按退出键返回，核对没有取消且仍运行。
         * 参数：无。
         * 返回：无；cancelCount 为 0、running 为 true 且弹窗关闭才成功。
         * 失败：控件缺失或退出键触发取消、未关闭弹窗时断言失败。
         * 副作用：只改变测试运行标志、操作临时弹窗，框架清理；不影响真实后台任务。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_cancelEscapeKeepsRunning() {
            setJob(false, 0)
            backend.running = true; backend.activeJobId = "owned-ui-job"
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const cancel = findChild(page, "cancelJob_owned-ui-job")
            const dialog = findChild(page, "cancelConfirm")
            verify(!!cancel, qsTr("对象存在")); verify(!!dialog, qsTr("对象存在"))
            mouseClick(cancel)
            tryCompare(dialog, "visible", true)
            dialog.contentItem.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "visible", false)
            tryCompare(backend, "cancelCount", 0)
            tryCompare(backend, "running", true)
        }
        /*
         * 功能：模拟运行任务与列表忙碌，点击取消再明确确认，核对忙碌不能阻挡运行中取消。
         * 参数：无。
         * 返回：无；确认前为 0 次、确认后为 1 次且 running 为 false 才成功。
         * 失败：取消入口不可用、弹窗缺失或确认后状态不符时断言失败。
         * 副作用：只设置内存替身和投递点击，临时页面自动清理，不删除真实数据。
         * 线程与生命周期：GUI 线程中同步执行；事件断言等待 Qt 事件处理，临时对象由用例机制清理。
         */
        function test_cancelWhileRunningAndRefreshing() {
            setJob(false, 0)
            backend.running = true; backend.activeJobId = "owned-ui-job"; backend.busy = true
            const page = createTemporaryObject(taskComponent, root)
            verify(!!page, qsTr("组件存在"))
            const cancel = findChild(page, "cancelJob_owned-ui-job")
            const dialog = findChild(page, "cancelConfirm")
            const confirm = findChild(page, "confirmCancelRun")
            verify(!!cancel, qsTr("对象存在")); verify(!!dialog, qsTr("对象存在")); verify(!!confirm, qsTr("对象存在"))
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
