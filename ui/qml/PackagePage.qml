import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

/* 包与备份页：GUI 线程分派用户明确的文件操作，包校验与数据库生命周期交给外部模型。 */
Item {
    id: page
    /* 外部共享主题观察引用，无默认值，页面与各选择框只读取，窗口负责其寿命。 */
    required property var uiTheme
    /* 世界包导入文件框，仅确认后导入选择地址。 */
    FileDialog {
        id: importDialog
        options: FileDialog.DontUseNativeDialog
        title: qsTr("导入世界包")
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
        options: FileDialog.DontUseNativeDialog
        title: qsTr("导出世界包")
        acceptLabel: qsTr("保存")
        rejectLabel: qsTr("取消")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("世界包 (*.zip)")]
        /*
         * 功能：确认将当前世界导出到 selectedFile。
         * 参数：无；动作使用所在控件与模型的当前属性，不接收独立输入参数。
         * 返回：无。失败：目标地址或包写入失败由 packages 报告。
         * 副作用：请求模型创建导出文件，不由回调添加小说正文或凭据。
         * 线程与生命周期：GUI 线程同步分派；回调及控件随父对象存活，模型负责其自身持久化或后台操作。
         */
        onAccepted: packages.exportWorld(selectedFile)
    }
    /* 备份目标目录框，明确确认后才创建备份。 */
    FolderDialog {
        id: backupDialog
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
