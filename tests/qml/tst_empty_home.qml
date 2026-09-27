import QtQuick
import QtTest
import "../../ui/qml"

Item {
    id: root
    width: 900
    height: 640

    UiTheme { id: theme }
    Component { id: homeComponent; HomePage { uiTheme: theme } }

    TestCase {
        name: "EmptyHome"
        when: windowShown

        /** @brief 空白首启时不得在未选择小说的情况下创建世界。 */
        function test_emptyStateCannotCreate() {
            const page = createTemporaryObject(homeComponent, root,
                                               { width: 900, height: 640 })
            verify(!!page, "Component exists")
            const worldName = findChild(page, "newWorldNameField")
            const novel = findChild(page, "selectedNovelField")
            const submit = findChild(page, "createWorldButton")
            verify(!!worldName, "Object exists")
            verify(!!novel, "Object exists")
            verify(!!submit, "Object exists")
            compare(worldName.text, qsTr(""))
            compare(novel.text, qsTr(""))
            compare(submit.enabled, false)
        }

        /** @brief 输入各种名称后，小说未选择时点击仍不能触发创建。 */
        function test_nameEntryStillNeedsNovel() {
            const page = createTemporaryObject(homeComponent, root,
                                               { width: 900, height: 640 })
            verify(!!page, "Component exists")
            const worldName = findChild(page, "newWorldNameField")
            const submit = findChild(page, "createWorldButton")
            verify(!!worldName, "Object exists")
            verify(!!submit, "Object exists")
            worldName.focus = true
            worldName.text = qsTr("测试世界")
            compare(worldName.text, qsTr("测试世界"))
            worldName.text = qsTr("2026")
            compare(worldName.text, qsTr("2026"))
            worldName.text = qsTr("#世界")
            compare(worldName.text, qsTr("#世界"))
            compare(submit.enabled, false)
            mouseClick(submit, submit.width / 2, submit.height / 2)
            tryCompare(workspaceCatalog, "createCount", 0)
        }
    }
}
