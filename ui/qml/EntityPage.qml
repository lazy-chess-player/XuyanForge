import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 当前世界资料页：GUI 线程显示后端分页结果与编辑表单；保存保留未展示属性，修订冲突由模型处理。 */
Item {
    id: page
    /* 共享主题观察引用，无默认值，窗口保证其覆盖页面生命周期。 */
    required property var uiTheme
    /* 世界资料模型的信号观察器，不拥有外部模型，随页面销毁断开。 */
    Connections {
        target: workspace
        /* 功能：按模型中的类型协议值同步显示选择，未知类型保持未选而不误归为人物。
         * 参数：无。返回：无。失败：查无选项时索引 -1，由中文 displayText 提示。
         * 副作用：更新类型选择，不提交资料；GUI 线程随页面连接存活。 */
        function onChanged() { kindChoice.currentIndex = kindChoice.indexOfValue(workspace.selectedKind) }
    }
    /* 资料列表与编辑区的左右布局。 */
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        /* 分页列表面板，宽度 300 像素，读取当前世界当前页。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            /* 搜索、列表及页码的纵向布局。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                /* 条目列表标题，不展示内部类型协议。 */
                Label { text: qsTr("条目"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                /* 查询输入，初始空；回车请求按名称刷新，查询本身不写资料。 */
                AppField { id: query; uiTheme: page.uiTheme; Layout.fillWidth: true; placeholderText: qsTr("搜索名称");
                    /*
                     * 功能：回车按当前查询输入 text 刷新资料列表。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：查询或存储错误由 workspace 展示。
                     * 副作用：请求只读查询，不保存编辑草稿。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onAccepted: workspace.refresh(text) }
                /* 搜索按钮，读取 query.text 请求模型刷新。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("搜索");
                    /*
                     * 功能：点击按 query.text 请求名称查询。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：查询或存储错误由 workspace 展示。
                     * 副作用：刷新模型当前页，不在 QML 全量筛选。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: workspace.refresh(query.text) }
                /* 空资料说明，依赖实际当前页列表，不生成默认实体。 */
                Label { visible: workspace.entityItems.length === 0; text: qsTr("暂无世界资料。解析并接受候选后会显示在这里。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 后端分页条目列表，读取 entityItems，不在 QML 全量过滤。 */
                ListView {
                    id: entityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: workspace.entityItems
                    /* 资料行代理，显示用户名称并按索引请求选择。 */
                    delegate: ItemDelegate {
                        /* 当前页条目元数据，由列表注入，无默认值；只读显示名称，不持有完整世界资料。 */
                        required property var modelData
                        /* 当前页内从零行索引，由列表注入，无默认值；选中高亮和选择动作读取。 */
                        required property int index
                        width: entityList.width
                        text: modelData.name
                        highlighted: workspace.selectedIndex === index
                        /*
                         * 功能：按当前页 index 选择世界条目。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：索引无效或资料失效由 workspace 处理。
                         * 副作用：更新模型选择和编辑字段，触发类型同步，不保存条目。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: workspace.selectEntity(index)
                    }
                }
                /* 分页操作行，由后端的翻页能力控制按钮。 */
                RowLayout {
                    /* 上一页入口，只在 hasPreviousPage 为真时可用。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("上一页"); enabled: workspace.hasPreviousPage;
                        /*
                         * 功能：请求模型返回当前过滤条件下的上一页。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：页边界和读取失败由 workspace 处理。
                         * 副作用：改变当前分页结果，不修改实体内容。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: workspace.previousPage() }
                    /* 后端页码状态文字，不自行推算总数。 */
                    Label { text: workspace.pageText; color: page.uiTheme.muted }
                    /* 下一页入口，只在 hasNextPage 为真时可用。 */
                    AppButton { uiTheme: page.uiTheme; text: qsTr("下一页"); enabled: workspace.hasNextPage;
                        /*
                         * 功能：请求模型读取下一页世界资料。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：页边界和读取失败由 workspace 处理。
                         * 副作用：改变分页结果，列表代理按模型更新。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: workspace.nextPage() }
                }
            }
        }
        /* 资料编辑面板，独立于列表区填满剩余空间。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            /* 编辑字段滚动容器，表单超出面板时保留访问入口。 */
            Flickable {
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: form.implicitHeight + 16
                clip: true
                /* 资料字段及保存操作纵向布局。 */
                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: 10
                    /* 资料编辑分区标题。 */
                    Label { text: qsTr("资料编辑"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 名称字段标签。 */
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    /* 名称草稿，绑定所选名称，用户编辑后保存读取原值。 */
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedName }
                    /* 类型字段标签，内部枚举仅供提交使用。 */
                    Label { text: qsTr("类型"); color: page.uiTheme.muted }
                    /* 中文类型选择，label/value 分离，初始索引 8 为其他，模型通知同步当前类型。 */
                    AppComboBox {
                        id: kindChoice
                        uiTheme: page.uiTheme
                        Layout.fillWidth: true
                        model: [
                            {"label": qsTr("人物"), "value": "character"},
                            {"label": qsTr("势力"), "value": "faction"},
                            {"label": qsTr("地点"), "value": "location"},
                            {"label": qsTr("物品"), "value": "item"},
                            {"label": qsTr("规则"), "value": "rule"},
                            {"label": qsTr("事件"), "value": "event"},
                            {"label": qsTr("文化"), "value": "culture"},
                            {"label": qsTr("技术"), "value": "technology"},
                            {"label": qsTr("其他"), "value": "other"}
                        ]
                        textRole: "label"
                        valueRole: "value"
                        currentIndex: 8
                        displayText: currentIndex < 0 ? qsTr("未知类型，请选择") : currentText
                    }
                    /* 描述字段标签。 */
                    Label { text: qsTr("描述"); color: page.uiTheme.muted }
                    /* 描述草稿，来自 selectedDescription，保存时不推断新的事实。 */
                    AppField { id: descriptionField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedDescription }
                    /* 别名分隔规则标签，提示现有逗号输入格式。 */
                    Label { text: qsTr("别名（逗号分隔）"); color: page.uiTheme.muted }
                    /* 别名草稿，来自 selectedAliases，保存交给模型解析。 */
                    AppField { id: aliasesField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedAliases }
                    /* 标签分隔规则说明。 */
                    Label { text: qsTr("标签（逗号分隔）"); color: page.uiTheme.muted }
                    /* 标签草稿，来自 selectedTags，保存交给模型解析。 */
                    AppField { id: tagsField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: workspace.selectedTags }
                    /* 保存和清空选择操作行。 */
                    RowLayout {
                        Layout.fillWidth: true
                        /* 资料保存入口，区分新建与已有条目编辑，已有条目保留未展示的属性 JSON。 */
                        AppButton {
                            uiTheme: page.uiTheme; text: qsTr("保存")
                            enabled: nameField.text.trim().length > 0 && kindChoice.currentIndex >= 0
                            /*
                             * 功能：按当前选择提交新建或编辑，读取五个输入字段与明确类型。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：名称、类型、预期修订或保存失败由 workspace 展示；未知类型时入口禁用。
                             * 副作用：无选中项则 createEntity 使用空属性对象；已有项 saveSelected 保留 selectedAttributes，不覆盖未展示属性。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: {
                                if (workspace.selectedIndex < 0)
                                    workspace.createEntity(nameField.text, kindChoice.currentValue, descriptionField.text,
                                                           aliasesField.text, tagsField.text, "{}")
                                else
                                    workspace.saveSelected(nameField.text, kindChoice.currentValue, descriptionField.text,
                                                           aliasesField.text, tagsField.text, workspace.selectedAttributes)
                            }
                        }
                        /* 清空选择入口，只准备空白草稿，不在此动作中创建条目。 */
                        AppButton { uiTheme: page.uiTheme; text: qsTr("新建空白条目");
                            /*
                             * 功能：清空所选条目准备空白草稿。
                             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                             * 返回：无。失败：清空选择的模型异常由 QML 报告。
                             * 副作用：只更新选择和草稿，不创建任何默认条目。
                             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                             */
                            onClicked: workspace.clearSelection() }
                    }
                    /* 资料模型结果反馈，错误优先于状态，不假定保存成功。 */
                    Label { text: workspace.errorText.length > 0 ? workspace.errorText : workspace.statusText; color: workspace.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
