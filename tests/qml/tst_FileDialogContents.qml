import QtQuick
import QtTest
import QtQuick.Controls
import "../../ui/qml"

/* 文件框内部中文交互宿主：实际展开筛选弹层，不接受路径、联网或写入工作区。 */
Item {
    id: root
    width: 1000
    height: 800
    /* 宿主拥有的测试主题，默认深色，清理时恢复；不写生产设置。 */
    UiTheme {
        id: theme
    }
    /* 新建世界页面工厂，使用测试入口的空目录替身，临时实例由 Qt Test 释放。 */
    Component {
        id: homeComponent
        HomePage {
            uiTheme: theme
            width: 1000
            height: 800
        }
    }
    /* 小说章节页面工厂，使用空来源替身，不读取真实小说。 */
    Component {
        id: sourceComponent
        SourcePage {
            uiTheme: theme
            width: 1000
            height: 800
        }
    }
    /* 工作区页面工厂，不调用数据库创建或打开。 */
    Component {
        id: workspaceComponent
        WorkspacePage {
            uiTheme: theme
            width: 1000
            height: 800
        }
    }
    /* 项目侧栏工厂，无最近工作区，不导航到用户资料。 */
    Component {
        id: sidebarComponent
        ProjectSidebar {
            uiTheme: theme
            activePage: 0
            width: 280
            height: 800
        }
    }
    /* 包和备份页面工厂，替身只记内存调用计数，不写文件。 */
    Component {
        id: packageComponent
        PackagePage {
            uiTheme: theme
            width: 1000
            height: 800
        }
    }
    /* 文件框测试，探针仅观察该测试进程，不依赖用户工作区。 */
    TestCase {
        name: "FileDialogContentsChinese"
        when: windowShown
        /* 功能：提供八个文件/目录入口的深浅主题组合。参数：无。返回：十六项输入。
         * 失败：分配异常由框架报告。副作用：无，只构造测试输入。 */
        function test_filterPopup_data() {
            const novel = [qsTr("小说文本 (*.txt *.md *.markdown)"), qsTr("所有文件 (*)")];
            const workspace = [qsTr("工作区数据库 (*.sqlite *.db)")];
            const rows = [
                {
                    key: "home",
                    factory: homeComponent,
                    dialog: "novelDialog",
                    labels: novel
                },
                {
                    key: "source",
                    factory: sourceComponent,
                    dialog: "importDialog",
                    labels: novel
                },
                {
                    key: "workspace",
                    factory: workspaceComponent,
                    dialog: "openDialog",
                    labels: workspace
                },
                {
                    key: "sidebar",
                    factory: sidebarComponent,
                    dialog: "workspaceDialog",
                    labels: workspace
                },
                {
                    key: "package-import",
                    factory: packageComponent,
                    dialog: "importDialog",
                    labels: [qsTr("世界包 (*.zip)"), qsTr("所有文件 (*)")]
                },
                {
                    key: "package-export",
                    factory: packageComponent,
                    dialog: "exportDialog",
                    labels: [qsTr("世界包 (*.zip)")]
                },
                {
                    key: "backup",
                    factory: packageComponent,
                    dialog: "backupDialog",
                    labels: []
                },
                {
                    key: "restore",
                    factory: packageComponent,
                    dialog: "restoreDialog",
                    labels: []
                }
            ];
            const result = [];
            for (const mode of ["dark", "light"])
                for (const row of rows)
                    result.push({
                        tag: row.key + "-" + mode,
                        mode: mode,
                        factory: row.factory,
                        dialog: row.dialog,
                        labels: row.labels
                    });
            return result;
        }
        /*
         * 功能：打开真实文件/目录框，核对内置字段和按钮，并逐项点击筛选弹层；实际点击取消退出。
         * 参数：data 为本次借用的工厂、框名称、预期筛选及主题；mode 取 dark/light。
         * 返回：无；每项代理文字、选中结果与关闭状态均通过才通过。
         * 失败：框架弹层定位失败、筛选控件数量不符或任何选项回落英文时断言失败，不跳过。
         * 副作用：只读取测试进程拥有的空临时目录、投递鼠标事件、改变筛选并取消；不接受路径。
         * 线程与生命周期：GUI 线程同步测试，控件指针只使用到测试页面销毁之前。
         */
        function test_filterPopup(data) {
            theme.mode = data.mode;
            const page = createTemporaryObject(data.factory, root);
            verify(!!page, "Component exists");
            const dialog = findChild(page, data.dialog);
            verify(!!dialog, "Object exists");
            dialog.currentFolder = optionTestFolder;
            dialog.open();
            try {
                tryCompare(dialog, "visible", true);
                tryVerify(function () {
                    return frameworkOptions.activeDialog() !== null;
                });
                const actual = frameworkOptions.activeDialog();
                verify(!!actual, "Object exists");
                const choices = frameworkOptions.controls(actual, "QQuickComboBox");
                const labels = data.labels;
                compare(choices.length, labels.length === 0 ? 0 : 1);
                const titleLabels = frameworkOptions.controls(actual, "QQuickLabel");
                const allowed = [dialog.title, qsTr("文件名"), qsTr("文件类型")];
                verify(titleLabels.length > 0);
                const unexpected = [];
                for (const label of titleLabels)
                    if (label.visible && label.text.length > 0 && allowed.indexOf(label.text) < 0)
                        unexpected.push(label.text);
                compare(unexpected, []);
                // Qt 文件框还拥有隐藏的覆盖确认按钮区；只验收当前可见框的实际动作。
                const boxes = frameworkOptions.controls(actual, "QQuickDialogButtonBox").filter(function (box) {
                    return box.visible;
                });
                compare(boxes.length, 1);
                const cancel = boxes[0].standardButton(DialogButtonBox.Cancel);
                verify(!!cancel, "Object exists");
                compare(cancel.text, qsTr("取消"));
                compare(boxes[0].count, 2);
                // Qt 版本可让保存框沿用 Open 角色再设置 acceptLabel；验收实际按钮文字而不猜测角色。
                const accept = boxes[0].standardButton(DialogButtonBox.Save) || boxes[0].standardButton(DialogButtonBox.Open);
                verify(!!accept, "Object exists");
                compare(accept.text, dialog.acceptLabel);
                const choice = choices.length === 0 ? null : choices[0];
                if (choice !== null)
                    compare(choice.count, labels.length);
                for (let ordinal = 0; ordinal < labels.length; ++ordinal) {
                    mouseClick(choice, choice.width / 2, choice.height / 2);
                    tryCompare(choice.popup, "visible", true);
                    const list = choice.popup.contentItem;
                    list.positionViewAtIndex(ordinal, ListView.Center);
                    tryVerify(function () {
                        return list.itemAtIndex(ordinal) !== null;
                    });
                    const row = list.itemAtIndex(ordinal);
                    verify(!!row, "Object exists");
                    tryCompare(row, "text", labels[ordinal]);
                    mouseClick(row, row.width / 2, row.height / 2);
                    tryCompare(choice, "displayText", labels[ordinal]);
                    tryCompare(choice.popup, "visible", false);
                }
                mouseClick(cancel, cancel.width / 2, cancel.height / 2);
                tryCompare(dialog, "visible", false);
            } finally {
                // 即使断言失败也关闭测试框，避免残留模态窗口使后续用例定位到多个框。
                if (dialog.visible)
                    dialog.reject();
            }
        }
        /* 功能：恢复内存主题。参数：无。返回：无。
         * 失败：赋值错误由框架报告。副作用：只恢复测试状态，页面由临时对象机制释放。 */
        function cleanup() {
            theme.mode = "dark";
        }
    }
}
