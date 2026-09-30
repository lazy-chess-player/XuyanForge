import QtQuick
import QtTest

/* 核对与正式启动相同的翻译链，防止Qt内部动作回落英文；不接触用户文件。 */
Item {
    id: root
    width: 400
    height: 200
    TestCase {
        name: "FrameworkChineseOptions"
        when: windowShown
        /* 功能：给出实际Qt文件框与文本菜单使用的上下文词条。
         * 参数：无。返回：中文预期数据；失败：分配异常由框架报告。
         * 副作用：只创建测试预期值，不改变产品语言或资源。 */
        function test_labels_data() {
            return [
                {tag: "undo", context: "UndoAction", source: "Undo", label: qsTr("撤销")},
                {tag: "redo", context: "RedoAction", source: "Redo", label: qsTr("重做")},
                {tag: "cut", context: "CutAction", source: "Cut", label: qsTr("剪切")},
                {tag: "copy", context: "CopyAction", source: "Copy", label: qsTr("复制")},
                {tag: "paste", context: "PasteAction", source: "Paste", label: qsTr("粘贴")},
                {tag: "delete", context: "DeleteAction", source: "Delete", label: qsTr("删除")},
                {tag: "all", context: "SelectAllAction", source: "Select All", label: qsTr("全选")},
                {tag: "file", context: "FileDialog", source: "File name", label: qsTr("文件名")},
                {tag: "filter", context: "FileDialog", source: "Filter", label: qsTr("文件类型")},
                {tag: "overwrite", context: "FileDialog", source: "Overwrite file?", label: qsTr("确认替换文件")},
                {tag: "favorite", context: "SideBar", source: "Add Favorite", label: qsTr("添加收藏")}
            ]
        }
        /* 功能：通过Qt真实翻译链读取词条，中文显示须匹配而不是回落英文。
         * 参数：data为上下文、源文和中文预期，只读至返回。
         * 返回：无；失败：缺失、空或英文回退时断言失败。
         * 副作用：只读取翻译器，不打开菜单或写文件；GUI线程同步完成。 */
        function test_labels(data) { compare(qsTranslate(data.context, data.source), data.label) }
    }
}
