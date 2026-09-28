import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: page
    required property var uiTheme
    // 测试可替换为无网络端口；正式界面仍使用工作区的真实视图模型。
    property var jobModel: extractionJobs
    property var sourceModel: sources
    property var providerModel: providers
    signal navigate(int page)
    // 将持久化状态码转换为仅含中文的界面状态。
    function statusLabel(status) {
        switch (status) {
        case "ready": case "pending": case "queued": return qsTr("待处理")
        case "running": return qsTr("进行中")
        case "cancelling": return qsTr("正在取消")
        case "completed": return qsTr("已完成")
        case "failed": return qsTr("失败")
        case "unknown": return qsTr("结果未知")
        case "cancelled": return qsTr("已取消")
        case "needs_attention": return qsTr("需要处理")
        default: return qsTr("状态待确认")
        }
    }
    // 只接受完整的正整数预算；调用次数留空时交由后端按片段数计算上限。
    function budgetInputValid() {
        const requestText = requestLimit.text.trim()
        return chunkSize.acceptableInput && overlap.acceptableInput && outputLimit.acceptableInput
            && Number(overlap.text) < Number(chunkSize.text) / 2
            && (requestText.length === 0 || (requestLimit.acceptableInput && Number(requestText) > 0))
    }
    // 按持久化顺序找到下一块待处理原文，供发送确认框展示其范围。
    function nextReadyStep(steps) {
        for (const step of steps) {
            if (step.status === "ready") return step
        }
        return null
    }
    contentWidth: width
    contentHeight: content.implicitHeight + 48
    clip: true

    ColumnLayout {
        id: content
        x: 24
        y: 24
        width: page.width - 48
        spacing: 16
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            implicitHeight: createForm.implicitHeight + 40
            ColumnLayout {
                id: createForm
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12
                Label { text: qsTr("创建解析任务"); color: page.uiTheme.text; font.pointSize: 13; font.weight: Font.DemiBold }
                Label {
                    text: qsTr("先选择小说。默认离线；模型任务仅在你明确确认开始后发送。")
                    color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("小说"); color: page.uiTheme.text }
                    AppComboBox {
                        id: sourceChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: page.sourceModel.sourceItems
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: page.sourceModel.selectedIndex >= 0 ? page.sourceModel.selectedIndex : 0
                        displayText: currentIndex < 0 ? qsTr("先导入小说") : currentText
                    }
                    AppButton { uiTheme: page.uiTheme; text: qsTr("查看章节"); onClicked: page.navigate(2) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("模型连接"); color: page.uiTheme.text }
                    AppComboBox {
                        id: providerChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: page.providerModel.connections
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: -1
                        displayText: currentIndex < 0 ? qsTr("未选择，使用离线规则") : currentText
                    }
                    AppButton { uiTheme: page.uiTheme; text: qsTr("管理连接"); onClicked: page.navigate(5) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("每片字数"); color: page.uiTheme.text }
                    AppField { id: chunkSize; uiTheme: page.uiTheme; text: "6000"; Layout.preferredWidth: 105; validator: IntValidator { bottom: 1000; top: 20000 } }
                    Label { text: qsTr("重叠字数"); color: page.uiTheme.text }
                    AppField { id: overlap; uiTheme: page.uiTheme; text: "300"; Layout.preferredWidth: 90; validator: IntValidator { bottom: 0; top: 2000 } }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("调用上限"); color: page.uiTheme.text }
                    AppField {
                        id: requestLimit
                        uiTheme: page.uiTheme
                        Layout.preferredWidth: 145
                        placeholderText: qsTr("留空自动计算")
                        validator: IntValidator { bottom: 1; top: 1000000 }
                    }
                    Label { text: qsTr("单次输出上限（词元）"); color: page.uiTheme.text }
                    AppField {
                        id: outputLimit
                        uiTheme: page.uiTheme
                        Layout.preferredWidth: 110
                        text: "2048"
                        validator: IntValidator { bottom: 1; top: 1000000 }
                    }
                    Item { Layout.fillWidth: true }
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("创建任务"); primary: true
                        enabled: page.sourceModel.sourceItems.length > 0 && sourceChoice.currentIndex >= 0
                            && page.budgetInputValid() && !page.jobModel.busy
                        onClicked: {
                            // 空白调用上限表示采用后端计算的片段数硬上限。
                            const cap = requestLimit.text.trim()
                            page.jobModel.createJob(sourceChoice.currentValue, Number(chunkSize.text),
                                                     Number(overlap.text), cap.length === 0 ? 0 : Number(cap),
                                                     Number(outputLimit.text),
                                                     providerChoice.currentIndex < 0 ? "" : providerChoice.currentValue)
                        }
                    }
                }
                Label {
                    text: qsTr("调用上限留空时按片段数自动计算；价格未知时不显示虚假的费用估算。")
                    color: page.uiTheme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
        Label { text: qsTr("任务列表"); color: page.uiTheme.text; font.pointSize: 13; font.weight: Font.DemiBold }
        Label {
            visible: page.jobModel.jobs.length === 0
            text: qsTr("暂无解析任务。导入小说后可在这里开始。")
            color: page.uiTheme.muted
        }
        Repeater {
            model: page.jobModel.jobs
            SectionPanel {
                id: jobCard
                required property var modelData
                readonly property bool active: page.jobModel.running && page.jobModel.activeJobId === modelData.id
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                implicitHeight: details.implicitHeight + 32
                ColumnLayout {
                    id: details
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: qsTr("任务 %1").arg(jobCard.modelData.id)
                            color: page.uiTheme.text
                            font.pointSize: 10
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: jobCard.active ? (page.jobModel.stopping ? qsTr("正在停止") : qsTr("解析中"))
                                : jobCard.modelData.paused ? qsTr("已暂停") : page.statusLabel(jobCard.modelData.status)
                            color: page.uiTheme.muted
                        }
                    }
                    Label {
                        text: qsTr("进度：%1／%2 片 · 输入估算约 %3 词元")
                            .arg(jobCard.modelData.completed).arg(jobCard.modelData.total)
                            .arg(jobCard.modelData.estimatedTokens)
                        color: page.uiTheme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: (jobCard.modelData.totalChapters || 0) > 0
                        text: qsTr("已处理章节：%1／%2 章")
                            .arg(jobCard.modelData.completedChapters || 0).arg(jobCard.modelData.totalChapters || 0)
                        color: page.uiTheme.muted
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("调用：%1／%2 次 · 单次输出上限：%3 词元")
                            .arg(jobCard.modelData.consumedRequests).arg(jobCard.modelData.maxRequests)
                            .arg(jobCard.modelData.outputTokenLimit)
                        color: page.uiTheme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: jobCard.modelData.providerConnectionId.length === 0
                            ? qsTr("离线任务，无远程调用费用")
                            : jobCard.modelData.priceKnown ? qsTr("模型价格已配置") : qsTr("模型价格未知，暂无法估算金额")
                        color: page.uiTheme.muted
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: jobCard.modelData.problemOrdinal > 0
                        text: qsTr("第 %1 片失败或结果未知。未知请求可能已计费；重新发送前请核对服务账单。")
                            .arg(jobCard.modelData.problemOrdinal)
                        color: page.uiTheme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: jobCard.modelData.consumedRequests >= jobCard.modelData.maxRequests
                            && jobCard.modelData.completed < jobCard.modelData.total
                        text: qsTr("调用预算已用尽。此任务不能继续领取片段，请调整参数新建任务。")
                        color: page.uiTheme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8
                        AppButton {
                            objectName: "startJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: jobCard.modelData.completed > 0 || jobCard.modelData.paused ? qsTr("继续解析") : qsTr("开始解析")
                            primary: true
                            enabled: !page.jobModel.busy && !page.jobModel.running && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status === "queued"
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            onClicked: {
                                if (jobCard.modelData.providerConnectionId.length > 0) {
                                    fullConfirm.jobId = jobCard.modelData.id
                                    fullConfirm.modelId = jobCard.modelData.modelId
                                    fullConfirm.remaining = jobCard.modelData.total - jobCard.modelData.completed
                                    fullConfirm.open()
                                } else page.jobModel.startJob(jobCard.modelData.id)
                            }
                        }
                        AppButton {
                            objectName: "pauseJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: qsTr("暂停解析")
                            enabled: jobCard.active && !page.jobModel.stopping
                            onClicked: page.jobModel.pauseJob(jobCard.modelData.id)
                        }
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("模型抽样 1 步")
                            enabled: !page.jobModel.busy && !page.jobModel.running && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status === "queued"
                                && jobCard.modelData.providerConnectionId.length > 0
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            onClicked: {
                                const step = page.nextReadyStep(jobCard.modelData.steps)
                                if (step === null) return
                                remoteConfirm.jobId = jobCard.modelData.id
                                remoteConfirm.ordinal = step.ordinal
                                remoteConfirm.startCodepoint = step.start
                                remoteConfirm.endCodepoint = step.end
                                remoteConfirm.open()
                            }
                        }
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("重试问题片段")
                            enabled: !page.jobModel.busy && !jobCard.active && jobCard.modelData.problemOrdinal > 0
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            onClicked: page.jobModel.retryStep(jobCard.modelData.id,
                                                                jobCard.modelData.problemOrdinal,
                                                                jobCard.modelData.problemAttempt)
                        }
                        AppButton {
                            objectName: "cancelJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: qsTr("取消任务")
                            enabled: (jobCard.active || !page.jobModel.busy) && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status !== "completed"
                                && jobCard.modelData.status !== "cancelled"
                                && jobCard.modelData.status !== "cancelling"
                            onClicked: {
                                cancelConfirm.jobId = jobCard.modelData.id
                                cancelConfirm.revision = jobCard.modelData.revision
                                cancelConfirm.open()
                            }
                        }
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("核查")
                            enabled: !page.jobModel.busy
                            onClicked: page.jobModel.auditJob(jobCard.modelData.id)
                        }
                    }
                }
            }
        }
        Label { text: page.jobModel.errorText.length > 0 ? page.jobModel.errorText : page.jobModel.statusText; color: page.jobModel.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        AppButton { uiTheme: page.uiTheme; text: qsTr("前往人工校对"); onClicked: page.navigate(8) }
    }

    Dialog {
        id: fullConfirm
        objectName: "fullConfirm"
        property string jobId: ""
        property string modelId: ""
        property int remaining: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: qsTr("确认开始模型解析"); color: page.uiTheme.text; font.pointSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Label {
                text: qsTr("将按任务参数把剩余 %1 个片段依次发送给模型“%2”，不超过任务的调用上限。可能产生费用。暂停或取消会在当前请求结束后生效。")
                    .arg(fullConfirm.remaining).arg(fullConfirm.modelId)
                color: page.uiTheme.text; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { objectName: "dismissFullRun"; uiTheme: page.uiTheme; text: qsTr("返回"); onClicked: fullConfirm.close() }
                AppButton {
                    objectName: "confirmFullRun"
                    uiTheme: page.uiTheme; text: qsTr("确认开始"); primary: true
                    enabled: fullConfirm.jobId.length > 0 && !page.jobModel.busy && !page.jobModel.running
                    onClicked: {
                        // 页面打开、刷新与取消弹窗均不发送；只有明确确认才启动批次。
                        const jobId = fullConfirm.jobId
                        fullConfirm.close()
                        page.jobModel.startJob(jobId)
                    }
                }
            }
        }
    }

    Dialog {
        id: remoteConfirm
        objectName: "remoteConfirm"
        property string jobId: ""
        property int ordinal: 0
        property int startCodepoint: 0
        property int endCodepoint: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                text: qsTr("确认发送小说片段")
                color: page.uiTheme.text
                font.pointSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("将把第 %1 片原文（码点 %2—%3）发送给所选模型连接，可能产生费用。每次确认只执行一步。")
                    .arg(remoteConfirm.ordinal).arg(remoteConfirm.startCodepoint).arg(remoteConfirm.endCodepoint)
                color: page.uiTheme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { uiTheme: page.uiTheme; text: qsTr("返回"); onClicked: remoteConfirm.close() }
                AppButton {
                    uiTheme: page.uiTheme
                    text: qsTr("确认发送 1 片")
                    primary: true
                    enabled: remoteConfirm.jobId.length > 0 && !page.jobModel.busy
                    onClicked: {
                        // 只有用户在此确认后才触发一次可能计费的远程解析。
                        const jobId = remoteConfirm.jobId
                        remoteConfirm.close()
                        page.jobModel.runRemoteSample(jobId)
                    }
                }
            }
        }
    }

    Dialog {
        id: cancelConfirm
        objectName: "cancelConfirm"
        property string jobId: ""
        property int revision: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        contentItem: ColumnLayout {
            spacing: 16
            Label { text: qsTr("确认取消解析任务"); color: page.uiTheme.text; font.pointSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Label {
                text: qsTr("未执行的片段将标记为已取消。已经发出的模型请求可能仍会完成并计费。")
                color: page.uiTheme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { uiTheme: page.uiTheme; text: qsTr("返回"); onClicked: cancelConfirm.close() }
                AppButton {
                    objectName: "confirmCancelRun"
                    uiTheme: page.uiTheme
                    text: qsTr("确认取消")
                    enabled: cancelConfirm.jobId.length > 0 && (!page.jobModel.busy
                        || page.jobModel.activeJobId === cancelConfirm.jobId)
                    onClicked: {
                        // 确认后提交当前任务修订，避免误取消或覆盖新状态。
                        const jobId = cancelConfirm.jobId
                        const revision = cancelConfirm.revision
                        cancelConfirm.close()
                        page.jobModel.cancelJob(jobId, revision)
                    }
                }
            }
        }
    }
}
