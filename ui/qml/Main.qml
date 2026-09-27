import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1366
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("叙演工坊")
    property int activePage: 10

    UiTheme { id: theme; mode: workspaceCatalog.themeId }
    color: theme.canvas

    // 页面切换只改变工作区内容，项目列表和当前工作区始终保留。
    function navigate(page) {
        activePage = page
        if (page === 1) workspace.refresh()
        if (page === 3) characters.refresh()
        if (page === 5) providers.refresh()
        if (page === 7) extractionJobs.refresh()
        if (page === 8) candidateReview.refresh()
        if (page === 9) worldViews.refresh()
    }
    Connections {
        target: workspaceCatalog
        // 创建并导入成功后直接进入章节工作区，接续校对流程。
        function onWorldCreated() { window.navigate(2) }
    }

    readonly property string pageTitle: {
        switch (activePage) {
        case 1: return qsTr("世界资料")
        case 2: return qsTr("小说与章节")
        case 3: return qsTr("人物卡")
        case 4: return qsTr("包与备份")
        case 5: return qsTr("模型连接")
        case 6: return qsTr("工作区管理")
        case 7: return qsTr("解析任务")
        case 8: return qsTr("校对中心")
        case 9: return qsTr("世界视图")
        default: return qsTr("开始")
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        ProjectSidebar {
            uiTheme: theme
            activePage: window.activePage
            Layout.preferredWidth: 244
            Layout.fillHeight: true
            onNavigate: window.navigate(page)
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: theme.surface
                border.color: theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 28
                    Label {
                        text: window.pageTitle
                        color: theme.text
                        font.pointSize: 14
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: workspaceCatalog.activeWorldId.length > 0
                            ? qsTr("当前世界已打开") : qsTr("尚未选择世界")
                        color: theme.muted
                        font.pointSize: 9
                    }
                }
            }
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: {
                    switch (window.activePage) {
                    case 1: return entityPage
                    case 2: return sourcePage
                    case 3: return characterPage
                    case 4: return packagePage
                    case 5: return providerPage
                    case 6: return workspacePage
                    case 7: return taskPage
                    case 8: return reviewPage
                    case 9: return viewsPage
                    default: return homePage
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: theme.sidebar
                border.color: theme.border
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    verticalAlignment: Text.AlignVCenter
                    text: workspaceCatalog.errorText.length > 0 ? workspaceCatalog.errorText
                         : workspaceCatalog.statusText
                    color: workspaceCatalog.errorText.length > 0 ? theme.danger : theme.muted
                    font.pointSize: 9
                    elide: Text.ElideRight
                }
            }
        }
    }

    Component { id: homePage; HomePage { uiTheme: theme; onNavigate: window.navigate(page) } }
    Component { id: sourcePage; SourcePage { uiTheme: theme; onNavigate: window.navigate(page) } }
    Component { id: taskPage; TaskPage { uiTheme: theme; onNavigate: window.navigate(page) } }
    Component { id: reviewPage; ReviewPage { uiTheme: theme; onNavigate: window.navigate(page) } }
    Component { id: entityPage; EntityPage { uiTheme: theme } }
    Component { id: providerPage; ProviderPage { uiTheme: theme } }
    Component { id: characterPage; CharacterPage { uiTheme: theme } }
    Component { id: packagePage; PackagePage { uiTheme: theme } }
    Component { id: workspacePage; WorkspacePage { uiTheme: theme } }
    Component { id: viewsPage; ViewsPage { uiTheme: theme } }
}
