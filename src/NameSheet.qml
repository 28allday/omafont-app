import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// One line of text: a collection's name, new or renamed.
Sheet {
    id: sheet
    objectName: "nameSheet"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.s5 * 2, 420)
    modal: true
    standardButtons: Dialog.Save | Dialog.Cancel

    property string targetId: ""
    signal named(string id, string name)

    function openNew(what) { targetId = ""; title = what; field.text = ""; open(); field.forceActiveFocus() }
    function openRename(id, current) { targetId = id; title = qsTr("Rename"); field.text = current; open(); field.forceActiveFocus(); field.selectAll() }
    onAccepted: if (field.text.trim() !== "") named(targetId, field.text.trim())

    contentItem: ColumnLayout {
        TextField {
            id: field
            objectName: "nameField"
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            onAccepted: sheet.accept()
        }
    }
}
