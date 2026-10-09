import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The hotkey sheet, on `?`.
Sheet {
    id: sheet
    objectName: "helpSheet"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.s5 * 2, 480)
    modal: true
    title: qsTr("Keyboard shortcuts")
    standardButtons: Dialog.Close

    readonly property var keys: [
        ["Ctrl+O", qsTr("Install fonts from a file, folder or archive")],
        ["Enter", qsTr("Install what is staged")],
        ["Esc", qsTr("Clear the stage, then the filter")],
        ["/", qsTr("Filter the families")],
        ["↑ ↓", qsTr("Move through the list")],
        ["Delete", qsTr("Remove the selected family (yours only)")],
        ["Ctrl+T", qsTr("Set the selected monospace family as the terminal font")],
        ["Ctrl+S", qsTr("Star or unstar the selected family")],
        ["Ctrl+D", qsTr("Deactivate or activate the selected family")],
        ["Ctrl+E", qsTr("Export the selected family's files")],
        ["Ctrl+Shift+V", qsTr("Validate the selected family")],
        ["Ctrl+P", qsTr("Save a specimen PDF")],
        ["Ctrl+1 … 5", qsTr("Specimen, Grid, List, Repertoire, Compare")],
        ["Ctrl+M", qsTr("Add the selected family to Compare, or take it out")],
        ["Ctrl+Shift+T", qsTr("Tidy up: twins, broken files, strays")],
        ["Ctrl + / −", qsTr("Bigger or smaller type")],
        ["Ctrl+B", qsTr("Show or hide the sidebar")],
        ["Ctrl+L", qsTr("Show or hide the language-coverage fonts")],
        ["Ctrl+R", qsTr("Rescan")],
        ["?", qsTr("This sheet")],
        ["Ctrl+Q", qsTr("Quit")]
    ]

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.s4
        rowSpacing: Theme.s1
        Repeater {
            model: sheet.keys
            delegate: Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 0
                text: modelData[0]
                font.family: Theme.monoFamily
                color: Theme.accent
            }
        }
        Repeater {
            model: sheet.keys
            delegate: Label {
                required property var modelData
                required property int index
                Layout.row: index
                Layout.column: 1
                Layout.fillWidth: true
                text: modelData[1]
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }
}
