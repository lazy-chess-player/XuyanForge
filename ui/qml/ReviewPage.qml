import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    required property var uiTheme
    signal navigate(int page)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.preferredWidth: 310
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: qsTr("待校对候选"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                Label { visible: candidateReview.candidates.length === 0; text: qsTr("暂无待校对内容。解析完成后会显示在这里。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ListView {
                    id: list
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: candidateReview.candidates
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: list.width
                        text: modelData.name
                        highlighted: candidateReview.selectedIndex === index
                        onClicked: candidateReview.selectCandidate(index)
                    }
                }
            }
        }
        SectionPanel {
            uiTheme: page.uiTheme
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label { text: qsTr("候选详情"); color: page.uiTheme.text; font.pointSize: 12; font.weight: Font.DemiBold }
                Label { text: qsTr("核对原文证据后再接受。被拒绝的候选不会写入世界资料。"); color: page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { text: qsTr("名称"); color: page.uiTheme.muted }
                AppField { id: candidateName; uiTheme: page.uiTheme; Layout.fillWidth: true; text: candidateReview.selectedName }
                Label { text: qsTr("原文证据"); color: page.uiTheme.muted }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        readOnly: true
                        text: candidateReview.selectedQuote
                        wrapMode: TextEdit.Wrap
                        color: page.uiTheme.text
                        background: Rectangle { color: page.uiTheme.surfaceAlt; radius: 6 }
                    }
                }
                Label { text: candidateReview.selectedRange; color: page.uiTheme.muted; font.pointSize: 9 }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("接受")
                        enabled: candidateReview.selectedIndex >= 0
                        onClicked: candidateReview.reviewSelected("accepted", candidateName.text,
                                                                   candidateReview.selectedFields,
                                                                   candidateReview.selectedProvenance)
                    }
                    AppButton {
                        uiTheme: page.uiTheme; text: qsTr("拒绝")
                        enabled: candidateReview.selectedIndex >= 0
                        onClicked: candidateReview.reviewSelected("rejected", candidateName.text,
                                                                   candidateReview.selectedFields,
                                                                   candidateReview.selectedProvenance)
                    }
                    Item { Layout.fillWidth: true }
                    AppButton { uiTheme: page.uiTheme; text: qsTr("查看世界资料"); onClicked: page.navigate(1) }
                }
                Label { text: candidateReview.errorText.length > 0 ? candidateReview.errorText : candidateReview.statusText; color: candidateReview.errorText.length > 0 ? page.uiTheme.danger : page.uiTheme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
