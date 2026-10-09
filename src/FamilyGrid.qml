import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// Every visible family as a card: a big "Ag" in its own face, the name under
// it. The grid view, for finding a font by how it looks rather than by name.
Rectangle {
    id: gridView
    color: Theme.pasteboard

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: Theme.s4
        clip: true
        model: backend.families
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / (200 * backend.specimenScale))))
        cellHeight: Math.round(150 * backend.specimenScale)
        cacheBuffer: cellHeight * 4
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        currentIndex: backend.families.indexOf(backend.selectedName)

        delegate: Item {
            id: cell
            required property int index
            required property string name
            required property bool latin
            required property bool inactive
            required property bool mono
            required property string previewFile
            required property int styleCount
            required property bool starred
            width: grid.cellWidth
            height: grid.cellHeight

            Loader {
                id: face
                active: cell.previewFile !== ""
                sourceComponent: FontLoader { source: backend.fileUrl(cell.previewFile) }
            }
            readonly property bool faceReady: face.item !== null && face.item.status === FontLoader.Ready
            readonly property bool selected: cell.name === backend.selectedName

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.s1
                radius: Theme.rCard
                color: cell.selected ? Theme.withAlpha(Theme.accent, 0.10) : hover.hovered ? Theme.controlHover : Theme.panelBg
                border.width: cell.selected ? Theme.selectionRing : Theme.hairline
                border.color: cell.selected ? Theme.accent : Theme.border
                opacity: cell.inactive ? 0.45 : 1
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: { if (backend.staged || backend.staging) backend.clearStage(); backend.selectedName = cell.name }
                    onDoubleTapped: { backend.selectedName = cell.name; backend.view = "specimen" }
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.s3
                    spacing: Theme.s1
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: cell.latin ? "Ag" : cell.name.substring(0, 2)
                        textFormat: Text.PlainText
                        font.family: cell.faceReady ? face.item.name : Theme.fontFamily
                        font.pixelSize: Math.round(56 * backend.specimenScale)
                        color: Theme.specimenInk
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Text {
                            Layout.fillWidth: true
                            text: cell.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: cell.selected ? Theme.textPrimary : Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fsLabel
                        }
                        Icon { visible: cell.starred; name: "star"; size: Theme.szIcon - 4; color: Theme.warning }
                        Label { text: cell.styleCount; color: Theme.textMuted; font.pixelSize: Theme.fsCaption; font.family: Theme.monoFamily }
                        Label { visible: cell.mono; text: "M"; color: Theme.accent; font.pixelSize: Theme.fsCaption; font.family: Theme.monoFamily; font.weight: Theme.wHeading }
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: grid.count === 0
        text: backend.scanning ? qsTr("Reading your fonts…") : qsTr("Nothing to show here")
        color: Theme.textMuted
    }
}
