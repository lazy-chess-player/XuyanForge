import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 人物卡页面：GUI 线程随 Loader 存活，观察外部人物模型，输入字段只形成草稿而不预置人物。 */
Item {
    id: page
    /* 外部共享主题观察引用，无默认值；窗口须保证其覆盖页面生命周期。 */
    required property var uiTheme
    /* 人物卡列表与编辑区左右布局。 */
    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        /* 卡片列表面板，宽度 280 像素，绑定当前工作区人物模型。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            /* 列表、空态和新建动作纵向布局。 */
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                /* 人物卡列表标题。 */
                Label { text: qsTr("人物卡"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                /* 空卡片说明，提示用户自行创建，不生成任何默认卡。 */
                Label { visible: characters.cardItems.length === 0; text: qsTr("还没有人物卡。可从空白卡片开始。")
                        color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                /* 真实人物卡列表，行实例由列表拥有。 */
                ListView {
                    id: cards
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: characters.cardItems
                    clip: true
                    /* 人物卡行代理，读取名称和选中索引，点击选择当前卡。 */
                    delegate: ItemDelegate {
                        /* 当前人物卡元数据，由列表注入，无默认值；包含显示名称，代理只读观察，随行销毁释放。 */
                        required property var modelData
                        /* 当前列表从零行索引，由 ListView 注入，无默认值；点击选择读取，列表变化可更新。 */
                        required property int index
                        width: cards.width
                        text: modelData.name
                        highlighted: characters.selectedIndex === index
                        /*
                         * 功能：按卡片当前 index 请求选择人物卡。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：无效索引或失效卡片由 characters 处理。
                         * 副作用：更新所选卡片与表单，不创建人物实例。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: characters.selectCard(index)
                    }
                }
                /* 新建卡入口，仅清空模型选择，待保存才创建资料。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("新建人物卡");
                    /*
                     * 功能：清空人物卡选择，准备空白卡片草稿。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：模型状态错误由 characters 处理。
                     * 副作用：更新选择，不生成或保存卡片。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: characters.clearSelection() }
            }
        }
        /* 人物设定编辑面板，占据右侧剩余空间。 */
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            /* 人物表单滚动容器，长字段集合仍可访问。 */
            Flickable {
                anchors.fill: parent
                anchors.margins: 20
                contentWidth: width
                contentHeight: form.implicitHeight + 16
                clip: true
                /* 现有可见人物字段的纵向布局。 */
                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: 10
                    /* 人物设定标题。 */
                    Label { text: qsTr("人物设定"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 人物名称标签。 */
                    Label { text: qsTr("名称"); color: page.uiTheme.muted }
                    /* 名称草稿，绑定 characters.name，保存时读取用户编辑。 */
                    AppField { id: nameField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.name }
                    /* 人物概述标签。 */
                    Label { text: qsTr("概述"); color: page.uiTheme.muted }
                    /* 概述草稿，绑定 summary，不填入默认背景。 */
                    AppField { id: summaryField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.summary }
                    /* 性格特征标签。 */
                    Label { text: qsTr("性格特征"); color: page.uiTheme.muted }
                    /* 特征草稿，绑定 traits，由用户编辑后保存。 */
                    AppField { id: traitsField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.traits }
                    /* 长期目标标签。 */
                    Label { text: qsTr("长期目标"); color: page.uiTheme.muted }
                    /* 长期目标草稿，绑定 longGoal，不推断短期目标。 */
                    AppField { id: goalField; uiTheme: page.uiTheme; Layout.fillWidth: true; text: characters.longGoal }
                    /* 保存卡片入口，提交四个可见字段并保留模型中其他字段。 */
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("保存人物卡"); primary: true
                        enabled: nameField.text.trim().length > 0
                        /*
                         * 功能：保存名称、概述、性格与长期目标草稿，并带回模型中其余既有字段。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：名称无效、修订冲突或保存失败由 characters 报告。
                         * 副作用：请求保存人物卡，不根据四个可见字段重置价值观、背景、装备或私密备注。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onClicked: characters.saveCard(nameField.text, summaryField.text, characters.values,
                                                        traitsField.text, goalField.text, characters.shortGoal,
                                                        characters.speechStyle, characters.abilities, characters.equipment,
                                                        characters.background, characters.privateNotes)
                    }
                    /* 人物模型错误说明，只有错误非空才显示。 */
                    Label { visible: characters.errorText.length > 0; text: characters.errorText; color: page.uiTheme.danger; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
            }
        }
    }
}
