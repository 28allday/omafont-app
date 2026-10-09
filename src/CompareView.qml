import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// Up to six families side by side: the same sample line in each, at the
// specimen size, so a choice can be made by looking rather than remembering.
Rectangle {
    id: compare
    color: Theme.pasteboard

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s4
            implicitHeight: Theme.hNavHeader
            spacing: Theme.s2
            Label { text: qsTr("Compare"); font.weight: Theme.wHeading; color: Theme.textPrimary }
            Label { text: qsTr("%1 of 6").arg(backend.compareList.length); color: Theme.textMuted; font.pixelSize: Theme.fsLabel }
            Item { Layout.fillWidth: true }
            Button {
                flat: true
                icon.name: "plus"
                text: qsTr("Add %1").arg(backend.selectedName)
                visible: backend.hasSelection && backend.compareList.indexOf(backend.selectedName) < 0
                onClicked: backend.toggleCompare(backend.selectedName)
            }
            Button { flat: true; icon.name: "x"; text: qsTr("Clear"); visible: backend.compareList.length > 0; onClicked: backend.clearCompare() }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        TextField {
            id: sampleField
            Layout.fillWidth: true
            Layout.margins: Theme.s4
            Layout.bottomMargin: Theme.s2
            text: backend.sample
            placeholderText: qsTr("Type a sample…")
            onTextEdited: backend.sample = text
        }

        ListView {
            id: rows
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s4
            clip: true
            model: backend.compareRows
            spacing: Theme.s2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: body.implicitHeight + Theme.s3 * 2
                radius: Theme.rCard
                color: modelData.name === backend.selectedName ? Theme.withAlpha(Theme.accent, 0.08) : Theme.panelBg
                border.width: Theme.hairline
                border.color: modelData.name === backend.selectedName ? Theme.accent : Theme.border
                Loader {
                    id: face
                    active: (modelData.file ?? "") !== ""
                    sourceComponent: FontLoader { source: backend.fileUrl(modelData.file) }
                }
                readonly property bool faceReady: face.item !== null && face.item.status === FontLoader.Ready
                TapHandler { onTapped: backend.selectedName = modelData.name }
                ColumnLayout {
                    id: body
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.s3
                    spacing: Theme.s1
                    RowLayout {
                        spacing: Theme.s2
                        Label { text: modelData.name; font.weight: Theme.wHeading; color: Theme.textPrimary }
                        Label {
                            text: [Theme.plural(modelData.styles, qsTr("style"), qsTr("styles")), modelData.classification !== "" && modelData.classification !== "Unknown" ? modelData.classification : "", modelData.designer].filter(s => s !== "").join("  ·  ")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsLabel
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        ToolButton { icon.name: "x"; onClicked: backend.toggleCompare(modelData.name); ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: qsTr("Take out of the comparison") }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.sample === "" ? backend.defaultSample : backend.sample
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.family: row.faceReady ? face.item.name : Theme.fontFamily
                        font.pixelSize: Math.round(34 * backend.specimenScale)
                        color: Theme.specimenInk
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.sample === "" ? backend.defaultSample : backend.sample
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.family: row.faceReady ? face.item.name : Theme.fontFamily
                        font.pixelSize: Math.round(14 * backend.specimenScale)
                        color: Theme.specimenInk
                    }
                }
            }
        }
        Column {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: backend.compareList.length === 0
            spacing: Theme.s2
            topPadding: Theme.s6
            Icon { name: "columns-2"; size: Theme.szIconHero; color: Theme.textMuted; anchors.horizontalCenter: parent.horizontalCenter }
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Nothing to compare yet"); color: Theme.textPrimary; font.pixelSize: Theme.fsTitle; font.weight: Theme.wHeading }
            Label { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Pick a family and press Ctrl+M, or use the compare button in the Info pane. Up to six."); color: Theme.textMuted }
        }
    }
}
