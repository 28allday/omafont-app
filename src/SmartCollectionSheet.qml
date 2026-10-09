import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// Rules that pick families: <field> <op> <value>, all or any of them.
Sheet {
    id: sheet
    objectName: "smartSheet"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.s5 * 2, 640)
    modal: true
    title: editingId === "" ? qsTr("New smart collection") : qsTr("Edit smart collection")
    standardButtons: Dialog.Save | Dialog.Cancel

    property string editingId: ""
    property var rules: []

    function openNew() {
        editingId = ""
        nameField.text = ""
        matchAll.checked = true
        rules = [{ field: "family", op: "contains", value: "" }]
        open()
        nameField.forceActiveFocus()
    }
    function openEdit(id) {
        const sc = backend.smartCollection(id)
        if (!sc.id) return
        editingId = id
        nameField.text = sc.name
        matchAll.checked = sc.matchAll
        rules = sc.rules.length ? sc.rules : [{ field: "family", op: "contains", value: "" }]
        open()
    }
    function fieldLabel(f) {
        return ({ family: qsTr("Family name"), style: qsTr("Style name"), postscript: qsTr("PostScript name"), fullname: qsTr("Full name"),
                  designer: qsTr("Designer"), manufacturer: qsTr("Manufacturer"), kind: qsTr("Kind"), language: qsTr("Language"),
                  "class": qsTr("Design class"), trait: qsTr("Trait"), location: qsTr("Location"), weight: qsTr("Weight") })[f] ?? f
    }
    function opLabel(o) {
        return ({ contains: qsTr("contains"), begins: qsTr("begins with"), ends: qsTr("ends with"), is: qsTr("is"), isnot: qsTr("is not") })[o] ?? o
    }
    function hintFor(f) {
        return ({ kind: "TrueType · CFF", "class": "Serif · Sans-serif · Handwriting · Ornamental · Symbolic",
                  trait: "Bold · Italic · Monospaced · Condensed · Expanded · Variable · Colour glyphs",
                  location: "Yours · System", weight: "Thin … Regular … Bold … Black", language: qsTr("a language name or code, e.g. German or de") })[f] ?? ""
    }
    onAccepted: {
        const clean = rules.filter(r => r.value.trim() !== "" || r.op === "isnot")
        backend.saveSmartCollection(editingId, nameField.text, matchAll.checked, clean)
    }

    contentItem: ColumnLayout {
        spacing: Theme.s3
        TextField { id: nameField; objectName: "smartName"; Layout.fillWidth: true; placeholderText: qsTr("Collection name") }
        RowLayout {
            spacing: Theme.s2
            Label { text: qsTr("Match"); color: Theme.textSecondary }
            Button { id: matchAll; text: qsTr("all"); checkable: true; checked: true; onClicked: checked = true }
            Button { text: qsTr("any"); checkable: true; checked: !matchAll.checked; onClicked: matchAll.checked = false }
            Label { text: qsTr("of the following rules"); color: Theme.textSecondary }
        }
        Repeater {
            model: sheet.rules.length
            RowLayout {
                required property int index
                Layout.fillWidth: true
                spacing: Theme.s2
                ComboBox {
                    Layout.preferredWidth: 150
                    model: backend.ruleFields()
                    displayText: sheet.fieldLabel(currentText)
                    currentIndex: Math.max(0, backend.ruleFields().indexOf(sheet.rules[index].field))
                    delegate: ItemDelegate { required property string modelData; required property int index; width: parent ? parent.width : 150; text: sheet.fieldLabel(modelData) }
                    onActivated: { const r = sheet.rules.slice(); r[index] = Object.assign({}, r[index], { field: currentText }); sheet.rules = r }
                }
                ComboBox {
                    Layout.preferredWidth: 120
                    model: backend.ruleOps()
                    displayText: sheet.opLabel(currentText)
                    currentIndex: Math.max(0, backend.ruleOps().indexOf(sheet.rules[index].op))
                    delegate: ItemDelegate { required property string modelData; required property int index; width: parent ? parent.width : 120; text: sheet.opLabel(modelData) }
                    onActivated: { const r = sheet.rules.slice(); r[index] = Object.assign({}, r[index], { op: currentText }); sheet.rules = r }
                }
                TextField {
                    Layout.fillWidth: true
                    text: sheet.rules[index].value
                    placeholderText: sheet.hintFor(sheet.rules[index].field)
                    onTextEdited: { const r = sheet.rules.slice(); r[index] = Object.assign({}, r[index], { value: text }); sheet.rules = r }
                }
                ToolButton { icon.name: "x"; enabled: sheet.rules.length > 1; onClicked: { const r = sheet.rules.slice(); r.splice(index, 1); sheet.rules = r } }
            }
        }
        Button { text: qsTr("Add a rule"); icon.name: "plus"; flat: true; onClicked: sheet.rules = sheet.rules.concat([{ field: "family", op: "contains", value: "" }]) }
    }
}
