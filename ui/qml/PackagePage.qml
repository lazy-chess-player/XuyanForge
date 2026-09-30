import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 包与备份页：GUI 线程分派用户明确的文件操作，包校验与数据库生命周期交给外部模型。 */
Item {
    id: page
    /* 外部共享主题观察引用，无默认值，页面与各选择框只读取，窗口负责其寿命。 */
    required property var uiTheme
    /* 等待替换确认的目标URL，默认空；文件选择写入，确认/取消后清空，不代表已授权导出。 */
    property url pendingExportFile: ""
    /* 功能：分派用户已选导出路径；目标存在时先显示中文替换确认。
     * 参数：target为完整本地URL，由文件选择框提供；仅在待确认期间按值持有。
     * 返回：无。失败：文件元数据或导出错误由包模型处理，路径不在页面校验。
     * 副作用：不存在时请求导出，存在时只存确认快照并打开弹窗；GUI线程同步调用。 */
    function requestExport(target) {
        if (packages.destinationExists(target)) {
            page.pendingExportFile = target
            overwriteConfirm.open()
        } else packages.exportWorld(target)
    }
    /* 世界包导入文件框，仅确认后导入选择地址。 */
    FileDialog {
        id: importDialog
        objectName: "importDialog"
        options: FileDialog.DontUseNativeDialog
        title: qsTr("导入世界包")
        nameFilters: [qsTr("世界包 (*.zip)"), qsTr("所有文件 (*)")]
        acceptLabel: qsTr("导入")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.OpenFile
        /*
         * 功能：确认导入 selectedFile 的世界包。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：包损坏、字段或文件错误由 packages 报告。
         * 副作用：模型执行包导入；本回调不读取密钥或拼接世界资料。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: packages.importWorld(selectedFile)
    }
    /* 世界包导出保存框，完整输出 URL 交给模型处理。 */
    FileDialog {
        id: exportDialog
        objectName: "exportDialog"
        // Basic框架内置覆盖正文没有翻译接口，交给本页中文确认，保留替换前确认行为。
        options: FileDialog.DontUseNativeDialog | FileDialog.DontConfirmOverwrite
        title: qsTr("导出世界包")
        acceptLabel: qsTr("保存")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("世界包 (*.zip)")]
        /*
         * 功能：确认将当前世界导出到 selectedFile。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：目标地址或包写入失败由 packages 报告。
         * 副作用：只读目标是否存在；已有文件先中文确认，未确认零导出；不添加小说正文或凭据。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: page.requestExport(selectedFile)
    }
    /* 中文替换确认：页面拥有，保存框接受不等于同意覆盖；取消不写出目标文件。 */
    Dialog {
        id: overwriteConfirm
        objectName: "overwriteConfirm"
        title: qsTr("确认替换文件")
        width: Math.min(480, Math.max(320, page.width - 40))
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        /* 功能：为替换/返回动作安装可重译中文标签。参数：无。返回：无。
         * 失败：标准按钮缺失由QML报告；副作用：只绑定按钮文字，不执行导出，GUI线程随弹窗存活。 */
        Component.onCompleted: {
            /* 功能：提供明确替换动作中文。参数：无。返回：中文标签。
             * 失败：分配异常交Qt；副作用：仅读取翻译环境，绑定由按钮持有。 */
            standardButton(Dialog.Yes).text = Qt.binding(function() { return qsTr("替换") })
            /* 功能：提供返回动作中文。参数：无。返回：中文标签。
             * 失败：分配异常交Qt；副作用：仅读取翻译环境，绑定由按钮持有。 */
            standardButton(Dialog.No).text = Qt.binding(function() { return qsTr("返回") })
        }
        /* 真实目标URL保持原值，产品提示为中文；不把路径当词条翻译。 */
        contentItem: Label { text: qsTr("目标文件已存在，是否替换？\n%1").arg(page.pendingExportFile.toString()); wrapMode: Text.Wrap; width: overwriteConfirm.availableWidth }
        /* 功能：接受替换后消费本次目标快照。参数：无。返回：无。
         * 失败：写入错误由包模型报告；副作用：清空待确认值并请求导出，只有此动作授权覆盖。 */
        onAccepted: { const target = page.pendingExportFile; page.pendingExportFile = ""; packages.exportWorld(target) }
        /* 功能：返回、退出键或其他关闭后清理目标。参数：无。返回：无。
         * 失败：赋值异常交Qt；副作用：仅清空快照，未接受时零导出，GUI线程执行。 */
        onClosed: page.pendingExportFile = ""
    }
    /* 备份目标目录框，明确确认后才创建备份。 */
    FolderDialog {
        id: backupDialog
        objectName: "backupDialog"
        options: FolderDialog.DontUseNativeDialog
        title: qsTr("选择备份保存位置")
        acceptLabel: qsTr("选择")
        rejectLabel: qsTr("取消")
        /*
         * 功能：确认向 selectedFolder 创建备份。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：目标路径、权限或复制失败由 packages 报告。
         * 副作用：请求模型写备份，实际文件边界由服务校验。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: packages.createBackup(selectedFolder)
    }
    /* 恢复源目录框，明确确认后交给模型恢复工作区。 */
    FolderDialog {
        id: restoreDialog
        objectName: "restoreDialog"
        options: FolderDialog.DontUseNativeDialog
        title: qsTr("选择备份目录")
        acceptLabel: qsTr("选择")
        rejectLabel: qsTr("取消")
        /*
         * 功能：确认从 selectedFolder 恢复备份。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：无效备份、校验或恢复失败由 packages 报告。
         * 副作用：模型负责工作区恢复和相关文件生命周期；本回调不绕过校验直接覆盖文件。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: packages.restoreBackup(selectedFolder)
    }
    /* 包、备份和结果反馈纵向布局。 */
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16
        /* 包与备份工具页标题。 */
        Label { text: qsTr("包与备份"); color: page.uiTheme.text; font.pointSize: 16; font.weight: Font.DemiBold }
        /* 文件操作与凭据排除说明，不把密钥写入导出数据。 */
        Label { text: qsTr("导入或导出世界包；备份前请确认目标位置。密钥不会写入包。")
                color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 世界包导入和导出入口横向布局。 */
        RowLayout {
            /* 导入入口，仅打开选择框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("导入世界包");
                /*
                 * 功能：显示世界包导入文件框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不导入，组件错误由 Qt 报告。
                 * 副作用：打开 importDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: importDialog.open() }
            /* 导出入口，仅打开保存框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("导出世界包");
                /*
                 * 功能：显示世界包导出保存框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不导出，组件错误由 Qt 报告。
                 * 副作用：打开 exportDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: exportDialog.open() }
        }
        /* 备份创建与恢复入口横向布局。 */
        RowLayout {
            /* 创建备份入口，仅打开目标目录选择框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("创建备份");
                /*
                 * 功能：显示备份保存位置选择框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不创建备份，组件错误由 Qt 报告。
                 * 副作用：打开 backupDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: backupDialog.open() }
            /* 恢复入口，仅打开备份源目录选择框。 */
            AppButton { uiTheme: page.uiTheme; text: qsTr("恢复备份");
                /*
                 * 功能：显示恢复源目录选择框。
                 * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
                 * 返回：无。失败：取消或未确认时不恢复，组件错误由 Qt 报告。
                 * 副作用：打开 restoreDialog。
                 * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
                 */
                onClicked: restoreDialog.open() }
        }
        /* 文件操作反馈，模型错误优先显示。 */
        Label { text: packages.errorText.length > 0 ? packages.errorText : packages.statusText;
                color: packages.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted;
                wrapMode: Text.Wrap; Layout.fillWidth: true }
        /* 底部弹性留白，保留工具页现有位置。 */
        Item { Layout.fillHeight: true }
    }
}
