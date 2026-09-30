import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

/* 应用主窗口：在 GUI 线程拥有侧栏、共享主题及当前页面，关闭时释放 QML 树；后台任务由视图模型管理。 */
ApplicationWindow {
    id: window
    width: 1366
    height: 800
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("叙演工坊")
    /* 内部路由编号，无单位；初始 10 为新建入口，navigate 写入，标题、侧栏和 Loader 读取。 */
    property int activePage: 10

    /* 窗口拥有的共享主题实例，跟随工作区保存的主题标识，页面只观察它。 */
    UiTheme { id: theme; mode: workspaceCatalog.themeId }
    color: theme.canvas

    /*
     * 功能：切换右侧页面，并按目标路由请求刷新相应视图模型。
     * 参数：page 为输入的内部整数路由，1—9 对应工具页，10 及其他值显示新建入口。
     * 返回：无。失败：不校验路由；刷新失败由目标模型的错误状态展示，未知路由回退入口。
     * 副作用：更新 activePage，Loader 销毁旧页并创建新页；刷新不启动小说模型发送。
     * 线程与生命周期：GUI 线程同步分派；模型自行管理刷新任务，窗口不拥有其后台线程。
     */
    function navigate(page) {
        activePage = page
        if (page === 1) workspace.refresh()
        if (page === 3) characters.refresh()
        if (page === 5) providers.refresh()
        if (page === 7) extractionJobs.refresh()
        if (page === 8) candidateReview.refresh()
        if (page === 9) worldViews.refresh()
    }
    /* 观察外部项目目录模型；连接随窗口销毁断开，不拥有该模型。 */
    Connections {
        target: workspaceCatalog
        /* 功能：导入成功后进入章节页。参数：无。返回：无。
         * 失败：导航刷新错误由模型展示。副作用：调用 navigate(2)。
         * 线程与生命周期：GUI 信号回调，仅在窗口连接存活时处理。 */
        function onWorldCreated() { window.navigate(2) }
    }

    /* 当前路由的中文标题，无单位；由 activePage 只读推导，未知值显示“开始”，保留 qsTr 翻译接口。 */
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

    /* 主窗口左右分区布局，固定侧栏和自适应工作区共同由窗口拥有。 */
    RowLayout {
        anchors.fill: parent
        spacing: 0
        /* 常驻项目侧栏：读取窗口路由、观察主题，将导航请求交还窗口。 */
        ProjectSidebar {
            uiTheme: theme
            activePage: window.activePage
            Layout.preferredWidth: 244
            Layout.fillHeight: true
            /*
             * 功能：转发侧栏或页面的内部路由请求给主窗口 navigate。
             * 参数：page 为信号输入的内部整数路由，转发不改变其值。
             * 返回：无。失败：导航刷新错误按 navigate 契约由目标模型报告。
             * 副作用：切换当前页面并刷新对应模型，不自动发送原文。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onNavigate: function(page) { window.navigate(page) }
        }
        /* 右侧工作区纵向布局，持有标题、当前页面及底部状态。 */
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            /* 页面标题栏背景，高度 60 像素，跟随主题。 */
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: theme.surface
                border.color: theme.border
                /* 标题与当前世界状态的横向布局。 */
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 28
                    /* 路由标题，读取 pageTitle，不显示内部路由数字。 */
                    Label {
                        text: window.pageTitle
                        color: theme.text
                        font.pointSize: 14
                        font.weight: Font.DemiBold
                    }
                    /* 弹性空白占据标题与世界状态之间的空间。 */
                    Item { Layout.fillWidth: true }
                    /* 当前世界选择状态，读取目录模型身份，不推断世界资料。 */
                    Label {
                        text: workspaceCatalog.activeWorldId.length > 0
                            ? qsTr("当前世界已打开") : qsTr("尚未选择世界")
                        color: theme.muted
                        font.pointSize: 9
                    }
                }
            }
            /* 当前页面加载器：根据路由实例化组件，切页销毁旧实例；业务任务寿命由外部模型决定。 */
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
            /* 底部状态栏背景，高度 30 像素，由窗口常驻拥有。 */
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: theme.sidebar
                border.color: theme.border
                /* 目录模型状态文字，错误优先，路径和用户名称按模型原值展示。 */
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

    /* 新建入口组件工厂及其主题观察者；Loader 拥有实际页面，导航回调在 GUI 线程转发 page 路由，无返回，接收不到时无动作。 */
    Component { id: homePage; HomePage { uiTheme: theme;
        /*
         * 功能：转发新建入口的页面导航请求。
         * 参数：page 为信号输入的内部整数路由，转发不改变其值。
         * 返回：无。失败：未知路由回退新建入口，刷新错误由模型展示。
         * 副作用：调用 window.navigate，旧页面由 Loader 清理。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onNavigate: function(page) { window.navigate(page) } } }
    /* 来源页组件工厂及页面实例定义；Loader 管理寿命，GUI 导航回调转发 page 整数，无返回，不保存资料。 */
    Component { id: sourcePage; SourcePage { uiTheme: theme;
        /*
         * 功能：转发来源页请求的内部路由。
         * 参数：page 为信号输入的内部整数路由，转发不改变其值。
         * 返回：无。失败：未知路由回退入口，模型刷新错误在目标页显示。
         * 副作用：调用 window.navigate，不保存来源草稿。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onNavigate: function(page) { window.navigate(page) } } }
    /* 任务页组件工厂，向实例提供主题；GUI 导航回调转发 page 整数，无返回，后台运行交给模型。 */
    Component { id: taskPage; TaskPage { uiTheme: theme;
        /*
         * 功能：转发任务页请求的内部路由。
         * 参数：page 为信号输入的内部整数路由，转发不改变其值。
         * 返回：无。失败：未知路由回退入口，模型刷新错误由模型报告。
         * 副作用：切页不取消模型拥有的批次。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onNavigate: function(page) { window.navigate(page) } } }
    /* 校对页组件工厂，Loader 管理实例；GUI 导航回调转发 page 整数，无返回，不自动接受候选。 */
    Component { id: reviewPage; ReviewPage { uiTheme: theme;
        /*
         * 功能：转发校对页请求的内部路由。
         * 参数：page 为信号输入的内部整数路由，转发不改变其值。
         * 返回：无。失败：未知路由回退入口，刷新错误由模型展示。
         * 副作用：调用 window.navigate，不隐式提交审核。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onNavigate: function(page) { window.navigate(page) } } }
    /* 世界资料组件工厂，Loader 创建表单实例并在切页时释放。 */
    Component { id: entityPage; EntityPage { uiTheme: theme } }
    /* 模型连接组件工厂，Loader 拥有页面及瞬时输入。 */
    Component { id: providerPage; ProviderPage { uiTheme: theme } }
    /* 人物卡组件工厂，提供主题，不预置任何卡片。 */
    Component { id: characterPage; CharacterPage { uiTheme: theme } }
    /* 包与备份组件工厂，仅实例化工具页，不自动操作文件。 */
    Component { id: packagePage; PackagePage { uiTheme: theme } }
    /* 工作区管理组件工厂，不因创建实例就打开或创建数据库。 */
    Component { id: workspacePage; WorkspacePage { uiTheme: theme } }
    /* 世界视图组件工厂，仅观察外部模型，切页释放统计页面。 */
    Component { id: viewsPage; ViewsPage { uiTheme: theme } }
}
