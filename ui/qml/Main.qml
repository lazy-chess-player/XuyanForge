import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 1380
    height: 860
    minimumWidth: 1080
    minimumHeight: 700
    visible: true
    title: "叙演工坊 · 灰港议和"
    color: "#0d131b"
    property int activePage: 0
    Component.onCompleted: if (workspaceCatalog.onboardingVisible) activePage = 6

    readonly property color ink: "#e8edf1"
    readonly property color muted: "#8e9ba7"
    readonly property color panel: "#141d27"
    readonly property color panelRaised: "#192531"
    readonly property color line: "#2a3946"
    readonly property color amber: "#dfa94f"
    readonly property color teal: "#58b7a7"
    readonly property color red: "#e0746a"

    component SectionLabel: Label {
        font.pixelSize: 11
        font.letterSpacing: 1.6
        font.weight: Font.DemiBold
        color: root.muted
    }

    component Card: Rectangle {
        radius: 10
        color: root.panelRaised
        border.color: root.line
        border.width: 1
    }

    component ActionButton: Button {
        id: control
        font.pixelSize: 13
        font.weight: Font.DemiBold
        implicitHeight: 38
        leftPadding: 14
        rightPadding: 14
        background: Rectangle {
            radius: 7
            color: control.down ? "#b98535" : control.highlighted ? root.amber : "#22313d"
            border.color: control.highlighted ? root.amber : root.line
        }
        contentItem: Text {
            text: control.text
            font: control.font
            color: control.highlighted ? "#14181c" : root.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component InputField: TextField {
        color: root.ink
        selectionColor: root.teal
        selectedTextColor: "#0d131b"
        placeholderTextColor: root.muted
        background: Rectangle { radius: 7; color: "#101922"; border.color: parent.activeFocus ? root.teal : root.line }
    }

    header: Rectangle {
        implicitHeight: 72
        color: "#101821"
        border.color: root.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 26
            anchors.rightMargin: 26
            spacing: 18

            Rectangle {
                Layout.minimumWidth: 38; Layout.preferredWidth: 38; height: 38; radius: 9
                color: root.amber
                Label {
                    anchors.centerIn: parent
                    text: "叙"
                    color: "#111820"
                    font.pixelSize: 20
                    font.bold: true
                }
            }
            ColumnLayout {
                Layout.minimumWidth: 185
                spacing: 1
                Label { text: "叙演工坊"; color: root.ink; font.pixelSize: 18; font.bold: true }
                Label { text: "XUYAN FORGE  ·  灰港议和"; color: root.muted; font.pixelSize: 9; font.letterSpacing: 1.7 }
            }
            RowLayout {
                spacing: 4
                ActionButton {
                    text: "世界资料"
                    highlighted: root.activePage === 1
                    onClicked: root.activePage = 1
                }
                ActionButton {
                    text: "来源与章节"
                    highlighted: root.activePage === 2
                    onClicked: root.activePage = 2
                }
                ActionButton {
                    text: "人物卡"
                    highlighted: root.activePage === 3
                    onClicked: root.activePage = 3
                }
                ActionButton {
                    text: "包与备份"
                    highlighted: root.activePage === 4
                    onClicked: root.activePage = 4
                }
                ActionButton {
                    text: "模型连接"
                    highlighted: root.activePage === 5
                    onClicked: root.activePage = 5
                }
                ActionButton {
                    text: "工作区"
                    highlighted: root.activePage === 6
                    onClicked: root.activePage = 6
                }
                ActionButton {
                    text: "任务中心"
                    highlighted: root.activePage === 7
                    onClicked: root.activePage = 7
                }
                ActionButton {
                    text: "校对中心"
                    highlighted: root.activePage === 8
                    onClicked: root.activePage = 8
                }
                ActionButton {
                    text: "世界视图"
                    highlighted: root.activePage === 9
                    onClicked: root.activePage = 9
                }
                ActionButton {
                    text: "推演室"
                    highlighted: root.activePage === 0
                    onClicked: root.activePage = 0
                }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 9; height: 9; radius: 5
                color: simulation.busy ? root.amber : simulation.completed ? root.teal : "#6d8292"
                visible: root.activePage === 0
            }
            Label { text: simulation.statusText; color: root.muted; font.pixelSize: 12; visible: root.activePage === 0 }
            ComboBox {
                id: branchPicker
                implicitWidth: 190
                model: simulation.branchNames
                currentIndex: simulation.activeBranchIndex
                visible: root.activePage === 0
                onActivated: simulation.selectBranch(currentIndex)
                background: Rectangle { radius: 7; color: root.panel; border.color: root.line }
                contentItem: Text {
                    leftPadding: 12
                    text: branchPicker.displayText
                    color: root.ink
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    ColumnLayout {
        id: simulationPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            radius: 9
            color: simulation.errorText.length ? "#392226" : "#121d25"
            border.color: simulation.errorText.length ? root.red : root.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                Label {
                    text: simulation.errorText.length ? "操作未提交" : "世界状态"
                    color: simulation.errorText.length ? root.red : root.teal
                    font.pixelSize: 12; font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: simulation.errorText.length ? simulation.errorText
                          : "分支固定于世界版本 v1  ·  谈判前一天傍晚  ·  议事厅外"
                    color: root.ink
                    font.pixelSize: 12
                }
                Label {
                    text: simulation.hasProductionSession
                          ? "会话 " + simulation.productionTurnCount + " / " + simulation.productionMaxTurns
                          : "演示回合 " + simulation.turn + " / 3"
                    color: root.amber; font.bold: true
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Card {
                Layout.preferredWidth: 292
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    SectionLabel { text: "人物与知识边界" }

                    Card {
                        Layout.fillWidth: true; implicitHeight: 126
                        color: "#17232d"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 5
                            RowLayout {
                                Label { text: "许澄"; color: root.ink; font.pixelSize: 16; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label { text: "城卫署"; color: root.teal; font.pixelSize: 11 }
                            }
                            Label { text: "重秩序 · 谈判代表"; color: root.muted; font.pixelSize: 12 }
                            Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                            Label { text: "已知  " + simulation.xuKnowledge; color: root.ink; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }

                    Card {
                        Layout.fillWidth: true; implicitHeight: 126
                        color: "#17232d"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14; spacing: 5
                            RowLayout {
                                Label { text: "沈棠"; color: root.ink; font.pixelSize: 16; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label { text: "盐运商会"; color: root.amber; font.pixelSize: 11 }
                            }
                            Label { text: "重信誉 · 印章持有人"; color: root.muted; font.pixelSize: 12 }
                            Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                            Label { text: "已知  " + simulation.shenKnowledge; color: root.ink; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }

                    SectionLabel { text: "场景约束" }
                    Label { text: "×  暴雨期间渡口停航"; color: root.muted; font.pixelSize: 12 }
                    Label { text: "×  世界规则禁止瞬间移动"; color: root.muted; font.pixelSize: 12 }
                    Label { text: "◇  物品转移需要持有人同意"; color: root.muted; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111a23"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Label { text: "议事厅外 · 暴雨"; color: root.ink; font.pixelSize: 21; font.bold: true }
                            Label {
                                text: simulation.hasProductionSession
                                      ? "可恢复生产会话 · 意图草稿与已提交事实分离"
                                      : "固定响应演示 · 内容在提交前仅为草稿"
                                color: root.muted; font.pixelSize: 11
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: simulation.paused ? "已暂停" : simulation.completed ? "场景完成" : "等待单步"
                            color: simulation.paused ? root.amber : simulation.completed ? root.teal : root.muted
                            font.pixelSize: 12; font.bold: true
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentHeight: storyColumn.implicitHeight
                        clip: true

                        ColumnLayout {
                            id: storyColumn
                            width: parent.width
                            spacing: 16

                            Card {
                                Layout.fillWidth: true
                                implicitHeight: Math.max(112, opening.implicitHeight + 42)
                                color: "#18232d"
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 18; spacing: 8
                                    Label { text: "场景基线"; color: root.amber; font.pixelSize: 11; font.bold: true }
                                    Label { id: opening; Layout.fillWidth: true; text: "谈判前一天傍晚，暴雨笼罩灰港。城卫署与盐运商会将在翌日午后谈判，一枚唯一的议和印章由沈棠保管。"; color: root.ink; wrapMode: Text.Wrap; lineHeight: 1.35 }
                                }
                            }

                            Repeater {
                                model: [
                                    { turn: 1, actor: "许澄", text: "暴雨封住渡口，明日谈判前先共同核对议和凭证。", note: "未泄露北门封闭计划" },
                                    { turn: 2, actor: "沈棠", text: "印泥边缘有二次压印，但我暂不公开结论。", note: "伪造迹象仅进入沈棠私密知识" },
                                    { turn: 3, actor: "沈棠", text: "许代表，印章可能被人动过。此事先只在你我之间核验。", note: "合法沟通后许澄获得该知识" }
                                ]
                                delegate: Card {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    visible: simulation.turn >= modelData.turn
                                    implicitHeight: visible ? content.implicitHeight + 36 : 0
                                    color: "#17232d"
                                    ColumnLayout {
                                        id: content
                                        anchors.fill: parent; anchors.margins: 16; spacing: 7
                                        RowLayout {
                                            Label { text: "0" + modelData.turn; color: root.muted; font.pixelSize: 11; font.bold: true }
                                            Label { text: modelData.actor; color: root.teal; font.pixelSize: 13; font.bold: true }
                                            Item { Layout.fillWidth: true }
                                            Label { text: "已提交"; color: root.teal; font.pixelSize: 10 }
                                        }
                                        Label { Layout.fillWidth: true; text: "“" + modelData.text + "”"; color: root.ink; font.pixelSize: 14; wrapMode: Text.Wrap; lineHeight: 1.3 }
                                        Label { Layout.fillWidth: true; text: "裁定 · " + modelData.note; color: root.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
                                    }
                                }
                            }

                            Card {
                                Layout.fillWidth: true
                                implicitHeight: 82
                                visible: simulation.turn === 0
                                color: "#141e27"
                                Label { anchors.centerIn: parent; text: "运行下一回合，查看角色在知识边界内行动"; color: root.muted }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.preferredWidth: 306
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14
                    SectionLabel { text: "已提交状态" }
                    Label { text: simulation.narration; Layout.fillWidth: true; color: root.ink; font.pixelSize: 13; wrapMode: Text.Wrap; lineHeight: 1.35 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    SectionLabel { text: "状态差异" }
                    Card {
                        Layout.fillWidth: true; implicitHeight: 92
                        color: "#17232d"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 13; spacing: 6
                            Label { text: "议和印章"; color: root.ink; font.bold: true }
                            Label { text: simulation.sealStatus; color: root.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                    Card {
                        Layout.fillWidth: true; implicitHeight: 74
                        color: "#17232d"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 13
                            ColumnLayout {
                                Label { text: "状态哈希"; color: root.ink; font.bold: true }
                                Label { text: simulation.stateHash; color: root.muted; font.pixelSize: 9 }
                            }
                        }
                    }
                    SectionLabel { text: "恢复保证" }
                    Label { text: "每回合经规则校验后，以短事务写入提交链。关闭应用后会从当前分支头恢复。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.3; font.pixelSize: 11 }
                    SectionLabel { text: "生产会话"; visible: simulation.hasProductionSession }
                    Card {
                        Layout.fillWidth: true
                        implicitHeight: sessionDetails.implicitHeight + 24
                        visible: simulation.hasProductionSession
                        color: "#17232d"
                        ColumnLayout {
                            id: sessionDetails
                            anchors.fill: parent; anchors.margins: 12; spacing: 5
                            Label {
                                text: simulation.productionStatus + (simulation.productionContinuous ? " · 连续" : " · 单步")
                                color: simulation.productionStatus === "completed" ? root.teal : root.amber
                                font.bold: true
                            }
                            Label { text: "调用 " + simulation.productionUsedCalls + " / " + simulation.productionMaxCalls + " · 未知 " + simulation.productionUnknownCalls; color: root.muted; font.pixelSize: 11 }
                            Label { text: simulation.productionSessionId; color: root.muted; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        }
                    }
                    Repeater {
                        model: simulation.productionLog
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            text: modelData
                            color: root.ink
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Label { text: "Mock 已验证 · 真实厂商待凭据验收"; color: root.teal; font.pixelSize: 10 }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 68
            radius: 10
            color: root.panel
            border.color: root.line
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Label {
                    text: simulation.hasProductionSession
                          ? "生产会话：事实提交先于叙事，导演介入不消耗模型调用。"
                          : "先创建单步或连续生产会话；固定演示仍可通过测试运行。"
                    color: root.muted; font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "新建单步"; visible: !simulation.hasProductionSession || simulation.productionStatus === "completed" || simulation.productionStatus === "cancelled"; enabled: !simulation.busy; onClicked: simulation.createProductionSession(false) }
                ActionButton { text: "新建连续"; visible: !simulation.hasProductionSession || simulation.productionStatus === "completed" || simulation.productionStatus === "cancelled"; enabled: !simulation.busy; onClicked: simulation.createProductionSession(true) }
                ActionButton {
                    text: "会话单步"
                    highlighted: true
                    visible: simulation.hasProductionSession && simulation.productionStatus !== "completed" && simulation.productionStatus !== "cancelled"
                    enabled: !simulation.busy && (simulation.productionStatus === "ready" || simulation.productionStatus === "running")
                    onClicked: simulation.stepProductionSession()
                }
                ActionButton { text: "连续运行"; visible: simulation.hasProductionSession && simulation.productionContinuous; enabled: !simulation.busy && (simulation.productionStatus === "ready" || simulation.productionStatus === "running"); onClicked: simulation.runProductionSession() }
                ActionButton { text: simulation.productionStatus === "paused" ? "恢复" : "暂停"; visible: simulation.hasProductionSession; enabled: !simulation.busy && simulation.productionStatus !== "completed" && simulation.productionStatus !== "cancelled"; onClicked: simulation.toggleProductionPause() }
                ActionButton { text: "导演复核"; visible: simulation.hasProductionSession; enabled: !simulation.busy && simulation.productionStatus !== "completed" && simulation.productionStatus !== "cancelled"; onClicked: simulation.directorInspectSeal() }
                ActionButton { text: "停止"; visible: simulation.hasProductionSession; enabled: !simulation.busy && simulation.productionStatus !== "completed" && simulation.productionStatus !== "cancelled"; onClicked: simulation.cancelProductionSession() }
                ActionButton { text: "创建分支"; enabled: !simulation.busy; onClicked: simulation.createBranch() }
            }
        }
    }

    ColumnLayout {
        id: workspacePage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 1

        Connections {
            target: workspace
            function onChanged() {
                if (workspace.selectedIndex >= 0 || workspace.draftAvailable) {
                    entityName.text = workspace.selectedName
                    entityDescription.text = workspace.selectedDescription
                    entityAliases.text = workspace.selectedAliases
                    entityTags.text = workspace.selectedTags
                    entityAttributes.text = workspace.selectedAttributes
                    kindPicker.currentIndex = Math.max(0, kindPicker.model.indexOf(workspace.selectedKind))
                }
            }
        }
        Timer {
            id: workspaceDraftTimer
            interval: 650; repeat: false
            onTriggered: workspace.saveDraft(entityName.text, kindPicker.currentText, entityDescription.text,
                                               entityAliases.text, entityTags.text, entityAttributes.text)
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 58
            radius: 10
            color: root.panel
            border.color: root.line
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                InputField {
                    id: searchField
                    Layout.preferredWidth: 310
                    placeholderText: "搜索名称、别名、标签或说明"
                    onAccepted: workspace.refresh(text, filterKind.currentValue)
                }
                ComboBox {
                    id: filterKind
                    implicitWidth: 150
                    textRole: "text"; valueRole: "value"
                    model: [
                        { text: "全部类型", value: "" }, { text: "人物", value: "character" },
                        { text: "势力", value: "faction" }, { text: "地点", value: "location" },
                        { text: "物品", value: "item" }, { text: "规则", value: "rule" },
                        { text: "事件", value: "event" }, { text: "其他", value: "other" }
                    ]
                    background: Rectangle { radius: 7; color: "#101922"; border.color: root.line }
                    contentItem: Text { leftPadding: 12; text: filterKind.displayText; color: root.ink; verticalAlignment: Text.AlignVCenter }
                }
                ActionButton { text: "筛选"; onClicked: workspace.refresh(searchField.text, filterKind.currentValue); enabled: !workspace.busy }
                Label { text: workspace.total + " 个条目"; color: root.muted; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "新建条目"
                    highlighted: true
                    onClicked: {
                        workspace.clearSelection()
                        entityName.text = ""
                        entityDescription.text = ""
                        entityAliases.text = ""
                        entityTags.text = ""
                        entityAttributes.text = "{}"
                        kindPicker.currentIndex = 0
                        entityName.forceActiveFocus()
                        workspaceDraftTimer.restart()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Card {
                Layout.preferredWidth: 390
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10
                    SectionLabel { text: "世界条目 · 灰港议和" }
                    ListView {
                        id: entityList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 7
                        clip: true
                        model: workspace.entityItems
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: entityList.width
                            height: 78
                            radius: 8
                            color: workspace.selectedIndex === index ? "#213442" : "#17232d"
                            border.color: workspace.selectedIndex === index ? root.teal : root.line
                            MouseArea { anchors.fill: parent; onClicked: workspace.selectEntity(index) }
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 11; spacing: 4
                                RowLayout {
                                    Label { text: modelData.name; color: root.ink; font.bold: true; font.pixelSize: 14 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: modelData.kind + "  r" + modelData.revision; color: root.muted; font.pixelSize: 10 }
                                }
                                Label { Layout.fillWidth: true; text: modelData.description; color: root.muted; font.pixelSize: 11; elide: Text.ElideRight }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                    Label {
                        visible: workspace.entityItems.length === 0 && !workspace.busy
                        text: "没有匹配的条目"
                        color: root.muted
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton { text: "上一页"; enabled: workspace.hasPreviousPage && !workspace.busy; onClicked: workspace.previousPage() }
                        Label { text: workspace.pageText; color: root.muted; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                        ActionButton { text: "下一页"; enabled: workspace.hasNextPage && !workspace.busy; onClicked: workspace.nextPage() }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111a23"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 11

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Label { text: workspace.selectedIndex >= 0 ? "编辑世界条目" : "创建世界条目"; color: root.ink; font.pixelSize: 21; font.bold: true }
                            Label {
                                text: workspace.selectedIndex >= 0 ? "稳定 ID  ·  修订 " + workspace.selectedRevision : "保存后生成稳定 ID 与首个修订"
                                color: root.muted; font.pixelSize: 11
                            }
                        }
                        Item { Layout.fillWidth: true }
                        Label { visible: workspace.draftAvailable; text: "草稿已自动保存"; color: root.amber; font.pixelSize: 10 }
                        ActionButton { visible: workspace.draftAvailable; text: "丢弃草稿"; onClicked: workspace.discardDraft() }
                        Label { text: workspace.busy ? "正在保存…" : "本地资料"; color: workspace.busy ? root.amber : root.teal }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    Label { visible: workspace.errorText.length > 0; text: workspace.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 7
                        Label { text: "名称"; color: root.muted }
                        InputField { id: entityName; Layout.fillWidth: true; placeholderText: "条目名称"; onTextChanged: workspaceDraftTimer.restart() }
                        Label { text: "类型"; color: root.muted }
                        ComboBox {
                            id: kindPicker
                            Layout.fillWidth: true
                            model: ["character", "faction", "location", "item", "rule", "event", "culture", "technology", "other"]
                            background: Rectangle { radius: 7; color: "#101922"; border.color: root.line }
                            contentItem: Text { leftPadding: 12; text: kindPicker.displayText; color: root.ink; verticalAlignment: Text.AlignVCenter }
                            onCurrentTextChanged: workspaceDraftTimer.restart()
                        }
                        Label { text: "别名"; color: root.muted }
                        InputField { id: entityAliases; Layout.fillWidth: true; placeholderText: "用逗号分隔"; onTextChanged: workspaceDraftTimer.restart() }
                        Label { text: "标签"; color: root.muted }
                        InputField { id: entityTags; Layout.fillWidth: true; placeholderText: "用逗号分隔"; onTextChanged: workspaceDraftTimer.restart() }
                    }

                    Label { text: "说明"; color: root.muted }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 150
                        radius: 7; color: "#101922"; border.color: entityDescription.activeFocus ? root.teal : root.line
                        TextArea {
                            id: entityDescription
                            anchors.fill: parent; anchors.margins: 7
                            color: root.ink; placeholderText: "设定说明、来源摘要与使用约束"
                            placeholderTextColor: root.muted; wrapMode: TextEdit.Wrap
                            background: null
                            onTextChanged: workspaceDraftTimer.restart()
                        }
                    }
                    Label { text: "扩展属性 JSON"; color: root.muted }
                    InputField { id: entityAttributes; Layout.fillWidth: true; text: "{}"; font.family: "Consolas"; onTextChanged: workspaceDraftTimer.restart() }

                    RowLayout {
                        Layout.fillWidth: true
                        InputField { id: mergeTargetId; Layout.fillWidth: true; placeholderText: "规范目标条目 ID" }
                        ActionButton { text: "合并当前条目"; enabled: !workspace.busy && workspace.selectedIndex >= 0; onClicked: workspace.mergeSelectedInto(mergeTargetId.text) }
                        InputField { id: splitMergeId; Layout.fillWidth: true; placeholderText: "merge-… 合并记录" }
                        ActionButton { text: "拆分恢复"; enabled: !workspace.busy; onClicked: workspace.splitMerge(splitMergeId.text) }
                    }
                    Label { visible: workspace.statusText.length > 0; text: workspace.statusText; color: root.teal; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10 }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: workspace.selectedId; color: root.muted; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.maximumWidth: 320 }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: "删除"; visible: workspace.selectedIndex >= 0; enabled: !workspace.busy; onClicked: workspace.deleteSelected() }
                        ActionButton {
                            text: workspace.selectedIndex >= 0 ? "保存新修订" : "创建条目"
                            highlighted: true
                            enabled: !workspace.busy
                            onClicked: {
                                if (workspace.selectedIndex >= 0)
                                    workspace.saveSelected(entityName.text, kindPicker.currentText, entityDescription.text, entityAliases.text, entityTags.text, entityAttributes.text)
                                else
                                    workspace.createEntity(entityName.text, kindPicker.currentText, entityDescription.text, entityAliases.text, entityTags.text, entityAttributes.text)
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: sourcesPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 2

        FileDialog {
            id: sourceDialog
            title: "选择 TXT 或 Markdown 来源"
            nameFilters: ["文本来源 (*.txt *.md *.markdown)"]
            fileMode: FileDialog.OpenFile
            onAccepted: sources.importFile(selectedFile)
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 58
            radius: 10
            color: root.panel
            border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 12
                ColumnLayout {
                    Label { text: "来源与章节"; color: root.ink; font.pixelSize: 16; font.bold: true }
                    Label { text: "保留原始文件，以标准化 UTF-8 码点区间定位证据"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                Label { text: sources.busy ? "正在处理…" : sources.sourceItems.length + " 个来源"; color: sources.busy ? root.amber : root.muted }
                ActionButton { text: "导入文本"; highlighted: true; enabled: !sources.busy; onClicked: sourceDialog.open() }
            }
        }

        Label { visible: sources.errorText.length > 0; text: sources.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Card {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    SectionLabel { text: "来源文件" }
                    ListView {
                        id: sourceList
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 7; clip: true
                        model: sources.sourceItems
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: sourceList.width; height: 72; radius: 8
                            color: sources.selectedIndex === index ? "#213442" : "#17232d"
                            border.color: sources.selectedIndex === index ? root.teal : root.line
                            MouseArea { anchors.fill: parent; onClicked: sources.selectSource(index) }
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 4
                                Label { text: modelData.name; color: root.ink; font.bold: true; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: modelData.chapters + " 章 · " + modelData.encoding + " · " + modelData.id; color: root.muted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                        }
                    }
                    Label { visible: sources.sourceItems.length === 0; text: "尚未导入来源文件"; color: root.muted }
                }
            }

            Card {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    SectionLabel { text: "章节索引" }
                    InputField { id: chapterTitle; Layout.fillWidth: true; placeholderText: "选择章节后校正标题" }
                    RowLayout {
                        Layout.fillWidth: true
                        InputField { id: chapterStart; Layout.fillWidth: true; placeholderText: "起始码点"; validator: IntValidator { bottom: 0 } }
                        InputField { id: chapterEnd; Layout.fillWidth: true; placeholderText: "结束码点"; validator: IntValidator { bottom: 1 } }
                        ActionButton {
                            text: "保存校正"; enabled: sources.selectedChapterIndex >= 0 && !sources.busy
                            onClicked: sources.saveChapter(sources.selectedChapterIndex, chapterTitle.text,
                                                           Number(chapterStart.text), Number(chapterEnd.text))
                        }
                    }
                    ListView {
                        id: chapterList
                        Layout.fillWidth: true; Layout.preferredHeight: 220; spacing: 6; clip: true
                        model: sources.chapterItems
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: chapterList.width; height: 64; radius: 7
                            color: sources.selectedChapterIndex === index ? "#213442" : "#17232d"
                            border.color: sources.selectedChapterIndex === index ? root.teal : root.line
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 9; spacing: 3
                                Label { text: modelData.ordinal + "  " + modelData.title; color: root.ink; font.bold: true }
                                Label { text: "码点 [" + modelData.start + ", " + modelData.end + ")"; color: root.muted; font.pixelSize: 10 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    sources.selectChapter(index)
                                    chapterTitle.text = modelData.title
                                    chapterStart.text = String(modelData.start)
                                    chapterEnd.text = String(modelData.end)
                                    sourcePreview.select(sources.highlightStart, sources.highlightEnd)
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    SectionLabel { text: "已关联证据" }
                    ListView {
                        id: evidenceList
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6; clip: true
                        model: sources.evidenceItems
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: evidenceList.width; height: 76; radius: 7; color: "#17232d"; border.color: root.line
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 2
                                Label { text: modelData.entityId + "." + modelData.field; color: root.teal; font.bold: true; font.pixelSize: 11 }
                                Label { text: modelData.quote; color: root.ink; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10 }
                                Label { text: modelData.provenance + " · [" + modelData.start + ", " + modelData.end + ")"; color: root.muted; font.pixelSize: 9 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    sources.selectEvidence(index)
                                    sourcePreview.select(sources.highlightStart, sources.highlightEnd)
                                    sourcePreview.forceActiveFocus()
                                }
                            }
                        }
                    }
                    Label { visible: sources.evidenceItems.length === 0; text: "选中原文后可创建字段级证据"; color: root.muted; font.pixelSize: 10 }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#111a23"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 9
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel { text: "标准化原文预览" }
                        Item { Layout.fillWidth: true }
                        Label { text: sources.selectedHash.length ? "SHA-256  " + sources.selectedHash.substring(0, 16) + "…" : ""; color: root.muted; font.pixelSize: 9 }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                    RowLayout {
                        Layout.fillWidth: true
                        InputField { id: evidenceEntity; Layout.fillWidth: true; placeholderText: "稳定条目 ID，如 entity-seal" }
                        InputField { id: evidenceField; Layout.preferredWidth: 150; placeholderText: "字段，如 description" }
                        ComboBox { id: evidenceType; model: ["original_fact", "in_text_claim", "author_setting"] }
                        ActionButton {
                            text: "关联所选原文"; enabled: !sources.busy
                            onClicked: sources.createEvidence(evidenceEntity.text, evidenceField.text,
                                                              sourcePreview.selectionStart, sourcePreview.selectionEnd,
                                                              evidenceType.currentText)
                        }
                    }
                    Label { visible: sources.statusText.length > 0; text: sources.statusText; color: root.teal; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10 }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        TextArea {
                            id: sourcePreview
                            text: sources.previewText
                            readOnly: true
                            wrapMode: TextEdit.Wrap
                            color: root.ink
                            selectionColor: root.teal
                            selectedTextColor: "#0d131b"
                            font.pixelSize: 13
                            background: null
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: charactersPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 3

        Connections {
            target: characters
            function onChanged() {
                if (characters.selectedIndex >= 0) {
                    cardName.text = characters.name; cardSummary.text = characters.summary
                    cardValues.text = characters.values; cardTraits.text = characters.traits
                    cardLongGoal.text = characters.longGoal; cardShortGoal.text = characters.shortGoal
                    cardSpeech.text = characters.speechStyle; cardAbilities.text = characters.abilities
                    cardEquipment.text = characters.equipment; cardBackground.text = characters.background
                    cardPrivate.text = characters.privateNotes
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 58; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 10
                ColumnLayout {
                    Label { text: "可移植人物卡"; color: root.ink; font.pixelSize: 16; font.bold: true }
                    Label { text: "核心身份、可适配内容与作者私密说明分开版本化"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: "新建人物卡"; highlighted: true
                    onClicked: {
                        characters.clearSelection(); cardName.text = ""; cardSummary.text = ""; cardValues.text = ""
                        cardTraits.text = ""; cardLongGoal.text = ""; cardShortGoal.text = ""; cardSpeech.text = ""
                        cardAbilities.text = "[]"; cardEquipment.text = ""; cardBackground.text = ""; cardPrivate.text = ""
                        cardName.forceActiveFocus()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            Card {
                Layout.preferredWidth: 310; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 10
                    SectionLabel { text: "人物卡版本头" }
                    ListView {
                        id: cardList
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 7; clip: true
                        model: characters.cardItems
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: cardList.width; height: 82; radius: 8
                            color: characters.selectedIndex === index ? "#213442" : "#17232d"
                            border.color: characters.selectedIndex === index ? root.teal : root.line
                            MouseArea { anchors.fill: parent; onClicked: characters.selectCard(index) }
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 10; spacing: 4
                                RowLayout {
                                    Label { text: modelData.name; color: root.ink; font.bold: true; font.pixelSize: 14 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: "v" + modelData.version; color: root.amber; font.bold: true }
                                }
                                Label { text: modelData.summary; color: root.muted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true; Layout.fillHeight: true; color: "#111a23"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Label { text: characters.selectedIndex >= 0 ? "编辑人物卡" : "创建人物卡"; color: root.ink; font.pixelSize: 21; font.bold: true }
                            Label { text: characters.selectedIndex >= 0 ? characters.selectedId + "  ·  固定版本 " + characters.selectedVersion : "已有世界实例不会随卡片更新而改变"; color: root.muted; font.pixelSize: 10 }
                        }
                        Item { Layout.fillWidth: true }
                        Label { text: characters.busy ? "正在保存…" : "本地卡片库"; color: characters.busy ? root.amber : root.teal }
                    }
                    Label { visible: characters.errorText.length > 0; text: characters.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        ColumnLayout {
                            width: parent.width; spacing: 9
                            GridLayout {
                                Layout.fillWidth: true; columns: 2; columnSpacing: 12; rowSpacing: 7
                                Label { text: "姓名"; color: root.muted }
                                InputField { id: cardName; Layout.fillWidth: true }
                                Label { text: "身份概述"; color: root.muted }
                                InputField { id: cardSummary; Layout.fillWidth: true }
                                Label { text: "核心价值"; color: root.muted }
                                InputField { id: cardValues; Layout.fillWidth: true; placeholderText: "逗号分隔" }
                                Label { text: "性格倾向"; color: root.muted }
                                InputField { id: cardTraits; Layout.fillWidth: true; placeholderText: "逗号分隔" }
                                Label { text: "长期目标"; color: root.muted }
                                InputField { id: cardLongGoal; Layout.fillWidth: true }
                                Label { text: "当前目标"; color: root.muted }
                                InputField { id: cardShortGoal; Layout.fillWidth: true }
                                Label { text: "说话方式"; color: root.muted }
                                InputField { id: cardSpeech; Layout.fillWidth: true }
                                Label { text: "初始装备"; color: root.muted }
                                InputField { id: cardEquipment; Layout.fillWidth: true; placeholderText: "逗号分隔" }
                            }
                            Label { text: "可适配能力 JSON"; color: root.muted }
                            InputField { id: cardAbilities; Layout.fillWidth: true; text: "[]"; font.family: "Consolas" }
                            Label { text: "关键经历"; color: root.muted }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: 90; radius: 7; color: "#101922"; border.color: root.line
                                TextArea { id: cardBackground; anchors.fill: parent; anchors.margins: 6; color: root.ink; wrapMode: TextEdit.Wrap; background: null }
                            }
                            Label { text: "作者私密说明"; color: root.amber }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: 90; radius: 7; color: "#18191f"; border.color: "#55472e"
                                TextArea { id: cardPrivate; anchors.fill: parent; anchors.margins: 6; color: root.ink; wrapMode: TextEdit.Wrap; background: null }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "保存会创建不可变的新版本"; color: root.muted; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: characters.selectedIndex >= 0 ? "保存为新版本" : "创建人物卡"
                            highlighted: true; enabled: !characters.busy
                            onClicked: characters.saveCard(cardName.text, cardSummary.text, cardValues.text, cardTraits.text,
                                                           cardLongGoal.text, cardShortGoal.text, cardSpeech.text,
                                                           cardAbilities.text, cardEquipment.text, cardBackground.text,
                                                           cardPrivate.text)
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: packagesPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 4
        onVisibleChanged: if (visible) packages.reloadBranches()

        FileDialog { id: worldExportDialog; title: "导出世界包"; fileMode: FileDialog.SaveFile; nameFilters: ["叙演世界包 (*.xuyan-world.zip)"]; onAccepted: packages.exportWorld(selectedFile) }
        FileDialog { id: worldImportDialog; title: "导入世界包"; fileMode: FileDialog.OpenFile; nameFilters: ["叙演世界包 (*.xuyan-world.zip *.zip)"]; onAccepted: packages.importWorld(selectedFile) }
        FileDialog { id: characterExportDialog; title: "导出人物包"; fileMode: FileDialog.SaveFile; nameFilters: ["叙演人物包 (*.xuyan-character.zip)"]; onAccepted: packages.exportCharacter(characters.selectedId, selectedFile, includePrivate.checked) }
        FileDialog { id: characterImportDialog; title: "导入人物包"; fileMode: FileDialog.OpenFile; nameFilters: ["叙演人物包 (*.xuyan-character.zip *.zip)"]; onAccepted: packages.importCharacter(selectedFile) }
        FolderDialog { id: backupParentDialog; title: "选择完整备份的父目录"; onAccepted: packages.createBackup(selectedFolder) }
        FolderDialog { id: backupRestoreDialog; title: "选择 XuyanForge 完整备份目录"; onAccepted: packages.restoreBackup(selectedFolder) }
        FileDialog { id: branchMarkdownDialog; title: "导出分支 Markdown"; fileMode: FileDialog.SaveFile; nameFilters: ["Markdown (*.md)"]; onAccepted: packages.exportBranch(outcomeBranch.currentIndex, selectedFile, "markdown", includeTechnical.checked) }
        FileDialog { id: branchJsonDialog; title: "导出分支 JSON"; fileMode: FileDialog.SaveFile; nameFilters: ["JSON (*.json)"]; onAccepted: packages.exportBranch(outcomeBranch.currentIndex, selectedFile, "json", includeTechnical.checked) }
        FileDialog { id: diagnosticsDialog; title: "导出脱敏诊断摘要"; fileMode: FileDialog.SaveFile; nameFilters: ["JSON (*.json)"]; onAccepted: packages.exportDiagnostics(selectedFile) }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 64; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    Label { text: "世界包、人物包与恢复"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "开放 JSON/ZIP 格式 · 导入前校验路径、大小、CRC、SHA-256 与引用"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "创建完整备份"; enabled: !packages.busy; onClicked: backupParentDialog.open() }
                ActionButton { text: "恢复到新工作区"; enabled: !packages.busy; onClicked: backupRestoreDialog.open() }
                Label { text: packages.busy ? "正在处理…" : packages.statusText; color: packages.busy ? root.amber : root.teal; Layout.maximumWidth: 500; elide: Text.ElideMiddle }
            }
        }
        Label { visible: packages.errorText.length > 0; text: packages.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }

        GridLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            columns: 3; columnSpacing: 14; rowSpacing: 14
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 13
                    Label { text: "导出世界包"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: "导出当前世界的可见条目、修订号和扩展字段。默认不包含原始小说、人物卡私密说明或任何凭据。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    Label { text: "包含  manifest.json · world.json · entities.jsonl"; color: root.teal; font.pixelSize: 11 }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "选择导出位置"; highlighted: true; enabled: !packages.busy; onClicked: worldExportDialog.open() }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 13
                    Label { text: "导入世界包"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: "先在内存中校验整个包，再用单个 SQLite 事务导入。任何条目冲突或摘要异常都会整包回滚。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    Label { text: "当前基础导入要求目标工作区不存在同 ID 条目。"; color: root.amber; font.pixelSize: 11 }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "选择世界包"; enabled: !packages.busy; onClicked: worldImportDialog.open() }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 12
                    Label { text: "导出人物包"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: characters.selectedId.length ? "当前人物卡：" + characters.name + " · v" + characters.selectedVersion : "先在人物卡页选择一张卡片"; color: root.muted }
                    CheckBox {
                        id: includePrivate
                        text: "包含作者私密说明"
                        checked: true
                        indicator: Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: includePrivate.checked ? root.amber : "#101922"
                            border.color: includePrivate.checked ? root.amber : root.line
                            Text { anchors.centerIn: parent; text: includePrivate.checked ? "✓" : ""; color: "#111820"; font.bold: true }
                        }
                        contentItem: Text {
                            leftPadding: includePrivate.indicator.width + 9
                            text: includePrivate.text; color: root.ink; verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Label { text: "人物包会保存从 v1 到当前版本的完整不可变版本链。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "选择导出位置"; highlighted: true; enabled: !packages.busy && characters.selectedId.length > 0; onClicked: characterExportDialog.open() }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 13
                    Label { text: "导入人物包"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: "验证 manifest、版本链、JSON 字段和文件摘要后一次性写入。本地模型绑定与 API Key 不属于人物包。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "选择人物包"; enabled: !packages.busy; onClicked: characterImportDialog.open() }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 9
                    Label { text: "分支比较"; color: root.ink; font.pixelSize: 18; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: compareLeft; Layout.fillWidth: true; model: packages.branchNames }
                        Label { text: "↔"; color: root.amber }
                        ComboBox { id: compareRight; Layout.fillWidth: true; model: packages.branchNames; currentIndex: count > 1 ? 1 : 0 }
                    }
                    Label { text: packages.comparisonText; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10; maximumLineCount: 5; elide: Text.ElideRight }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "比较共同起点与成本"; highlighted: true; enabled: !packages.busy && compareLeft.currentIndex !== compareRight.currentIndex; onClicked: packages.compareBranches(compareLeft.currentIndex, compareRight.currentIndex) }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 8
                    Label { text: "分支成果与诊断"; color: root.ink; font.pixelSize: 18; font.bold: true }
                    ComboBox { id: outcomeBranch; Layout.fillWidth: true; model: packages.branchNames }
                    RowLayout {
                        Layout.fillWidth: true
                        InputField { id: adoptionWorld; Layout.fillWidth: true; text: "world-grey-harbor"; placeholderText: "目标世界 ID" }
                        InputField { id: adoptionTitle; Layout.fillWidth: true; text: "推演候选结果"; placeholderText: "素材标题" }
                    }
                    CheckBox {
                        id: includeTechnical; text: "导出包含技术提交标识"; checked: false
                        indicator: Rectangle {
                            implicitWidth: 22; implicitHeight: 22; radius: 4
                            color: includeTechnical.checked ? root.amber : "#101922"
                            border.color: includeTechnical.checked ? root.amber : root.line
                            Text { anchors.centerIn: parent; text: includeTechnical.checked ? "✓" : ""; color: "#111820"; font.bold: true }
                        }
                        contentItem: Text {
                            leftPadding: includeTechnical.indicator.width + 9
                            text: includeTechnical.text; color: root.ink; verticalAlignment: Text.AlignVCenter
                        }
                    }
                    RowLayout {
                        ActionButton { text: "Markdown"; enabled: !packages.busy && outcomeBranch.currentIndex >= 0; onClicked: branchMarkdownDialog.open() }
                        ActionButton { text: "JSON"; enabled: !packages.busy && outcomeBranch.currentIndex >= 0; onClicked: branchJsonDialog.open() }
                        ActionButton { text: "采纳为新版本"; enabled: !packages.busy && outcomeBranch.currentIndex >= 0; onClicked: packages.adoptBranch(outcomeBranch.currentIndex, adoptionWorld.text, adoptionTitle.text) }
                    }
                    Item { Layout.fillHeight: true }
                    ActionButton { text: "导出脱敏诊断"; enabled: !packages.busy; onClicked: diagnosticsDialog.open() }
                }
            }
        }
    }

    ColumnLayout {
        id: providersPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 5
        property string editId: ""
        property int editRevision: 0

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 64; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    Label { text: "模型提供商连接"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "SQLite 仅保存 credential_ref；API Key 由当前 Windows 用户的凭据管理器保护"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                Label { text: providers.busy ? "正在处理…" : providers.statusText; color: providers.busy ? root.amber : root.teal }
            }
        }
        Label { visible: providers.errorText.length > 0; text: providers.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            Card {
                Layout.preferredWidth: 430; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    RowLayout {
                        Label { text: "已保存连接"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: "新建"
                            onClicked: {
                                providersPage.editId = ""; providersPage.editRevision = 0
                                providerName.text = ""; providerKind.currentIndex = 0
                                providerEndpoint.text = "https://api.openai.com/v1"; providerModel.text = ""
                                providerPolicy.currentIndex = 0; providerEnabled.checked = true; providerKey.text = ""
                            }
                        }
                        ActionButton {
                            text: "DeepSeek 预设"
                            onClicked: {
                                providersPage.editId = ""; providersPage.editRevision = 0
                                providerName.text = "DeepSeek"; providerKind.currentIndex = providerKind.find("deepseek")
                                providerEndpoint.text = "https://api.deepseek.com"; providerModel.text = "deepseek-flash"
                                providerPolicy.currentIndex = providerPolicy.find("remote_allowed")
                                providerEnabled.checked = true; providerKey.text = ""
                            }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                        model: providers.connections
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width; height: 92; radius: 8
                            color: providersPage.editId === modelData.id ? "#253744" : "#101922"
                            border.color: providersPage.editId === modelData.id ? root.teal : root.line
                            Column {
                                anchors.fill: parent; anchors.margins: 12; spacing: 5
                                Row {
                                    spacing: 8
                                    Label { text: modelData.name; color: root.ink; font.bold: true }
                                    Label { text: modelData.enabled ? "启用" : "停用"; color: modelData.enabled ? root.teal : root.muted; font.pixelSize: 10 }
                                    Label { text: modelData.credentialConfigured ? "凭据已配置" : "未配置凭据"; color: modelData.credentialConfigured ? root.teal : root.amber; font.pixelSize: 10 }
                                }
                                Label { text: modelData.kind + " · " + (modelData.model || "未指定模型"); color: root.muted; font.pixelSize: 11 }
                                Label { text: modelData.endpoint; color: root.muted; width: parent.width; elide: Text.ElideMiddle; font.pixelSize: 10 }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    providersPage.editId = modelData.id; providersPage.editRevision = modelData.revision
                                    providerName.text = modelData.name; providerKind.currentIndex = providerKind.find(modelData.kind)
                                    providerEndpoint.text = modelData.endpoint; providerModel.text = modelData.model
                                    providerPolicy.currentIndex = providerPolicy.find(modelData.dataPolicy)
                                    providerEnabled.checked = modelData.enabled; providerKey.text = ""
                                }
                            }
                        }
                    }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 22; spacing: 11
                    Label { text: providersPage.editId.length ? "编辑连接" : "新建连接"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: "连接名称"; color: root.muted }
                    InputField { id: providerName; Layout.fillWidth: true; placeholderText: "例如：创作模型" }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "提供商协议"; color: root.muted }
                            ComboBox { id: providerKind; Layout.fillWidth: true; model: ["openai", "openai-compatible", "deepseek", "anthropic", "gemini", "local"] }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "数据策略"; color: root.muted }
                            ComboBox { id: providerPolicy; Layout.fillWidth: true; model: ["remote_allowed", "local_only"] }
                        }
                    }
                    Label { text: "端点 URL"; color: root.muted }
                    InputField { id: providerEndpoint; Layout.fillWidth: true; text: "https://api.openai.com/v1" }
                    Label { text: "默认模型（可留空）"; color: root.muted }
                    InputField { id: providerModel; Layout.fillWidth: true; placeholderText: "运行时实际模型标识" }
                    Label { text: providersPage.editId.length ? "API Key（留空则保留现有凭据）" : "API Key（可稍后配置）"; color: root.muted }
                    InputField { id: providerKey; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: "不会写入 SQLite、日志或包" }
                    CheckBox {
                        id: providerEnabled; text: "启用此连接"; checked: true
                        contentItem: Text {
                            leftPadding: providerEnabled.indicator.width + providerEnabled.spacing
                            text: providerEnabled.text; color: root.ink; verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Label { text: "远程连接强制 HTTPS；local_only 只能用于 local 类型。连接尚未探测时不会宣称能力可用。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton {
                            text: "测试连接"; visible: providersPage.editId.length > 0; enabled: !providers.busy
                            onClicked: providers.probeConnection(providersPage.editId)
                        }
                        ActionButton {
                            text: "测试结构化生成"; visible: providersPage.editId.length > 0; enabled: !providers.busy
                            onClicked: providers.testStructuredGeneration(providersPage.editId)
                        }
                        ActionButton {
                            text: "删除"; visible: providersPage.editId.length > 0; enabled: !providers.busy
                            onClicked: {
                                providers.removeConnection(providersPage.editId, providersPage.editRevision)
                                providersPage.editId = ""; providersPage.editRevision = 0; providerKey.text = ""
                            }
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: "安全保存"; highlighted: true; enabled: !providers.busy
                            onClicked: {
                                providers.saveConnection(providersPage.editId, providersPage.editRevision, providerName.text,
                                    providerKind.currentText, providerEndpoint.text, providerModel.text,
                                    providerPolicy.currentText, providerEnabled.checked, providerKey.text)
                                providerKey.text = ""
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: workspacesPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 6

        FileDialog {
            id: workspaceOpenDialog
            title: "打开叙演工作区"
            fileMode: FileDialog.OpenFile
            nameFilters: ["SQLite 工作区 (*.sqlite *.db)", "所有文件 (*)"]
            onAccepted: workspaceCatalog.openWorkspace(selectedFile)
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 72; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    Label { text: "工作区中心"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "当前：" + workspaceCatalog.currentPath; color: root.muted; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.maximumWidth: 850 }
                }
                Item { Layout.fillWidth: true }
                ActionButton { text: "打开已有文件"; enabled: !workspaceCatalog.busy; onClicked: workspaceOpenDialog.open() }
            }
        }
        Label { visible: workspaceCatalog.errorText.length > 0; text: workspaceCatalog.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Card {
            Layout.fillWidth: true
            implicitHeight: 246
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Label { text: "首次使用 · 灰港议和完整向导"; color: root.ink; font.pixelSize: 19; font.bold: true }
                        Label {
                            text: workspaceCatalog.demoReady
                                  ? "演示基线已就绪。接下来运行 5 回合、创建 A/B 分支并导出选定结果。"
                                  : "一键安装完全自创的短文、证据、世界 v1、历史快照、林舟实例与固定分支根。"
                            color: workspaceCatalog.demoReady ? root.teal : root.muted; font.pixelSize: 11
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: workspaceCatalog.demoCompleted + " / " + workspaceCatalog.demoTotal
                        color: workspaceCatalog.demoReady ? root.teal : root.amber; font.pixelSize: 16; font.bold: true
                    }
                    ActionButton {
                        text: workspaceCatalog.demoReady ? "重新检查" : "安装完整演示"
                        highlighted: !workspaceCatalog.demoReady
                        enabled: !workspaceCatalog.demoBusy
                        onClicked: workspaceCatalog.demoReady ? workspaceCatalog.refreshDemo() : workspaceCatalog.installDemoWorld()
                    }
                    ActionButton {
                        text: "以后再说"; visible: workspaceCatalog.onboardingVisible
                        enabled: !workspaceCatalog.demoBusy; onClicked: workspaceCatalog.dismissOnboarding()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: 6; radius: 3; color: "#0d151d"
                    Rectangle {
                        height: parent.height; radius: 3; color: root.teal
                        width: parent.width * (workspaceCatalog.demoTotal > 0
                                               ? workspaceCatalog.demoCompleted / workspaceCatalog.demoTotal : 0)
                        Behavior on width { NumberAnimation { duration: 180 } }
                    }
                }
                ListView {
                    Layout.fillWidth: true; Layout.preferredHeight: 82; orientation: ListView.Horizontal
                    spacing: 8; clip: true; model: workspaceCatalog.demoStages
                    delegate: Rectangle {
                        required property var modelData
                        width: 230; height: 78; radius: 8
                        color: modelData.ready ? "#12302f" : "#18232d"
                        border.color: modelData.ready ? root.teal : root.line
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 9
                            Rectangle {
                                width: 24; height: 24; radius: 12
                                color: modelData.ready ? root.teal : "#263746"
                                Label { anchors.centerIn: parent; text: modelData.ready ? "✓" : "·"; color: modelData.ready ? "#071413" : root.muted; font.bold: true }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Label { text: modelData.title; color: root.ink; font.pixelSize: 12; font.bold: true }
                                Label { Layout.fillWidth: true; text: modelData.detail; color: root.muted; font.pixelSize: 9; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Label { text: workspaceCatalog.demoBusy ? "正在写入可恢复阶段…" : workspaceCatalog.statusText; color: workspaceCatalog.demoBusy ? root.amber : root.teal; Layout.fillWidth: true; elide: Text.ElideRight }
                    ActionButton { text: "1 查看来源证据"; enabled: workspaceCatalog.demoReady; onClicked: root.activePage = 2 }
                    ActionButton { text: "2 核对世界与入场"; enabled: workspaceCatalog.demoReady; onClicked: root.activePage = 9 }
                    ActionButton { text: "3 运行 5 回合"; enabled: workspaceCatalog.demoReady; onClicked: root.activePage = 0 }
                    ActionButton { text: "4 比较并导出"; enabled: workspaceCatalog.demoReady; onClicked: root.activePage = 4 }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            Card {
                Layout.preferredWidth: 430; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                    Label { text: "创建工作区"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { text: "每个工作区使用独立 SQLite、资产目录、提交链和分支；API Key 始终留在系统凭据库。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.3 }
                    Label { text: "名称"; color: root.muted }
                    InputField { id: newWorkspaceName; Layout.fillWidth: true; placeholderText: "例如：灰港续篇" }
                    ActionButton {
                        text: "创建并打开"; highlighted: true; enabled: !workspaceCatalog.busy
                        onClicked: workspaceCatalog.createWorkspace(newWorkspaceName.text)
                    }
                    Label { text: workspaceCatalog.busy ? "正在校验并打开工作区…" : workspaceCatalog.statusText; color: workspaceCatalog.busy ? root.amber : root.teal; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Item { Layout.fillHeight: true }
                    Label { text: "切换工作区会启动新进程，使所有 SQLite 与网络对象在原线程安全销毁后重新绑定。"; color: root.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 12
                    Label { text: "最近项目"; color: root.ink; font.pixelSize: 20; font.bold: true }
                    Label { visible: workspaceCatalog.recentWorkspaces.length === 0; text: "尚无最近项目"; color: root.muted }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                        model: workspaceCatalog.recentWorkspaces
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width; height: 82; radius: 8; color: "#101922"; border.color: root.line
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 12
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; color: root.ink; font.bold: true }
                                    Label { text: modelData.path; color: root.muted; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 10 }
                                    Label { text: "最近打开 " + modelData.lastOpened; color: root.muted; font.pixelSize: 9 }
                                }
                                ActionButton { text: "打开"; enabled: !workspaceCatalog.busy; onClicked: workspaceCatalog.switchToRecent(index) }
                                ActionButton { text: "移除记录"; enabled: !workspaceCatalog.busy; onClicked: workspaceCatalog.forgetRecent(index) }
                            }
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: tasksPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 7

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 72; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                ColumnLayout {
                    Layout.maximumWidth: 365
                    Label { text: "提取任务中心"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "分块、检查点、失败重试与 unknown 请求均持久化；已完成步骤不会重复执行"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                InputField { id: taskSourceId; Layout.preferredWidth: 220; Layout.maximumWidth: 220; text: sources.selectedSourceId; placeholderText: "来源 ID"; ToolTip.visible: hovered; ToolTip.text: "来源 ID" }
                InputField {
                    id: taskChunkSize; Layout.preferredWidth: 78; Layout.maximumWidth: 78; text: "6000"
                    validator: IntValidator { bottom: 500; top: 50000 }
                    ToolTip.visible: hovered; ToolTip.text: "每块最大码点"
                }
                InputField {
                    id: taskOverlap; Layout.preferredWidth: 68; Layout.maximumWidth: 68; text: "200"
                    validator: IntValidator { bottom: 0 }
                    ToolTip.visible: hovered; ToolTip.text: "重叠码点"
                }
                InputField {
                    id: taskMaxRequests; Layout.preferredWidth: 68; Layout.maximumWidth: 68; text: "0"; placeholderText: "自动"
                    validator: IntValidator { bottom: 0; top: 1000000 }
                    ToolTip.visible: hovered; ToolTip.text: "调用硬上限；0 为按步骤自动计算"
                }
                InputField {
                    id: taskOutputTokens; Layout.preferredWidth: 76; Layout.maximumWidth: 76; text: "1200"; placeholderText: "输出"
                    validator: IntValidator { bottom: 1; top: 1000000 }
                    ToolTip.visible: hovered; ToolTip.text: "单次输出 token 上限"
                }
                ActionButton {
                    text: "创建分块任务"; highlighted: true; enabled: !extractionJobs.busy && taskSourceId.text.length > 0
                    onClicked: extractionJobs.createJob(taskSourceId.text, Number(taskChunkSize.text), Number(taskOverlap.text), Number(taskMaxRequests.text), Number(taskOutputTokens.text))
                }
                ActionButton { text: "刷新"; enabled: !extractionJobs.busy; onClicked: extractionJobs.refresh() }
            }
        }
        Label { visible: extractionJobs.errorText.length > 0; text: extractionJobs.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { text: extractionJobs.busy ? "正在更新任务…" : extractionJobs.statusText; color: extractionJobs.busy ? root.amber : root.teal }
        Label { visible: extractionJobs.jobs.length === 0 && !extractionJobs.busy; text: "还没有提取任务。先在“来源与章节”中导入并选择来源。"; color: root.muted }
        ListView {
            id: extractionJobList
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10; clip: true
            model: extractionJobs.jobs
            delegate: Card {
                required property var modelData
                width: extractionJobList.width; height: 154
                RowLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 16
                    ColumnLayout {
                        Layout.preferredWidth: 380; Layout.fillHeight: true
                        RowLayout {
                            Label { text: modelData.id; color: root.ink; font.bold: true }
                            Label {
                                text: modelData.status
                                color: modelData.status === "completed" ? root.teal
                                     : modelData.status === "needs_attention" ? root.red : root.amber
                                font.bold: true
                            }
                        }
                        Label { text: modelData.sourceId; color: root.muted; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: modelData.promptVersion + " · " + modelData.schemaVersion + " · 修订 " + modelData.revision; color: root.muted; font.pixelSize: 10 }
                        Label { text: "估算输入 ≤ " + modelData.estimatedTokens + " token · 输出/次 ≤ " + modelData.outputTokenLimit + " · 调用 " + modelData.consumedRequests + "/" + modelData.maxRequests + " · " + (modelData.priceKnown ? "已配置价格" : "费用未知"); color: modelData.priceKnown ? root.muted : root.amber; font.pixelSize: 9 }
                        ProgressBar { Layout.fillWidth: true; from: 0; to: Math.max(1, modelData.total); value: modelData.completed }
                        Label { text: modelData.completed + " / " + modelData.total + " 步已提交"; color: root.muted; font.pixelSize: 10 }
                    }
                    Rectangle { width: 1; Layout.fillHeight: true; color: root.line }
                    Flow {
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
                        Repeater {
                            model: modelData.steps
                            delegate: Rectangle {
                                required property var modelData
                                width: 94; height: 42; radius: 6
                                color: modelData.status === "completed" ? "#18352f"
                                     : modelData.status === "unknown" || modelData.status === "failed" ? "#3a2528" : "#17232d"
                                border.color: modelData.status === "completed" ? root.teal
                                            : modelData.status === "unknown" || modelData.status === "failed" ? root.red : root.line
                                Column {
                                    anchors.centerIn: parent
                                    Label { text: "步骤 " + modelData.ordinal; color: root.ink; font.pixelSize: 10 }
                                    Label { text: modelData.status + " · a" + modelData.attempt; color: root.muted; font.pixelSize: 8 }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        ActionButton {
                            text: "抽样质量审计"
                            visible: modelData.completed > 0
                            enabled: !extractionJobs.busy
                            onClicked: extractionJobs.auditJob(modelData.id)
                        }
                        ActionButton {
                            text: "离线 Mock 执行"
                            visible: modelData.status === "queued"
                            enabled: !extractionJobs.busy
                            onClicked: extractionJobs.runMock(modelData.id)
                        }
                        ActionButton {
                            text: "重试问题步骤"; visible: modelData.problemOrdinal > 0; enabled: !extractionJobs.busy
                            onClicked: extractionJobs.retryStep(modelData.id, modelData.problemOrdinal, modelData.problemAttempt)
                        }
                        ActionButton {
                            text: "取消未开始步骤"
                            visible: modelData.status !== "completed" && modelData.status !== "cancelled"
                            enabled: !extractionJobs.busy
                            onClicked: extractionJobs.cancelJob(modelData.id, modelData.revision)
                        }
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }

    ColumnLayout {
        id: reviewPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14
        visible: root.activePage === 8

        Connections {
            target: candidateReview
            function onChanged() {
                if (!candidateReview.busy) {
                    reviewName.text = candidateReview.selectedName
                    reviewFields.text = candidateReview.selectedFields
                    reviewProvenance.currentIndex = Math.max(0, reviewProvenance.find(candidateReview.selectedProvenance))
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 72; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                ColumnLayout {
                    Label { text: "候选校对中心"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "模型输出先进入候选区；接受时世界条目、证据引用与审核修订原子提交"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                Label { text: "队列"; color: root.muted }
                ComboBox {
                    id: reviewFilter
                    Layout.preferredWidth: 130
                    model: [
                        { text: "待校对", value: "candidate" }, { text: "冲突", value: "conflicted" },
                        { text: "已接受", value: "accepted" }, { text: "已拒绝", value: "rejected" }
                    ]
                    textRole: "text"; valueRole: "value"
                    onActivated: candidateReview.setFilter(currentValue)
                }
                ActionButton { text: "刷新"; enabled: !candidateReview.busy; onClicked: candidateReview.refresh() }
            }
        }
        Label { visible: candidateReview.errorText.length > 0; text: candidateReview.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { text: candidateReview.busy ? "正在更新校对队列…" : candidateReview.statusText; color: candidateReview.busy ? root.amber : root.teal }

        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14
            Card {
                Layout.preferredWidth: 420; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    Label { text: "候选列表"; color: root.ink; font.pixelSize: 18; font.bold: true }
                    Label {
                        visible: candidateReview.candidates.length === 0 && !candidateReview.busy
                        text: "此队列为空。可先在任务中心执行离线 Mock 提取。"; color: root.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    ListView {
                        id: candidateList
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                        model: candidateReview.candidates
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: candidateList.width; height: 112; radius: 8
                            color: candidateReview.selectedIndex === index ? "#253744" : "#101922"
                            border.color: candidateReview.selectedIndex === index ? root.teal : root.line
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 11; spacing: 4
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.name; color: root.ink; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: modelData.type; color: root.amber; font.pixelSize: 10 }
                                    Label { text: "v" + modelData.revision; color: root.muted; font.pixelSize: 9 }
                                }
                                Label { text: "“" + modelData.quote + "”"; color: root.ink; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 11 }
                                Label { text: modelData.source + "  ·  " + modelData.start + "–" + modelData.end; color: root.muted; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 9 }
                                Label { text: modelData.provenance + " · " + modelData.reviewStatus; color: root.teal; font.pixelSize: 9 }
                            }
                            MouseArea { anchors.fill: parent; onClicked: candidateReview.selectCandidate(index) }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }

            Card {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 20; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: candidateReview.selectedId.length ? "审核 " + candidateReview.selectedType : "选择一个候选"; color: root.ink; font.pixelSize: 20; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Label { text: candidateReview.selectedId.length ? "修订 " + candidateReview.selectedRevision : ""; color: root.muted }
                    }
                    Label { text: "名称"; color: root.muted }
                    InputField { id: reviewName; Layout.fillWidth: true; enabled: candidateReview.selectedId.length > 0 && !candidateReview.busy }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "来源类型"; color: root.muted }
                            ComboBox {
                                id: reviewProvenance; Layout.fillWidth: true
                                model: ["original_fact", "in_text_claim", "model_inference", "author_setting"]
                                enabled: candidateReview.selectedId.length > 0 && !candidateReview.busy
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: "证据位置（Unicode 码点）"; color: root.muted }
                            Label { text: candidateReview.selectedSource + "  ·  " + candidateReview.selectedRange; color: root.ink; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        }
                    }
                    Label { text: "字段 JSON"; color: root.muted }
                    ScrollView {
                        Layout.fillWidth: true; Layout.preferredHeight: 150
                        TextArea {
                            id: reviewFields; color: root.ink; font.family: "Consolas"; font.pixelSize: 12
                            wrapMode: TextEdit.Wrap; enabled: candidateReview.selectedId.length > 0 && !candidateReview.busy
                            background: Rectangle { radius: 7; color: "#101922"; border.color: reviewFields.activeFocus ? root.teal : root.line }
                        }
                    }
                    Label { text: "不可编辑的来源引文"; color: root.muted }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; radius: 8; color: "#101922"; border.color: root.line
                        ScrollView {
                            anchors.fill: parent; anchors.margins: 10
                            Label { width: parent.width; text: candidateReview.selectedQuote.length ? "“" + candidateReview.selectedQuote + "”" : "尚未选择候选"; color: root.ink; wrapMode: Text.Wrap; lineHeight: 1.35 }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        readonly property bool editableQueue: candidateReview.filter === "candidate" || candidateReview.filter === "conflicted"
                        ActionButton {
                            text: "保存编辑"; enabled: parent.editableQueue && candidateReview.selectedId.length > 0 && !candidateReview.busy
                            onClicked: candidateReview.reviewSelected("candidate", reviewName.text, reviewFields.text, reviewProvenance.currentText)
                        }
                        ActionButton {
                            text: "标记冲突"; enabled: parent.editableQueue && candidateReview.selectedId.length > 0 && !candidateReview.busy
                            onClicked: candidateReview.reviewSelected("conflicted", reviewName.text, reviewFields.text, reviewProvenance.currentText)
                        }
                        ActionButton {
                            text: "拒绝"; enabled: parent.editableQueue && candidateReview.selectedId.length > 0 && !candidateReview.busy
                            onClicked: candidateReview.reviewSelected("rejected", reviewName.text, reviewFields.text, reviewProvenance.currentText)
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: "接受并写入世界"; highlighted: true
                            enabled: parent.editableQueue && candidateReview.selectedId.length > 0 && !candidateReview.busy
                            onClicked: candidateReview.reviewSelected("accepted", reviewName.text, reviewFields.text, reviewProvenance.currentText)
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: worldViewsPage
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10
        visible: root.activePage === 9

        Rectangle {
            Layout.fillWidth: true; implicitHeight: 66; radius: 10; color: root.panel; border.color: root.line
            RowLayout {
                anchors.fill: parent; anchors.margins: 12
                ColumnLayout {
                    Label { text: "世界版本与结构视图"; color: root.ink; font.pixelSize: 17; font.bold: true }
                    Label { text: "不可变版本 · 双时间轴 · 定向关系 · 地点拓扑 · 人物入场"; color: root.muted; font.pixelSize: 10 }
                }
                Item { Layout.fillWidth: true }
                Label { text: worldViews.busy ? "正在后台更新…" : worldViews.statusText; color: worldViews.busy ? root.amber : root.teal }
                ActionButton { text: "刷新"; enabled: !worldViews.busy; onClicked: worldViews.refresh() }
            }
        }
        Label { visible: worldViews.errorText.length > 0; text: worldViews.errorText; color: root.red; Layout.fillWidth: true; wrapMode: Text.Wrap }
        TabBar {
            id: worldViewTabs; Layout.fillWidth: true
            TabButton { text: "版本与入场" }
            TabButton { text: "时间与事件" }
            TabButton { text: "定向关系" }
            TabButton { text: "地图与路线" }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: worldViewTabs.currentIndex

            RowLayout {
                spacing: 12
                Card {
                    Layout.preferredWidth: 480; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 16; spacing: 8
                        Label { text: "已发布世界版本"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; model: worldViews.versions; clip: true; spacing: 7
                            delegate: Rectangle {
                                required property var modelData
                                width: ListView.view.width; height: 82; radius: 7; color: "#101922"; border.color: root.line
                                Column { anchors.fill: parent; anchors.margins: 10; spacing: 4
                                    Label { text: modelData.id; color: root.ink; font.bold: true }
                                    Label { text: modelData.members + " 个固定成员 · " + modelData.hash.substring(0, 12); color: root.teal; font.pixelSize: 10 }
                                    Label { text: modelData.parent.length ? "父版本 " + modelData.parent : "首个版本"; color: root.muted; font.pixelSize: 9; width: parent.width; elide: Text.ElideMiddle }
                                }
                            }
                        }
                    }
                }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        width: Math.max(620, worldViewsPage.width - 560); spacing: 8
                        Label { text: "发布与历史快照"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        InputField { id: versionParent; Layout.fillWidth: true; placeholderText: "父版本 ID（首版留空）" }
                        RowLayout { Layout.fillWidth: true
                            ActionButton { text: "发布当前世界"; highlighted: true; enabled: !worldViews.busy; onClicked: worldViews.publishVersion(versionParent.text) }
                            InputField { id: snapshotVersion; Layout.fillWidth: true; placeholderText: "世界版本 ID" }
                            InputField { id: snapshotTime; Layout.preferredWidth: 110; text: "0"; placeholderText: "故事时间" }
                            ActionButton { text: "生成快照"; enabled: !worldViews.busy; onClicked: worldViews.prepareSnapshot(snapshotVersion.text, snapshotTime.text) }
                        }
                        Label { text: "最近快照  " + (worldViews.latestSnapshotId || "尚未在本次会话生成"); color: root.teal; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                        Label { text: "人物实例"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        RowLayout { Layout.fillWidth: true
                            InputField { id: instanceCard; Layout.fillWidth: true; text: "blueprint-linzhou"; placeholderText: "人物卡 ID" }
                            InputField { id: instanceCardVersion; Layout.preferredWidth: 70; text: "1" }
                            ComboBox { id: instancePolicy; Layout.preferredWidth: 140; model: ["strict", "public_only", "author_selected"] }
                        }
                        InputField { id: instanceWorldVersion; Layout.fillWidth: true; placeholderText: "世界版本 ID" }
                        InputField { id: instanceSnapshot; Layout.fillWidth: true; placeholderText: "历史快照 ID"; text: worldViews.latestSnapshotId }
                        ScrollView { Layout.fillWidth: true; Layout.preferredHeight: 86
                            TextArea { id: instanceAdaptation; text: "{\"echo\":\"消耗专注的残响\",\"铜制指针\":\"普通调查工具\"}"; color: root.ink; wrapMode: TextEdit.Wrap; background: Rectangle { color: "#101922"; border.color: root.line; radius: 7 } }
                        }
                        ActionButton { text: "创建世界人物实例"; highlighted: true; enabled: !worldViews.busy; onClicked: worldViews.instantiateCharacter(instanceCard.text, Number(instanceCardVersion.text), instanceWorldVersion.text, instanceSnapshot.text, instanceAdaptation.text, instancePolicy.currentText) }
                        Repeater { model: worldViews.instances; delegate: Label { required property var modelData; Layout.fillWidth: true; text: modelData.name + " · " + modelData.id + " · " + modelData.policy + " · " + modelData.status + (modelData.conflicts ? "（冲突 " + modelData.conflicts + "）" : ""); color: modelData.status === "ready" ? root.teal : root.amber; elide: Text.ElideMiddle } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                        Label { text: "固定分支根"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        RowLayout { Layout.fillWidth: true
                            InputField { id: bindBranch; Layout.fillWidth: true; text: "branch-main"; placeholderText: "分支 ID" }
                            ComboBox { id: historyMode; Layout.preferredWidth: 170; model: ["original_constrained", "branching", "sandbox"] }
                        }
                        InputField { id: bindInstance; Layout.fillWidth: true; placeholderText: "就绪的人物实例 ID" }
                        ActionButton { text: "固定版本、快照与人物"; enabled: !worldViews.busy; onClicked: worldViews.bindBranch(bindBranch.text, instanceWorldVersion.text, instanceSnapshot.text, historyMode.currentText, bindInstance.text) }
                    }
                }
            }

            RowLayout {
                spacing: 12
                Card { Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14
                        Label { text: "故事时间顺序（未知时间保留）"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        ListView { Layout.fillWidth: true; Layout.fillHeight: true; model: worldViews.timeline; spacing: 6; clip: true
                            delegate: Rectangle { required property var modelData; width: ListView.view.width; height: 66; radius: 7; color: "#101922"; border.color: modelData.truthStatus === "fact" ? root.line : root.amber
                                RowLayout { anchors.fill: parent; anchors.margins: 10
                                    Label { text: modelData.storyTime; color: modelData.storyTime === "未知" ? root.amber : root.teal; Layout.preferredWidth: 70 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label { text: modelData.name; color: root.ink; font.bold: true }
                                        Label { text: "叙述序 " + modelData.narrativeOrder + " · " + modelData.relativeTime + " · 因 " + modelData.causes + " / 果 " + modelData.results; color: root.muted; font.pixelSize: 9 }
                                    }
                                    Label { text: modelData.truthStatus; color: root.amber }
                                }
                            }
                        }
                    }
                }
                Card { Layout.preferredWidth: 390; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 9
                        Label { text: "新增时间事件"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        InputField { id: timelineName; Layout.fillWidth: true; placeholderText: "事件名称" }
                        InputField { id: timelineStory; Layout.fillWidth: true; placeholderText: "故事时间整数；未知留空" }
                        InputField { id: timelineNarrative; Layout.fillWidth: true; text: "0"; placeholderText: "叙述顺序" }
                        InputField { id: timelineRelative; Layout.fillWidth: true; placeholderText: "相对时间说明" }
                        ComboBox { id: timelineTruth; Layout.fillWidth: true; model: ["fact", "claim", "hypothesis", "future_candidate"] }
                        Item { Layout.fillHeight: true }
                        ActionButton { text: "保存事件"; highlighted: true; enabled: !worldViews.busy; onClicked: worldViews.addTimelineEvent(timelineName.text, timelineStory.text, Number(timelineNarrative.text), timelineRelative.text, timelineTruth.currentText) }
                    }
                }
            }

            RowLayout {
                spacing: 12
                Card { Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14
                        Label { text: "A→B 与 B→A 独立"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        ListView { Layout.fillWidth: true; Layout.fillHeight: true; model: worldViews.relations; spacing: 6; clip: true
                            delegate: Rectangle { required property var modelData; width: ListView.view.width; height: 60; radius: 7; color: "#101922"; border.color: modelData.evidenceStatus === "evidence" ? root.line : root.amber
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 10
                                    Label { text: modelData.from; color: root.ink; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                    Label { text: "→  " + modelData.dimension + "  " + modelData.strength + "  →"; color: root.teal }
                                    Label { text: modelData.to; color: root.ink; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                    Label { text: modelData.visibility; color: root.muted }
                                }
                            }
                        }
                    }
                }
                Card { Layout.preferredWidth: 390; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 9
                        Label { text: "新增定向关系"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        InputField { id: relationFrom; Layout.fillWidth: true; placeholderText: "起点实体 ID" }
                        InputField { id: relationTo; Layout.fillWidth: true; placeholderText: "终点实体 ID" }
                        InputField { id: relationDimension; Layout.fillWidth: true; placeholderText: "维度，例如 trust" }
                        InputField { id: relationStrength; Layout.fillWidth: true; text: "0"; placeholderText: "强度 -100..100" }
                        ComboBox { id: relationVisibility; Layout.fillWidth: true; model: ["public", "author"] }
                        ComboBox { id: relationEvidence; Layout.fillWidth: true; model: ["evidence", "assumption"] }
                        Item { Layout.fillHeight: true }
                        ActionButton { text: "保存关系"; highlighted: true; enabled: !worldViews.busy; onClicked: worldViews.addRelation(relationFrom.text, relationTo.text, relationDimension.text, Number(relationStrength.text), relationVisibility.currentText, relationEvidence.currentText) }
                    }
                }
            }

            RowLayout {
                spacing: 12
                Card { Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 14
                        Label { text: "地点层级与底图标点"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 280; radius: 8; color: "#101922"; border.color: root.line; clip: true
                            Repeater { model: worldViews.locations
                                delegate: Rectangle { required property var modelData; required property int index; width: 118; height: 46; radius: 8; x: modelData.x === "未知" ? 20 + (index % 5) * 128 : Math.min(parent.width - width - 10, Number(modelData.x)); y: modelData.y === "未知" ? 18 + Math.floor(index / 5) * 58 : Math.min(parent.height - height - 10, Number(modelData.y)); color: modelData.evidenceStatus === "evidence" ? "#18352f" : "#3b3020"; border.color: modelData.evidenceStatus === "evidence" ? root.teal : root.amber
                                    Column {
                                        anchors.centerIn: parent
                                        Label { text: modelData.id; width: 105; elide: Text.ElideMiddle; color: root.ink; font.pixelSize: 9 }
                                        Label { text: modelData.x + ", " + modelData.y; color: root.muted; font.pixelSize: 8 }
                                    }
                                }
                            }
                        }
                        Label { text: "路线"; color: root.muted }
                        Repeater { model: worldViews.routes; delegate: Label { required property var modelData; text: modelData.from + (modelData.bidirectional ? " ↔ " : " → ") + modelData.to + " · " + modelData.minutes + " 分钟 · " + modelData.evidenceStatus; color: modelData.evidenceStatus === "evidence" ? root.ink : root.amber; Layout.fillWidth: true; elide: Text.ElideMiddle } }
                    }
                }
                Card { Layout.preferredWidth: 410; Layout.fillHeight: true
                    ColumnLayout { anchors.fill: parent; anchors.margins: 16; spacing: 8
                        Label { text: "新增地点标注"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        InputField { id: mapLocation; Layout.fillWidth: true; placeholderText: "地点实体 ID" }
                        InputField { id: mapParent; Layout.fillWidth: true; placeholderText: "父地点 ID（可空）" }
                        RowLayout {
                            Layout.fillWidth: true
                            InputField { id: mapX; Layout.fillWidth: true; placeholderText: "底图 X（可空）" }
                            InputField { id: mapY; Layout.fillWidth: true; placeholderText: "底图 Y（可空）" }
                        }
                        ComboBox { id: mapEvidence; Layout.fillWidth: true; model: ["evidence", "assumption"] }
                        ActionButton { text: "保存地点"; enabled: !worldViews.busy; onClicked: worldViews.addLocation(mapLocation.text, mapParent.text, mapX.text, mapY.text, mapEvidence.currentText) }
                        Rectangle { Layout.fillWidth: true; height: 1; color: root.line }
                        Label { text: "新增路线"; color: root.ink; font.pixelSize: 18; font.bold: true }
                        InputField { id: routeFrom; Layout.fillWidth: true; placeholderText: "起点地点 ID" }
                        InputField { id: routeTo; Layout.fillWidth: true; placeholderText: "终点地点 ID" }
                        InputField { id: routeMinutes; Layout.fillWidth: true; placeholderText: "行程分钟；未知留空" }
                        ComboBox { id: routeEvidence; Layout.fillWidth: true; model: ["evidence", "assumption"] }
                        Item { Layout.fillHeight: true }
                        ActionButton { text: "保存双向路线"; highlighted: true; enabled: !worldViews.busy; onClicked: worldViews.addRoute(routeFrom.text, routeTo.text, routeMinutes.text, routeEvidence.currentText) }
                    }
                }
            }
        }
    }
}
