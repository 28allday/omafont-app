import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The table view: one row per family with its facts in columns, for scanning
// a library by kind, style count and where it lives.
Rectangle {
    id: listView
    color: Theme.pasteboard

    readonly property var columns: [
        { title: qsTr("Family"), width: 0.30 },
        { title: qsTr("Styles"), width: 0.07 },
        { title: qsTr("Kind"), width: 0.13 },
        { title: qsTr("Foundry"), width: 0.11 },
        { title: qsTr("Where"), width: 0.09 },
        { title: qsTr("State"), width: 0.13 },
        { title: qsTr("Tags"), width: 0.17 }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.hRow
            color: Theme.panelBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s4
                anchors.rightMargin: Theme.s4
                spacing: Theme.s2
                Repeater {
                    model: listView.columns
                    Label {
                        required property var modelData
                        Layout.preferredWidth: (listView.width - Theme.s4 * 2) * modelData.width
                        text: modelData.title
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                        font.weight: Theme.wHeading
                        font.letterSpacing: Theme.capsTracking
                        font.capitalization: Font.AllUppercase
                        elide: Text.ElideRight
                    }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }

        ListView {
            id: table
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.families
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            cacheBuffer: Theme.hRow * 20
            delegate: ItemDelegate {
                id: row
                required property int index
                required property string name
                required property bool user
                required property bool mono
                required property bool latin
                required property bool inactive
                required property bool duplicate
                required property bool variable
                required property string format
                required property string previewFile
                required property int styleCount
                required property var faces
                required property var tags
                required property bool starred
                width: table.width
                height: Theme.hRow
                highlighted: name === backend.selectedName
                leftPadding: Theme.s4
                rightPadding: Theme.s4
                onClicked: { if (backend.staged || backend.staging) backend.clearStage(); backend.selectedName = name }
                onDoubleClicked: { backend.selectedName = name; backend.view = "specimen" }

                Loader {
                    id: face
                    active: row.latin && row.previewFile !== ""
                    sourceComponent: FontLoader { source: backend.fileUrl(row.previewFile) }
                }
                readonly property bool faceReady: face.item !== null && face.item.status === FontLoader.Ready
                readonly property real colWidth: (listView.width - Theme.s4 * 2)
                readonly property string foundry: faces.length > 0 && faces[0].foundry !== "unknown" ? faces[0].foundry : ""

                contentItem: RowLayout {
                    spacing: Theme.s2
                    Text {
                        Layout.preferredWidth: row.colWidth * listView.columns[0].width
                        text: row.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: row.highlighted ? Theme.textPrimary : Theme.textSecondary
                        font.family: row.faceReady ? face.item.name : Theme.fontFamily
                        font.pixelSize: row.faceReady ? Theme.fsRailName : Theme.fsControl
                        opacity: row.inactive ? 0.5 : 1
                    }
                    Label { Layout.preferredWidth: row.colWidth * listView.columns[1].width; text: row.styleCount; color: Theme.textSecondary; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel }
                    Label { Layout.preferredWidth: row.colWidth * listView.columns[2].width; text: row.format.replace(/CFF/g, "OpenType PS") + (row.variable ? qsTr(" · variable") : ""); color: Theme.textSecondary; font.pixelSize: Theme.fsLabel; elide: Text.ElideRight }
                    Label { Layout.preferredWidth: row.colWidth * listView.columns[3].width; text: row.foundry; color: Theme.textSecondary; font.pixelSize: Theme.fsLabel; elide: Text.ElideRight }
                    Label { Layout.preferredWidth: row.colWidth * listView.columns[4].width; text: row.user ? qsTr("Yours") : qsTr("System"); color: row.user ? Theme.accent : Theme.textMuted; font.pixelSize: Theme.fsLabel }
                    Label {
                        Layout.preferredWidth: row.colWidth * listView.columns[5].width
                        text: [row.inactive ? qsTr("Deactivated") : "", row.duplicate ? qsTr("Duplicate") : "", row.mono ? qsTr("Monospace") : "", row.starred ? "★" : ""].filter(s => s !== "").join(" · ")
                        color: row.inactive ? Theme.warning : Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.preferredWidth: row.colWidth * listView.columns[6].width
                        text: row.tags.map(t => "#" + t).join(" ")
                        color: Theme.accent
                        font.pixelSize: Theme.fsLabel
                        elide: Text.ElideRight
                    }
                }
                background: Rectangle {
                    color: row.down ? Theme.pressedOn(Theme.controlHover)
                         : row.highlighted ? Theme.withAlpha(Theme.accent, 0.10)
                         : row.hovered ? Theme.controlHover : (row.index % 2 === 1 ? Theme.withAlpha(Theme.panelBg, 0.5) : "transparent")
                }
            }
            Label {
                anchors.centerIn: parent
                visible: table.count === 0
                text: backend.scanning ? qsTr("Reading your fonts…") : qsTr("Nothing to show here")
                color: Theme.textMuted
            }
        }
    }
}
