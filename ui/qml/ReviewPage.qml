import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 候选校对页：GUI 线程显示当前世界后端候选页和选中引文，审核经模型，不直接写事实。 */
Item {
    id: page
    /* 窗口共享主题的观察引用，无默认值，调用方负责覆盖页面生命周期。 */
    required property var uiTheme
    /* 功能：请求切页。参数：page 为内部整数路由。返回：无。
     * 失败：无接收者无动作。副作用：窗口可能切页及刷新模型；GUI 线程连接随页面存活。 */
    signal navigate(int page)

    /* 待校对列表与证据详情左右布局。 */
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        /* 候选列表面板，宽度 310 像素，保留现有布局。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 310
            Layout.fillHeight: true
            /* 列表标题、空态和列表纵向布局。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                /* 待校对候选标题。 */
                Label { text: qsTr("待校对候选"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                /* 空候选提示，依据当前列表，不生成任何推断结果。 */
                Label { visible: candidateReview.candidates.length === 0; text: qsTr("暂无待校对内容。解析完成后会显示在这里。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 候选列表，读取模型当前页，不在 QML 中载入全工作区候选。 */
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: candidateReview.candidates
                    /* 候选行代理，显示名称并按页内索引选择候选。 */
                    delegate: ItemDelegate {
                        /* 当前候选页的单行元数据，由列表注入，无默认值；代理只读名称，不复制完整原文。 */
                        required property var modelData
                        /* 候选当前页从零行索引，由列表注入；高亮和 selectCandidate 读取，随模型行更新。 */
                        required property int index
                        width: list.width
                        text: modelData.name
                        highlighted: candidateReview.selectedIndex === index
                        /*
                         * 功能：按当前页内 index 选择候选供人工核对。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无效或失效候选由模型处理。
                         * 副作用：更新选中候选和证据，不审核或写世界事实。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: candidateReview.selectCandidate(index)
                    }
                }
            }
        }
        /* 证据和审核详情面板，占据右侧剩余空间。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            /* 详情字段、引文与动作纵向布局。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                /* 候选详情标题。 */
                Label { text: qsTr("候选详情"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                /* 人工核对边界提示，拒绝不会成为世界资料。 */
                Label { text: qsTr("核对原文证据后再接受。被拒绝的候选不会写入世界资料。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 候选名称字段标签。 */
                Label { text: qsTr("名称"); color: page.uiTheme.muted }
                /* 名称编辑草稿，初始来自 selectedName，仅审核提交时读取用户修改。 */
                AppField { id: candidateName; uiTheme: page.uiTheme; Layout.fillWidth: true; text: candidateReview.selectedName }
                /* 逐字原文证据标签。 */
                Label { text: qsTr("原文证据"); color: page.uiTheme.muted }
                /* 选中候选引文的滚动容器。 */
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    /* 只读证据文本，绑定 selectedQuote，不修改原文、不加载全书。 */
                    TextArea {
                        readOnly: true
                        text: candidateReview.selectedQuote
                        wrapMode: TextEdit.Wrap
                        color: page.uiTheme.text
                        /* 引文区域次级主题背景。 */
                        background: Rectangle { color: page.uiTheme.surfaceAlt; radius: 6 }
                    }
                }
                /* 后端格式化的原文范围说明，不用章节顺序猜测故事时间。 */
                Label { text: candidateReview.selectedRange; color: page.uiTheme.muted; font.pointSize: 9 }
                /* 接受、拒绝及资料导航操作行。 */
                RowLayout {
                    Layout.fillWidth: true
                    /* 接受入口，沿用现有字段与来源性质提交，不在页面将模型结果升级为事实。 */
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("接受")
                        enabled: candidateReview.selectedIndex >= 0
                        /*
                         * 功能：提交 accepted 审核，读取名称草稿并保留 selectedFields 与 selectedProvenance。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：修订冲突、字段、证据或保存失败由模型报告；页面不自动补关系端点。
                         * 副作用：模型持久化审核及适用资料投影，来源性质仍由现有审核契约约束。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: candidateReview.reviewSelected("accepted", candidateName.text,
                                                                   candidateReview.selectedFields,
                                                                   candidateReview.selectedProvenance)
                    }
                    /* 拒绝入口，记录审核终结而不创建资料。 */
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("拒绝")
                        enabled: candidateReview.selectedIndex >= 0
                        /*
                         * 功能：提交 rejected 审核，保留所选字段和来源性质作为审核输入。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：候选失效、修订冲突或写入失败由模型报告。
                         * 副作用：请求记录拒绝与历史，不创建世界资料。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: candidateReview.reviewSelected("rejected", candidateName.text,
                                                                   candidateReview.selectedFields,
                                                                   candidateReview.selectedProvenance)
                    }
                    /* 弹性空白，将资料导航放在操作行右侧。 */
                    Item { Layout.fillWidth: true }
                    /* 世界资料导航入口，点击只请求切页。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("查看世界资料");
                        /*
                         * 功能：请求世界资料页，内部路由 1。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无接收者时无动作。
                         * 副作用：发出 navigate，不自动保存审核草稿。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: page.navigate(1) }
                }
                /* 审核结果反馈，优先展示模型错误。 */
                Label { text: candidateReview.errorText.length > 0 ? candidateReview.errorText : candidateReview.statusText; color: candidateReview.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
