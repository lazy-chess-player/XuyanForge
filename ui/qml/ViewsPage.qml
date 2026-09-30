import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 世界统计页：GUI 线程读取外部世界视图模型，仅现有类别计数和发布入口，不伪造图谱。 */
Flickable {
    id: page
    /* 窗口主题观察引用，无默认值；随页面存活，不在统计页创建主题设置。 */
    required property var uiTheme
    contentWidth: width
    contentHeight: content.implicitHeight + 48
    clip: true
    /* 世界记录摘要纵向布局，随滚动页存活。 */
    ColumnLayout {
        id: content
        x: 24
        y: 24
        width: page.width - 48
        spacing: 14
        /* 世界视图页标题。 */
        Label { text: qsTr("世界视图"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        /* 数据来源说明，明确统计来自当前工作区。 */
        Label { text: qsTr("版本、时间线、关系与地点均来自当前工作区，不预置任何资料。")
                color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 刷新与发布操作行。 */
        RowLayout {
            /* 显式刷新入口，只请求模型更新资料。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("刷新");
                /*
                 * 功能：显式刷新当前世界视图统计。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：存储读取或世界状态错误由 worldViews 展示。
                 * 副作用：请求模型刷新分类列表，不发布或发送小说。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: worldViews.refresh() }
            /* 显式发布入口，需选中世界；空说明交给模型，不自行修改历史版本。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("发布世界版本"); enabled: workspaceCatalog.activeWorldId.length > 0;
                /*
                 * 功能：显式发布当前世界版本，空字符串为未填写版本说明。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：世界缺失、读取或发布错误由模型报告。
                 * 副作用：请求模型保存版本；历史版本不由本回调修改。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: worldViews.publishVersion("") }
        }
        /* 模型结果状态，优先显示错误。 */
        Label { text: worldViews.errorText.length > 0 ? worldViews.errorText : worldViews.statusText;
                color: worldViews.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted;
                wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 四类统计分区重复器，引用真实版本、事件、关系及地点列表，不生成业务资料。 */
        Repeater {
            model: [ {"title": qsTr("世界版本"), "items": worldViews.versions},
                     {"title": qsTr("时间线"), "items": worldViews.timeline},
                     {"title": qsTr("关系"), "items": worldViews.relations},
                     {"title": qsTr("地点"), "items": worldViews.locations} ]
            /* 统计分区代理，读取对应模型列表，实例由重复器拥有。 */
            SectionPanel {
                /* 本分区的标题和真实列表引用，由 Repeater 注入，无默认值；只读 items.length，不生成图记录。 */
                required property var modelData
                uiTheme: page.uiTheme
                Layout.fillWidth: true
                implicitHeight: detail.implicitHeight + 32
                /* 分类标题与计数纵向布局。 */
                ColumnLayout {
                    id: detail
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    /* 分类中文标题，来源于本页 qsTr 映射。 */
                    Label { text: modelData.title; color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                    /* 实际记录数或空记录说明，不代表相关图形交互已实现。 */
                    Label { text: modelData.items.length === 0 ? qsTr("暂无记录") : qsTr("共 %1 条记录").arg(modelData.items.length);
                            color: page.uiTheme.muted }
                }
            }
        }
    }
}
