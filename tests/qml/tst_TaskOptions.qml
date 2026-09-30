import QtQuick
import "../../ui/qml"

/* 解析页选项回归宿主，GUI线程拥有无正文、无网络能力的内存对象，仅测试目标加载。 */
Item {
    id: root
    width: 1000
    height: 800
    /* 共享测试主题，默认深色，清理阶段恢复。 */
    UiTheme { id: theme }
    /* 来源替身仅含名称与身份，不创建小说文件，随宿主释放。 */
    QtObject {
        id: emptySources
        /* 来源列表，默认空；用例显式创建元数据，控件只读。 */
        property var sourceItems: []
        /* 零基选择索引，默认-1表示没有来源。 */
        property int selectedIndex: -1
    }
    /* 连接替身不包含密钥、端点或传输能力，随宿主释放。 */
    QtObject {
        id: emptyProviders
        /* 连接名称与身份数组，默认空，用例显式设置，页面只读。 */
        property var connections: []
    }
    /* 任务替身只支持空态读取，无开始、抽样、创建或重试入口。 */
    QtObject {
        id: emptyJobs
        /* 无任务的列表，始终为空，界面不能产生任务卡片。 */
        property var jobs: []
        /* 无后台操作的忙碌状态，默认false，不修改。 */
        property bool busy: false
        /* 运行状态，默认false；替身不拥有线程。 */
        property bool running: false
        /* 中文状态提示，默认空，不虚构进度。 */
        property string statusText: ""
        /* 中文错误提示，默认空，不接触真实模型。 */
        property string errorText: ""
    }
    /* 真实页面工厂，三个模型显式注入，临时实例由Qt Test清理。 */
    Component {
        id: pageComponent
        TaskPage { uiTheme: theme; width: 1000; height: 800; sourceModel: emptySources; providerModel: emptyProviders; jobModel: emptyJobs }
    }
    /* 测试集合只操作真实下拉代理，不点击创建或发送按钮。 */
    OptionTestCase {
        name: "TaskChineseOptions"
        when: windowShown
        /* 功能：提供深浅色用例。参数：无。返回：两项主题数据。
         * 失败：数组分配异常交测试框架；副作用：无，不改变主题。 */
        function test_choices_data() { return [{tag: "dark", mode: "dark"}, {tag: "light", mode: "light"}] }
        /* 功能：核对空态中文，然后实际点击每个来源与连接名称，保证选项不自动发送。
         * 参数：data为框架提供的主题行，mode须为dark/light。
         * 返回：无；失败：默认标签、代理标签或协议身份不符时断言失败。
         * 副作用：只更新测试元数据和控件选择；替身无发送入口，不生成正文或数据库。 */
        function test_choices(data) {
            theme.mode = data.mode
            const page = createTemporaryObject(pageComponent, root)
            verify(!!page, qsTr("组件存在"))
            const novels = findChild(page, "sourceChoice")
            const connections = findChild(page, "providerChoice")
            verify(!!novels && !!connections, qsTr("选项控件存在"))
            compare(novels.displayText, qsTr("先导入小说"))
            compare(connections.displayText, qsTr("未选择，使用离线规则"))
            emptySources.sourceItems = [{id: "source-a", name: qsTr("用户来源甲")}, {id: "source-b", name: qsTr("用户来源乙")}]
            emptyProviders.connections = [{id: "connection-a", name: qsTr("用户连接甲")}, {id: "connection-b", name: qsTr("用户连接乙")}]
            chooseOption(novels, 0, qsTr("用户来源甲"), "source-a")
            chooseOption(novels, 1, qsTr("用户来源乙"), "source-b")
            chooseOption(connections, 0, qsTr("用户连接甲"), "connection-a")
            chooseOption(connections, 1, qsTr("用户连接乙"), "connection-b")
            compare(emptyJobs.jobs.length, 0)
        }
        /* 功能：恢复空列表及深色。参数：无。返回：无。
         * 失败：赋值错误交框架；副作用：只清理内存，页面实例由Qt Test销毁。 */
        function cleanup() { emptySources.sourceItems = []; emptyProviders.connections = []; theme.mode = "dark" }
    }
}
