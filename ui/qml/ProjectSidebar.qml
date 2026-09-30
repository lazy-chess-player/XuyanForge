import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 项目侧栏：GUI 线程随主窗口常驻，观察目录模型，选择项目后请求来源页，不持有小说正文。 */
Rectangle {
    id: sidebar
    /* 主窗口提供的主题观察引用，无默认值，覆盖常驻侧栏生命周期。 */
    required property var uiTheme
    /* 主窗口当前内部路由，无单位，无默认值，由窗口绑定；侧栏只读取以标记导航高亮。 */
    required property int activePage
    /* 项目列表是否非空，无单位，默认由目录模型列表推导；只控制空态和导航可见性，不代表已经选择世界。 */
    readonly property bool hasWorld: workspaceCatalog.worlds.length > 0
    /* 功能：请求窗口导航。参数：page 为目标内部整数路由。返回：无。
     * 失败：无接收者时保持当前页。副作用：接收窗口可能刷新模型；GUI 线程连接随侧栏存活。 */
    signal navigate(int page)

    color: uiTheme.sidebar
    border.color: uiTheme.border

    /* 工作区打开文件框，由侧栏拥有，选择完整 URL，取消不切换数据库。 */
    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: workspaceDialog
        objectName: "workspaceDialog"
        title: qsTr("打开工作区")
        acceptLabel: qsTr("打开")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("工作区数据库 (*.sqlite *.db)")]
        /*
         * 功能：确认 selectedFile 后请求目录模型打开工作区。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：路径或打开失败由目录模型展示，不在回调删除原文件。
         * 副作用：请求数据库切换，不预置任何资料。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: workspaceCatalog.openWorkspace(selectedFile)
    }

    /* 侧栏导航纵向布局，主题、项目与工具入口保持既有位置。 */
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        /* 应用中文名称标题。 */
        Label {
            text: qsTr("叙演工坊")
            color: sidebar.uiTheme.text
            font.pointSize: 13
            font.weight: Font.DemiBold
            Layout.topMargin: 6
        }
        /* 新建世界入口，根据当前路由高亮，不直接创建资料。 */
        AppButton {
            uiTheme: sidebar.uiTheme
            text: qsTr("新建世界")
            primary: sidebar.activePage === 10
            Layout.fillWidth: true
            Layout.topMargin: 18
            /*
             * 功能：请求新建世界入口，内部路由 10。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者时保持当前页。
             * 副作用：发出 navigate，不创建世界。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: sidebar.navigate(10)
        }
        /* 项目分区标题。 */
        Label {
            text: qsTr("项目")
            color: sidebar.uiTheme.muted
            font.pointSize: 9
            Layout.topMargin: 20
            Layout.bottomMargin: 4
        }
        /* 空项目说明，依据真实目录列表，不生成示例世界。 */
        Label {
            visible: !sidebar.hasWorld
            text: qsTr("暂无项目")
            color: sidebar.uiTheme.muted
            font.pointSize: 10
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        /* 世界项目列表，绑定目录模型，行实例随列表存活。 */
        ListView {
            id: worldList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: workspaceCatalog.worlds
            /* 世界列表纵向滚动条，由 ListView 管理位置与生命周期。 */
            ScrollBar.vertical: ScrollBar {}
            /* 世界行代理，读取稳定身份和来源身份，点击显式选择项目并进入来源页。 */
            delegate: ItemDelegate {
                id: worldDelegate
                /* 当前世界行，由目录列表注入，无默认值；包含 id/name/sourceId，代理观察，点击复制身份至模型动作。 */
                required property var modelData
                /* 世界列表从零索引，由列表注入；点击选择读取，不跨列表刷新持久保存。 */
                required property int index
                width: worldList.width
                height: 38
                text: modelData.name
                font.pointSize: 10
                highlighted: workspaceCatalog.activeWorldId === modelData.id
                /* 世界名称显示，使用用户原值，读取代理字体和选中状态。 */
                contentItem: Text {
                    text: worldDelegate.text
                    font: worldDelegate.font
                    color: worldDelegate.highlighted ? sidebar.uiTheme.accent : sidebar.uiTheme.text
                    leftPadding: 10
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                /* 世界行悬停及焦点背景，不修改选择身份。 */
                background: Rectangle {
                    radius: 5
                    color: worldDelegate.highlighted || worldDelegate.hovered
                        ? sidebar.uiTheme.surfaceAlt : "transparent"
                    border.width: worldDelegate.activeFocus ? 2 : 0
                    border.color: sidebar.uiTheme.accent
                }
                /*
                 * 功能：选择本行世界与其记录的来源，并请求进入章节工作区。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：世界或来源选择失效由对应模型处理；本回调仍发出导航，不声称选择成功。
                 * 副作用：按当前 index 选择世界，按 modelData.sourceId 选择来源，发出路由 2。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: {
                    // 选中项目，并请求打开项目记录的小说来源。
                    workspaceCatalog.selectWorld(index)
                    sources.selectSourceId(modelData.sourceId)
                    sidebar.navigate(2)
                }
            }
        }

        /* 项目列表与常用导航的装饰分隔线。 */
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; Layout.topMargin: 8; Layout.bottomMargin: 6; color: sidebar.uiTheme.border }
        /* 小说与章节导航入口，仅存在项目时显示，路由 2 决定选中样式。 */
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("小说与章节"); quiet: true; currentPage: sidebar.activePage === 2; visible: sidebar.hasWorld; Layout.fillWidth: true;
            /*
             * 功能：请求小说与章节页，路由 2。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者无动作。
             * 副作用：只发出 navigate 信号。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: sidebar.navigate(2) }
        /* 解析任务导航入口，路由 7，不因切页启动解析。 */
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("解析任务"); quiet: true; currentPage: sidebar.activePage === 7; visible: sidebar.hasWorld; Layout.fillWidth: true;
            /*
             * 功能：请求解析任务页，路由 7。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者无动作。
             * 副作用：只发出 navigate，不启动任务。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: sidebar.navigate(7) }
        /* 校对中心导航入口，路由 8，不自动审核候选。 */
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("校对中心"); quiet: true; currentPage: sidebar.activePage === 8; visible: sidebar.hasWorld; Layout.fillWidth: true;
            /*
             * 功能：请求人工校对页，路由 8。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者无动作。
             * 副作用：只发出 navigate，不接受候选。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: sidebar.navigate(8) }
        /* 世界资料导航入口，路由 1，选中项目后可见。 */
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("世界资料"); quiet: true; currentPage: sidebar.activePage === 1; visible: sidebar.hasWorld; Layout.fillWidth: true;
            /*
             * 功能：请求世界资料页，路由 1。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：无接收者无动作。
             * 副作用：只发出 navigate，不保存条目。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: sidebar.navigate(1) }
        /* 工具菜单入口，点击只展开辅助导航。 */
        AppButton { uiTheme: sidebar.uiTheme; text: qsTr("更多工具"); quiet: true; Layout.fillWidth: true;
            /*
             * 功能：展开辅助工具菜单。
             * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
             * 返回：无。失败：组件状态错误由 Qt 报告。
             * 副作用：打开 toolsMenu，不执行菜单项动作。
             * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
             */
            onClicked: toolsMenu.open() }
        /* 辅助工具菜单，由侧栏拥有，页面导航经 navigate 信号传递。 */
        Menu {
            id: toolsMenu
            objectName: "toolsMenu"
            palette.window: sidebar.uiTheme.surface
            palette.text: sidebar.uiTheme.text
            /* 工具菜单主题背景。 */
            background: Rectangle { color: sidebar.uiTheme.surface; border.color: sidebar.uiTheme.border; radius: 6 }
            /* 人物卡菜单入口，目标内部路由 3。 */
            MenuItem { text: qsTr("人物卡");
                /*
                 * 功能：请求人物卡页，路由 3。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无接收者无动作。
                 * 副作用：发出 navigate，不创建卡片。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onTriggered: sidebar.navigate(3) }
            /* 世界视图菜单入口，目标内部路由 9。 */
            MenuItem { text: qsTr("世界视图");
                /*
                 * 功能：请求世界视图页，路由 9。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无接收者无动作。
                 * 副作用：发出 navigate，不发布世界。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onTriggered: sidebar.navigate(9) }
            /* 模型连接菜单入口，目标内部路由 5，点击不探测网络。 */
            MenuItem { text: qsTr("模型连接");
                /*
                 * 功能：请求模型连接页，路由 5。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无接收者无动作。
                 * 副作用：发出 navigate，不发起探测。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onTriggered: sidebar.navigate(5) }
            /* 包与备份菜单入口，目标内部路由 4，点击不读写文件。 */
            MenuItem { text: qsTr("包与备份");
                /*
                 * 功能：请求包与备份页，路由 4。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无接收者无动作。
                 * 副作用：发出 navigate，不读写包文件。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onTriggered: sidebar.navigate(4) }
            /* 工作区管理菜单入口，目标内部路由 6。 */
            MenuItem { text: qsTr("工作区管理");
                /*
                 * 功能：请求工作区管理页，路由 6。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无接收者无动作。
                 * 副作用：发出 navigate，不切换数据库。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onTriggered: sidebar.navigate(6) }
        }
        /* 打开工作区与最近记录的横向入口布局。 */
        RowLayout {
            Layout.fillWidth: true
            /* 打开工作区按钮，仅展开工作区选择框。 */
            AppButton {
                uiTheme: sidebar.uiTheme
                text: qsTr("打开工作区")
                quiet: true
                Layout.fillWidth: true
                /*
                 * 功能：显示工作区打开选择框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不打开工作区，组件错误由 Qt 报告。
                 * 副作用：打开 workspaceDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: workspaceDialog.open()
            }
            /* 最近工作区菜单按钮，无记录时禁用。 */
            AppButton {
                uiTheme: sidebar.uiTheme
                text: qsTr("最近")
                quiet: true
                enabled: workspaceCatalog.recentWorkspaces.length > 0
                /*
                 * 功能：展开最近工作区菜单。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无记录时入口禁用，组件错误由 Qt 报告。
                 * 副作用：打开 recentMenu，不立刻切换数据库。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: recentMenu.open()
            }
            /* 最近工作区弹出菜单，读取真实目录历史，不生成默认数据库记录。 */
            Menu {
                id: recentMenu
                palette.window: sidebar.uiTheme.surface
                palette.text: sidebar.uiTheme.text
                /* 最近菜单主题背景。 */
                background: Rectangle { color: sidebar.uiTheme.surface; border.color: sidebar.uiTheme.border; radius: 6 }
                /* 最近工作区菜单行重复器，随目录列表更新，菜单拥有生成项。 */
                Repeater {
                    model: workspaceCatalog.recentWorkspaces
                    /* 最近工作区菜单项，显示用户名称，按当前模型索引请求切换。 */
                    MenuItem {
                        /* 最近工作区元数据，由重复器注入，无默认值；只读显示用户保存名称。 */
                        required property var modelData
                        /* 最近列表从零索引，由重复器注入；菜单点击交给目录模型切换，非数据库稳定身份。 */
                        required property int index
                        text: modelData.name
                        /*
                         * 功能：按最近菜单当前 index 请求工作区切换。
                         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                         * 返回：无。失败：记录过期、路径或打开失败由目录模型展示。
                         * 副作用：请求目录模型打开已有工作区，不删除历史文件。
                         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                         */
                        onTriggered: workspaceCatalog.switchToRecent(index)
                    }
                }
            }
        }
        /* 侧栏底部主题操作横向布局。 */
        RowLayout {
            Layout.fillWidth: true
            /* 主题切换入口，读取并设置内部 dark/light 标识，显示中文标签。 */
            AppButton {
                id: themeButton
                objectName: "themeButton"
                uiTheme: sidebar.uiTheme
                quiet: true
                text: workspaceCatalog.themeId === "dark" ? qsTr("切换浅色主题") : qsTr("切换深色主题")
                Layout.fillWidth: true
                /*
                 * 功能：按内部 dark/light 标识切换主题，未知当前标识也可显式切到 dark。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：无效设置或持久化失败由目录模型处理。
                 * 副作用：调用 setThemeId 更新主题设置，主题绑定同步刷新，不修改业务数据。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: workspaceCatalog.setThemeId(workspaceCatalog.themeId === "dark" ? "light" : "dark")
            }
        }
    }
}
