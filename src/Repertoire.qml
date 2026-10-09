import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// Every character the selected face covers, as a grid of glyphs with their
// code points. Loaded from fontconfig's charset for the preview file.
Rectangle {
    id: repertoire
    color: Theme.pasteboard

    readonly property string file: backend.previewFile
    onFileChanged: if (visible) backend.loadRepertoire(file)
    onVisibleChanged: if (visible) backend.loadRepertoire(file)
    Component.onCompleted: if (visible) backend.loadRepertoire(file)

    Loader {
        id: loader
        active: repertoire.file !== ""
        sourceComponent: FontLoader { source: backend.fileUrl(repertoire.file) }
    }
    readonly property bool ready: loader.item !== null && loader.item.status === FontLoader.Ready
    readonly property string family: ready ? loader.item.name : ""
    readonly property int cell: Math.round(72 * backend.specimenScale)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s4
            implicitHeight: Theme.hNavHeader
            spacing: Theme.s2
            Label { text: backend.previewTitle; font.weight: Theme.wHeading; color: Theme.textPrimary; elide: Text.ElideRight; Layout.maximumWidth: 320 }
            Label {
                text: backend.repertoireTotal > backend.repertoire.length
                      ? qsTr("%1 characters, first %2 shown").arg(backend.repertoireTotal).arg(backend.repertoire.length)
                      : Theme.plural(backend.repertoireTotal, qsTr("character"), qsTr("characters"))
                color: Theme.textMuted
                font.pixelSize: Theme.fsLabel
            }
            Item { Layout.fillWidth: true }
            Label { visible: pointed.codepoint >= 0; text: pointed.codepoint >= 0 ? "U+" + pointed.codepoint.toString(16).toUpperCase().padStart(4, "0") : ""; color: Theme.accent; font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        QtObject { id: pointed; property int codepoint: -1 }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.s3
            clip: true
            model: backend.repertoire
            cellWidth: repertoire.cell
            cellHeight: Math.round(repertoire.cell * 1.3)
            cacheBuffer: cellHeight * 3
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: cellItem
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property int cp: Number(modelData)
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Theme.rChip
                    color: hover.hovered ? Theme.controlHover : "transparent"
                    border.width: Theme.hairline
                    border.color: Theme.border
                    HoverHandler { id: hover; onHoveredChanged: pointed.codepoint = hover.hovered ? cellItem.cp : (pointed.codepoint === cellItem.cp ? -1 : pointed.codepoint) }
                    TapHandler { onTapped: { backend.sample = backend.sample + String.fromCodePoint(cellItem.cp) } }
                    Text {
                        anchors.top: parent.top
                        anchors.bottom: code.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        text: String.fromCodePoint(cellItem.cp)
                        textFormat: Text.PlainText
                        font.family: repertoire.family
                        font.pixelSize: Math.round(repertoire.cell * 0.5)
                        color: Theme.specimenInk
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        id: code
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Theme.s1
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cellItem.cp.toString(16).toUpperCase().padStart(4, "0")
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fsCaption
                        color: Theme.textMuted
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: repertoire.file === "" ? qsTr("Choose a family to see its characters") : qsTr("Reading the character set…")
                color: Theme.textMuted
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s4
            Layout.bottomMargin: Theme.s2
            text: qsTr("Click a glyph to add it to the sample text.")
            color: Theme.textMuted
            font.pixelSize: Theme.fsCaption
        }
    }
}
