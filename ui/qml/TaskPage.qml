import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 解析任务页：GUI 线程观察任务、来源及连接模型，持有确认快照，长任务寿命由外部视图模型管理。 */
Flickable {
    id: page
    /* 外部共享主题观察引用，无默认值，调用方须保证其覆盖任务页面生命周期。 */
    required property var uiTheme
    /* 任务模型观察引用，默认 extractionJobs；页面读取状态并分派动作，测试可注入无网络替身，页面不拥有线程。 */
    property var jobModel: extractionJobs
    /* 来源模型观察引用，默认 sources；提供当前世界来源及选中索引，测试可注入空列表替身。 */
    property var sourceModel: sources
    /* 连接模型观察引用，默认 providers；仅提供连接名称和身份，不由本属性读取密钥或探测网络。 */
    property var providerModel: providers
    /* 功能：请求切换页面。参数：page 为内部整数路由。返回：无。
     * 失败：无接收者时无动作。副作用：接收者可切页，GUI 线程发出；切页不取消模型持有的任务。 */
    signal navigate(int page)
    /* 功能：将任务或步骤状态协议映射成中文词条，保持内部状态值不变。
     * 参数：status 为输入状态字符串，空值、未识别值均走回退。
     * 返回：已知状态的 qsTr 文字或“状态待确认”。失败：不抛业务错误，未知状态不回显英文。
     * 副作用：只读映射，不改变任务。线程与生命周期：GUI 线程同步调用，不持有参数或调度任务。 */
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
    /* 功能：校验片长、重叠和预算输入是否可用于创建任务，调用次数空白保留自动计算语义。
     * 参数：无。返回：全部整数合法、重叠小于片长一半且输出上限有效时 true，否则 false。
     * 失败：只报告布尔状态，不显示错误；数字和范围校验读取现有控件的 acceptableInput。
     * 副作用：只读四个输入框，不改写输入或创建任务。线程与生命周期：GUI 线程随页面控件同步求值。 */
    function budgetInputValid() {
        const requestText = requestLimit.text.trim()
        return chunkSize.acceptableInput && overlap.acceptableInput && outputLimit.acceptableInput
            && Number(overlap.text) < Number(chunkSize.text) / 2
            && (requestText.length === 0 || (requestLimit.acceptableInput && Number(requestText) > 0))
    }
    /* 功能：按任务快照顺序取第一条 ready 步骤，供单步发送确认展示原文范围。
     * 参数：steps 为输入的可迭代步骤集合，每项须含 status，确认还读取 ordinal/start/end；不拥有元素。
     * 返回：首条待处理步骤的观察引用；空集合或无 ready 项返回 null。
     * 失败：不可迭代或含 null 元素时访问错误向调用方传播，不跳过坏快照猜测范围。
     * 副作用：只读集合，不领取或发送步骤。线程与生命周期：GUI 线程同步调用，调用方立即复制确认字段。 */
    function nextReadyStep(steps) {
        for (const step of steps) {
            if (step.status === "ready") return step
        }
        return null
    }
    contentWidth: width
    contentHeight: content.implicitHeight + 48
    clip: true

    /* 任务创建、列表与校对入口的纵向滚动内容。 */
    ColumnLayout {
        id: content
        x: 24
        y: 24
        width: page.width - 48
        spacing: 16
        /* 创建参数面板，根据表单隐式高度适配，不预置任何任务。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            implicitHeight: createForm.implicitHeight + 40
            /* 来源、连接、切片及预算字段的纵向布局。 */
            ColumnLayout {
                id: createForm
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12
                /* 任务创建分区标题。 */
                Label { text: qsTr("创建解析任务"); color: page.uiTheme.text; font.pointSize: 13; font.weight: Font.DemiBold }
                /* 创建前说明，保持默认离线及显式发送边界。 */
                Label {
                    text: qsTr("先选择小说。默认离线；模型任务仅在你明确确认开始后发送。")
                    color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                /* 来源选择与章节导航操作行。 */
                RowLayout {
                    Layout.fillWidth: true
                    /* 小说来源标签。 */
                    Label { text: qsTr("小说"); color: page.uiTheme.text }
                    /* 来源下拉框，读取 sourceItems，身份 id 与名称分离，初始选择跟随来源模型。 */
                    AppComboBox {
                        id: sourceChoice
                        objectName: "sourceChoice"
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: page.sourceModel.sourceItems
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: page.sourceModel.selectedIndex >= 0 ? page.sourceModel.selectedIndex : 0
                        displayText: count === 0 || currentIndex < 0 ? qsTr("先导入小说") : currentText
                    }
                    /* 章节页导航入口，点击不创建或启动任务。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("查看章节");
                        /*
                         * 功能：请求查看章节，路由 2。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无接收者无动作。
                         * 副作用：发出 navigate，不创建任务。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: page.navigate(2) }
                }
                /* 连接选择与模型管理操作行。 */
                RowLayout {
                    Layout.fillWidth: true
                    /* 模型连接标签。 */
                    Label { text: qsTr("模型连接"); color: page.uiTheme.text }
                    /* 连接下拉框，初始 -1 表示离线；读取连接名称和稳定身份，不提供默认远程连接。 */
                    AppComboBox {
                        id: providerChoice
                        objectName: "providerChoice"
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: page.providerModel.connections
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: -1
                        displayText: currentIndex < 0 ? qsTr("未选择，使用离线规则") : currentText
                    }
                    /* 连接管理导航入口，点击不探测服务。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("管理连接");
                        /*
                         * 功能：请求管理模型连接，路由 5。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无接收者无动作。
                         * 副作用：发出 navigate，不探测网络。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: page.navigate(5) }
                }
                /* 片段与重叠参数横向布局。 */
                RowLayout {
                    Layout.fillWidth: true
                    /* 片段长度标签，界面字数实际按 Unicode 码点提交。 */
                    Label { text: qsTr("每片字数"); color: page.uiTheme.text }
                    /* 片段长度输入，默认 6000 码点，整数校验器限定 1000—20000；创建动作读取。 */
                    AppField { id: chunkSize; uiTheme: page.uiTheme; text: "6000"; Layout.preferredWidth: 105;
                        /* 片长输入整数校验器，单位 Unicode 码点；控件拥有，范围 1000—20000，不代替后端验证。 */
                        validator: IntValidator { bottom: 1000; top: 20000 } }
                    /* 相邻片段重叠长度标签，单位为 Unicode 码点。 */
                    Label { text: qsTr("重叠字数"); color: page.uiTheme.text }
                    /* 重叠输入，默认 300 码点，校验器限定 0—2000；还需小于片长的一半。 */
                    AppField { id: overlap; uiTheme: page.uiTheme; text: "300"; Layout.preferredWidth: 90;
                        /* 重叠输入整数校验器，单位 Unicode 码点；控件拥有，范围 0—2000，表单另验证片长的一半。 */
                        validator: IntValidator { bottom: 0; top: 2000 } }
                    /* 参数行弹性留白，保持现有控件位置。 */
                    Item { Layout.fillWidth: true }
                }
                /* 调用次数与输出词元上限横向布局。 */
                RowLayout {
                    Layout.fillWidth: true
                    /* 累计调用次数硬上限标签。 */
                    Label { text: qsTr("调用上限"); color: page.uiTheme.text }
                    /* 次数上限输入，初始空表示后端按片数计算，不把空值转换成无限预算。 */
                    AppField {
                        id: requestLimit
                        uiTheme: page.uiTheme
                        Layout.preferredWidth: 145
                        placeholderText: qsTr("留空自动计算")
                        /* 调用次数整数校验器，限制 1—1000000 次，空输入的特殊含义由表单函数处理。 */
                        validator: IntValidator { bottom: 1; top: 1000000 }
                    }
                    /* 单次输出词元上限标签，区别于原文字数。 */
                    Label { text: qsTr("单次输出上限（词元）"); color: page.uiTheme.text }
                    /* 输出上限输入，默认 2048 词元，创建动作读取。 */
                    AppField {
                        id: outputLimit
                        uiTheme: page.uiTheme
                        Layout.preferredWidth: 110
                        text: "2048"
                        /* 单次输出整数校验器，限制 1—1000000 词元，空值不合法。 */
                        validator: IntValidator { bottom: 1; top: 1000000 }
                    }
                    /* 预算行弹性留白，将创建动作靠右。 */
                    Item { Layout.fillWidth: true }
                    /* 任务创建入口，校验来源和参数，只创建队列，不自动发送原文。 */
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("创建任务"); primary: true
                        enabled: page.sourceModel.sourceItems.length > 0 && sourceChoice.currentIndex >= 0
                            && page.budgetInputValid() && !page.jobModel.busy
                        /*
                         * 功能：将来源、片长、重叠、调用次数及输出词元上限提交为新任务参数。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：输入有效性由 budgetInputValid 与模型再次校验，失败由 jobModel 显示。
                         * 副作用：调用 createJob；调用上限空白转 0 供后端自动计算，未选连接传空身份表示离线，创建不启动解析。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
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
                /* 调用预算及未知价格说明，不显示伪造费用估算。 */
                Label {
                    text: qsTr("调用上限留空时按片段数自动计算；价格未知时不显示虚假的费用估算。")
                    color: page.uiTheme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
        /* 解析任务列表标题。 */
        Label { text: qsTr("任务列表"); color: page.uiTheme.text; font.pointSize: 13; font.weight: Font.DemiBold }
        /* 空任务说明，依据真实 jobs 数组，不产生假进度。 */
        Label {
            visible: page.jobModel.jobs.length === 0
            text: qsTr("暂无解析任务。导入小说后可在这里开始。")
            color: page.uiTheme.muted
        }
        /* 任务卡片重复器，绑定模型当前任务列表，卡片销毁不等于后台取消。 */
        Repeater {
            model: page.jobModel.jobs
            /* 单任务卡片，读取持久化快照和当前后台身份，不拥有后台线程。 */
            SectionPanel {
                id: jobCard
                /* 当前任务轻量快照，由 Repeater 注入，无默认值；只读身份、计数及单步元数据，不持有正文。 */
                required property var modelData
                /* 本卡是否对应唯一运行批次，无单位；默认由模型推导，运行身份变化自动更新，控制暂停和取消入口。 */
                readonly property bool active: page.jobModel.running && page.jobModel.activeJobId === modelData.id
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                implicitHeight: details.implicitHeight + 32
                /* 任务详情和操作纵向布局。 */
                ColumnLayout {
                    id: details
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 8
                    /* 任务身份与状态操作行。 */
                    RowLayout {
                        Layout.fillWidth: true
                        /* 任务稳定标识说明，保留原始身份并省略中部，不把标识作为状态码。 */
                        Label {
                            text: qsTr("任务 %1").arg(jobCard.modelData.id)
                            color: page.uiTheme.text
                            font.pointSize: 10
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        /* 中文任务状态，活动身份优先读取 stopping，非活动时读取暂停与持久化状态。 */
                        Label {
                            text: jobCard.active ? (page.jobModel.stopping ? qsTr("正在停止") : qsTr("解析中"))
                                : jobCard.modelData.paused ? qsTr("已暂停") : page.statusLabel(jobCard.modelData.status)
                            color: page.uiTheme.muted
                        }
                    }
                    /* 已提交片段进度及粗略输入词元估算，不冒称实际账单。 */
                    Label {
                        text: qsTr("进度：%1／%2 片 · 输入估算约 %3 词元")
                            .arg(jobCard.modelData.completed).arg(jobCard.modelData.total)
                            .arg(jobCard.modelData.estimatedTokens)
                        color: page.uiTheme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    /* 完整已处理章节计数，没有章节总数时隐藏，不将片段完成混同整章完成。 */
                    Label {
                        visible: (jobCard.modelData.totalChapters || 0) > 0
                        text: qsTr("已处理章节：%1／%2 章")
                            .arg(jobCard.modelData.completedChapters || 0).arg(jobCard.modelData.totalChapters || 0)
                        color: page.uiTheme.muted
                        Layout.fillWidth: true
                    }
                    /* 已消耗调用次数与任务硬上限，读取持久化预算。 */
                    Label {
                        text: qsTr("调用：%1／%2 次 · 单次输出上限：%3 词元")
                            .arg(jobCard.modelData.consumedRequests).arg(jobCard.modelData.maxRequests)
                            .arg(jobCard.modelData.outputTokenLimit)
                        color: page.uiTheme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    /* 离线费用和价格配置状态，未知价格保持未知。 */
                    Label {
                        text: jobCard.modelData.providerConnectionId.length === 0
                            ? qsTr("离线任务，无远程调用费用")
                            : jobCard.modelData.priceKnown ? qsTr("模型价格已配置") : qsTr("模型价格未知，暂无法估算金额")
                        color: page.uiTheme.muted
                        Layout.fillWidth: true
                    }
                    /* 问题片段提示，失败或未知需要关注，不自动重发。 */
                    Label {
                        visible: jobCard.modelData.problemOrdinal > 0
                        text: qsTr("第 %1 片失败或结果未知。未知请求可能已计费；重新发送前请核对服务账单。")
                            .arg(jobCard.modelData.problemOrdinal)
                        color: page.uiTheme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    /* 调用次数耗尽提示，仍有未完成片段时说明需新建任务。 */
                    Label {
                        visible: jobCard.modelData.consumedRequests >= jobCard.modelData.maxRequests
                            && jobCard.modelData.completed < jobCard.modelData.total
                        text: qsTr("调用预算已用尽。此任务不能继续领取片段，请调整参数新建任务。")
                        color: page.uiTheme.danger
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    /* 任务动作流布局，按可用宽度换行而不改变动作顺序。 */
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8
                        /* 开始或续跑入口，远程先冻结弹窗快照，离线直接请求批次。 */
                        AppButton {
                            objectName: "startJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: jobCard.modelData.completed > 0 || jobCard.modelData.paused ? qsTr("继续解析") : qsTr("开始解析")
                            primary: true
                            enabled: !page.jobModel.busy && !page.jobModel.running && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status === "queued"
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            /*
                             * 功能：为远程任务准备全书确认；离线任务直接请求开始。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：卡片快照失效、状态或预算冲突由模型处理；准备弹窗不代表请求已发送。
                             * 副作用：远程复制任务 ID、模型 ID 和剩余片数并打开 fullConfirm；离线调用 startJob。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: {
                                if (jobCard.modelData.providerConnectionId.length > 0) {
                                    fullConfirm.jobId = jobCard.modelData.id
                                    fullConfirm.modelId = jobCard.modelData.modelId
                                    fullConfirm.remaining = jobCard.modelData.total - jobCard.modelData.completed
                                    fullConfirm.open()
                                } else page.jobModel.startJob(jobCard.modelData.id)
                            }
                        }
                        /* 检查点暂停入口，当前任务运行且未停止时可用。 */
                        AppButton {
                            objectName: "pauseJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: qsTr("暂停解析")
                            enabled: jobCard.active && !page.jobModel.stopping
                            /*
                             * 功能：请求当前活动任务在检查点暂停。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：非活动或已停止状态由模型处理。
                             * 副作用：调用 pauseJob，仅请求停止下一片，不能撤销在途调用。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: page.jobModel.pauseJob(jobCard.modelData.id)
                        }
                        /* 单步模型抽样入口，仅存在待处理步骤且预算尚余时准备发送确认。 */
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("模型抽样 1 步")
                            enabled: !page.jobModel.busy && !page.jobModel.running && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status === "queued"
                                && jobCard.modelData.providerConnectionId.length > 0
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            /*
                             * 功能：定位下一 ready 步骤并准备单步发送确认。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：无待处理步骤直接返回；损坏快照按 nextReadyStep 契约传播，不发送猜测范围。
                             * 副作用：复制任务、序号与全文码点范围，打开 remoteConfirm；不调用网络。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
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
                        /* 显式问题步骤重试入口，提交问题序号和尝试号，不从刷新自动触发。 */
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("重试问题片段")
                            enabled: !page.jobModel.busy && !jobCard.active && jobCard.modelData.problemOrdinal > 0
                                && jobCard.modelData.consumedRequests < jobCard.modelData.maxRequests
                            /*
                             * 功能：明确重试当前问题步骤，带上问题序号与尝试号。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：状态失效、预算不足或尝试号冲突由模型拒绝并显示。
                             * 副作用：调用 retryStep 处理步骤重试状态，不由本回调自动执行远程批次。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: page.jobModel.retryStep(jobCard.modelData.id,
                                                                jobCard.modelData.problemOrdinal,
                                                                jobCard.modelData.problemAttempt)
                        }
                        /* 取消入口，运行中即使列表忙碌仍可准备确认，不直接删除候选。 */
                        AppButton {
                            objectName: "cancelJob_" + jobCard.modelData.id
                            uiTheme: page.uiTheme
                            text: qsTr("取消任务")
                            enabled: (jobCard.active || !page.jobModel.busy) && !jobCard.modelData.cancelRequested
                                && jobCard.modelData.status !== "completed"
                                && jobCard.modelData.status !== "cancelled"
                                && jobCard.modelData.status !== "cancelling"
                            /*
                             * 功能：为所选任务准备取消确认，冻结身份及当前修订。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：快照过期在真正确认时由模型处理，本动作不取消任务。
                             * 副作用：填写 cancelConfirm.jobId/revision 并打开弹窗。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: {
                                cancelConfirm.jobId = jobCard.modelData.id
                                cancelConfirm.revision = jobCard.modelData.revision
                                cancelConfirm.open()
                            }
                        }
                        /* 任务核查入口，由后端审计持久化状态。 */
                        AppButton {
                            uiTheme: page.uiTheme
                            text: qsTr("核查")
                            enabled: !page.jobModel.busy
                            /*
                             * 功能：显式核查所选任务的持久化状态。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：核查读取或一致性错误由 jobModel 展示。
                             * 副作用：调用 auditJob，不启动解析或重试。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: page.jobModel.auditJob(jobCard.modelData.id)
                        }
                    }
                }
            }
        }
        /* 任务模型结果反馈，错误优先显示。 */
        Label { text: page.jobModel.errorText.length > 0 ? page.jobModel.errorText : page.jobModel.statusText; color: page.jobModel.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 校对中心导航入口，点击不自动接受模型候选。 */
        AppButton { uiTheme: page.uiTheme; text: qsTr("前往人工校对");
            /*
             * 功能：请求人工校对页，路由 8。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者无动作。
             * 副作用：发出 navigate，后台任务寿命仍由模型管理。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: page.navigate(8) }
    }

    /* 全书模型运行确认框，由页面拥有；Overlay 仅作为视觉父级，销毁页面不隐式停止外部任务。 */
    Dialog {
        id: fullConfirm
        objectName: "fullConfirm"
        /* 待确认批次的稳定任务身份，初始空；开始按钮写入，明确确认读取，模型重新校验任务状态。 */
        property string jobId: ""
        /* 待发送的真实模型标识，初始空；准备确认时复制，仅展示原值，不作为翻译词条或凭据。 */
        property string modelId: ""
        /* 准备确认时尚未完成片数，单位片，初始 0；用于说明，后台实际调度以最新检查点为准。 */
        property int remaining: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        /* 全书确认框主题背景。 */
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        /* 全书确认说明及动作纵向布局。 */
        contentItem: ColumnLayout {
            spacing: 16
            /* 全书模型发送确认标题。 */
            Label { text: qsTr("确认开始模型解析"); color: page.uiTheme.text; font.pointSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            /* 冻结的剩余片数与模型标识说明，提醒停止仅阻止后续调度。 */
            Label {
                text: qsTr("将按任务参数把剩余 %1 个片段依次发送给模型“%2”，不超过任务的调用上限。可能产生费用。暂停或取消会在当前请求结束后生效。")
                    .arg(fullConfirm.remaining).arg(fullConfirm.modelId)
                color: page.uiTheme.text; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            /* 全书确认与返回动作行。 */
            RowLayout {
                Layout.fillWidth: true
                /* 弹性留白，将确认动作置于弹窗右侧。 */
                Item { Layout.fillWidth: true }
                /* 返回入口，关闭全书确认而不启动发送。 */
                AppButton { objectName: "dismissFullRun"; uiTheme: page.uiTheme; text: qsTr("返回");
                    /*
                     * 功能：退出全书发送确认。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：关闭错误由 Qt 报告。
                     * 副作用：关闭 fullConfirm，不调用 startJob。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: fullConfirm.close() }
                /* 全书明确确认入口，仅有任务身份且模型不忙、不运行时可启动一次批次。 */
                AppButton {
                    objectName: "confirmFullRun"
                    uiTheme: page.uiTheme; text: qsTr("确认开始"); primary: true
                    enabled: fullConfirm.jobId.length > 0 && !page.jobModel.busy && !page.jobModel.running
                    /*
                     * 功能：在明确确认后启动冻结身份的批次。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：任务状态、预算或启动失败由模型报告；确认之后不自动重试。
                     * 副作用：先复制 jobId 并关闭弹窗，再调用 startJob；后台由模型持有，页面切换不拥有或销毁线程。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
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

    /* 单步发送确认框，持有当前待处理范围快照，不持有小说正文。 */
    Dialog {
        id: remoteConfirm
        objectName: "remoteConfirm"
        /* 单步待确认任务身份，初始空，抽样按钮复制；确认时交给模型，不拥有任务。 */
        property string jobId: ""
        /* 待发送步骤的一基序号，单位片，初始 0 表示未准备；只用于确认说明。 */
        property int ordinal: 0
        /* 全文原文起点，单位 Unicode 码点，初始 0；从待处理步骤复制，不是 UTF-16 字符索引。 */
        property int startCodepoint: 0
        /* 全文原文排他终点，单位 Unicode 码点，初始 0；确认说明读取，不用于截取小说。 */
        property int endCodepoint: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        /* 单步确认框主题背景。 */
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        /* 单步范围说明与动作纵向布局。 */
        contentItem: ColumnLayout {
            spacing: 16
            /* 单步小说发送确认标题。 */
            Label {
                text: qsTr("确认发送小说片段")
                color: page.uiTheme.text
                font.pointSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            /* 片段序号及原文码点范围说明，只读确认快照，不代表请求已发送。 */
            Label {
                text: qsTr("将把第 %1 片原文（码点 %2—%3）发送给所选模型连接，可能产生费用。每次确认只执行一步。")
                    .arg(remoteConfirm.ordinal).arg(remoteConfirm.startCodepoint).arg(remoteConfirm.endCodepoint)
                color: page.uiTheme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            /* 单步确认与返回动作行。 */
            RowLayout {
                Layout.fillWidth: true
                /* 弹性留白，将单步确认按钮靠右。 */
                Item { Layout.fillWidth: true }
                /* 返回入口，关闭单步确认，不调用抽样接口。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("返回");
                    /*
                     * 功能：退出单步发送确认。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：关闭错误由 Qt 报告。
                     * 副作用：关闭 remoteConfirm，不调用抽样。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: remoteConfirm.close() }
                /* 单步明确发送入口，确认后请求一次抽样，后台线程由模型持有。 */
                AppButton {
                    uiTheme: page.uiTheme
                    text: qsTr("确认发送 1 片")
                    primary: true
                    enabled: remoteConfirm.jobId.length > 0 && !page.jobModel.busy
                    /*
                     * 功能：在明确确认后对冻结任务身份执行一次抽样。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：状态失效、预算、连接或请求失败由模型报告，不自动追加调用。
                     * 副作用：复制 jobId、关闭弹窗，再调用 runRemoteSample，可能产生计费请求，生命周期归模型。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
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

    /* 任务取消确认框，冻结目标身份及预期修订，退出键关闭不提交取消。 */
    Dialog {
        id: cancelConfirm
        objectName: "cancelConfirm"
        /* 待取消任务稳定身份，初始空；取消按钮复制，确认读取，不随其他卡片的状态变化自动改目标。 */
        property string jobId: ""
        /* 准备取消时的预期任务修订，无单位，初始 0；确认提交后由模型判断是否过期。 */
        property int revision: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(320, Math.min(480, page.width - 40))
        padding: 20
        modal: true
        closePolicy: Popup.CloseOnEscape
        /* 取消确认框主题背景。 */
        background: Rectangle { color: page.uiTheme.surface; border.color: page.uiTheme.border; radius: 8 }
        /* 取消语义及确认动作纵向布局。 */
        contentItem: ColumnLayout {
            spacing: 16
            /* 取消任务确认标题。 */
            Label { text: qsTr("确认取消解析任务"); color: page.uiTheme.text; font.pointSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            /* 取消范围说明，已发送请求可能结算并计费，不声称立即撤销请求。 */
            Label {
                text: qsTr("未执行的片段将标记为已取消。已经发出的模型请求可能仍会完成并计费。")
                color: page.uiTheme.text
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            /* 取消确认与返回动作行。 */
            RowLayout {
                Layout.fillWidth: true
                /* 弹性留白，将取消确认放在右侧。 */
                Item { Layout.fillWidth: true }
                /* 返回入口，关闭取消框而保持任务原状态。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("返回");
                    /*
                     * 功能：退出取消确认，保持任务当前状态。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：关闭错误由 Qt 报告。
                     * 副作用：关闭 cancelConfirm，不提交取消。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: cancelConfirm.close() }
                /* 明确取消入口，读取冻结修订后请求取消，不删除已提交候选。 */
                AppButton {
                    objectName: "confirmCancelRun"
                    uiTheme: page.uiTheme
                    text: qsTr("确认取消")
                    enabled: cancelConfirm.jobId.length > 0 && (!page.jobModel.busy
                        || page.jobModel.activeJobId === cancelConfirm.jobId)
                    /*
                     * 功能：以冻结任务身份和预期修订提交明确取消。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：任务修订过期或取消失败由模型报告，不修改目标或覆盖新状态。
                     * 副作用：复制 jobId/revision、关闭弹窗并调用 cancelJob；仅阻止后续调度，在途请求可能结算，已提交候选不删除。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
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
