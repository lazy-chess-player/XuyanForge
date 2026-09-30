import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 工作区管理页：GUI 线程观察目录模型，显式打开或新建工作区，默认不创建资料。 */
Item {
    id: page
    /* 外部共享主题观察引用，无默认值；窗口须保证页面存活期间引用有效。 */
    required property var uiTheme
    /* 工作区文件选择框，仅确认后请求模型打开完整 URL。 */
    FileDialog {
        // 使用 Qt 中文翻译的文件框，避免系统语言改变内置选项。
        options: FileDialog.DontUseNativeDialog
        id: openDialog
        objectName: "openDialog"
        title: qsTr("打开工作区")
        acceptLabel: qsTr("打开")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("工作区数据库 (*.sqlite *.db)")]
        /*
         * 功能：确认打开选中的工作区，将 selectedFile 的完整 URL 交给目录模型。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：地址无效或打开失败由目录模型报告中文错误，原文件由模型保护。
         * 副作用：请求工作区切换，不自行复制或删除数据库。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: workspaceCatalog.openWorkspace(selectedFile)
    }
    /* 路径、最近记录及动作纵向布局。 */
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16
        /* 工作区管理标题。 */
        Label { text: qsTr("工作区管理"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        /* 当前数据库路径说明，真实用户路径保留原值。 */
        Label { text: qsTr("当前工作区：%1").arg(workspaceCatalog.currentPath); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 打开与新建入口横向布局。 */
        RowLayout {
            /* 打开入口，点击只打开文件框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("打开工作区");
                /*
                 * 功能：显示工作区选择框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：对话框无法打开时由 Qt 报告，未确认前不切换。
                 * 副作用：打开页面拥有的 openDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: openDialog.open() }
            /* 新建入口，点击只打开命名弹窗。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("新建工作区");
                /*
                 * 功能：显示工作区命名弹窗。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：组件初始化问题由 Qt 报告，未确认不创建。
                 * 副作用：打开 createDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: createDialog.open() }
        }
        /* 最近打开分区标题。 */
        Label { text: qsTr("最近打开"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
        /* 无最近记录提示，不生成虚假历史。 */
        Label { visible: workspaceCatalog.recentWorkspaces.length === 0; text: qsTr("暂无最近工作区"); color: page.uiTheme.muted }
        /* 最近记录重复器，读取目录模型列表，子行由重复器拥有。 */
        Repeater {
            model: workspaceCatalog.recentWorkspaces
            /* 最近工作区行，包含用户名称及打开、移除动作。 */
            RowLayout {
                /* 最近工作区行，由重复器注入，无默认值；仅观察名称和历史元数据，不拥有数据库。 */
                required property var modelData
                /* 最近记录从零索引，由重复器注入；打开与移除动作读取，不表示文件编号。 */
                required property int index
                Layout.fillWidth: true
                /* 最近记录名称，保留用户自定义名称。 */
                Label { text: modelData.name; color: page.uiTheme.text; Layout.fillWidth: true }
                /* 打开最近记录入口，按当前索引请求模型切换。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("打开");
                    /*
                     * 功能：按本行 index 打开最近工作区。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：索引过期、文件缺失或打开失败由目录模型报告。
                     * 副作用：请求模型切换数据库，不直接读写文件。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: workspaceCatalog.switchToRecent(index) }
                /* 移除历史入口，仅从最近列表移除，不删除用户数据库。 */
                AppButton { uiTheme: page.uiTheme; text: qsTr("从列表移除");
                    /*
                     * 功能：按本行 index 从最近列表移除记录。
                     * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                     * 返回：无。失败：索引无效由目录模型处理。
                     * 副作用：修改最近记录，不删除用户工作区数据库。
                     * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                     */
                    onClicked: workspaceCatalog.forgetRecent(index) }
            }
        }
        /* 底部弹性留白，将管理内容保持在页面上部。 */
        Item { Layout.fillHeight: true }
    }
    /* 新建工作区命名弹窗，由页面拥有，只有确认动作才交给模型创建。 */
    Dialog {
        id: createDialog
        objectName: "createDialog"
        title: qsTr("新建工作区")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        /* 默认按钮也显式使用中文，不受 Qt 框架翻译资源或操作系统语言影响。 */
        /*
         * 功能：在新建弹窗创建完成时将两个标准动作显式设为中文。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：标准按钮未生成时访问错误由 QML 报告；本弹窗声明两个标准动作后初始化。
         * 副作用：设置确定及取消按钮文字，不创建工作区。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        Component.onCompleted: {
            /* 功能：为确定按钮提供可重译的中文标签。参数：无。返回：翻译后的动作文字。
             * 失败：词条未翻译时使用中文源文。副作用：只读语言环境；绑定由按钮持有，在 GUI 线程重算，随按钮销毁释放。 */
            standardButton(Dialog.Ok).text = Qt.binding(function() { return qsTr("确定") })
            /* 功能：为取消按钮提供可重译的中文标签。参数：无。返回：翻译后的动作文字。
             * 失败：词条未翻译时使用中文源文。副作用：只读语言环境；绑定由按钮持有，在 GUI 线程重算，随按钮销毁释放。 */
            standardButton(Dialog.Cancel).text = Qt.binding(function() { return qsTr("取消") })
        }
        /* 名称输入，初始空，宽度 320 像素，确认时读取，不生成默认样例。 */
        contentItem: AppField { id: workspaceName; uiTheme: page.uiTheme; placeholderText: qsTr("工作区名称"); implicitWidth: 320 }
        /*
         * 功能：确认创建用户命名的工作区，读取 workspaceName.text。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：名称校验或创建失败由目录模型展示错误。
         * 副作用：请求模型创建工作区，不创建世界、小说或人物。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: workspaceCatalog.createWorkspace(workspaceName.text)
    }
}
