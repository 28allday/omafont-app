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
                  "class": qsTr("Design class"), trait: qsTr("Trait"), location: qsTr("Location"), weight: qsTr("Weight"),
                  tag: qsTr("Tag"), script: qsTr("Writing system") })[f] ?? f
    }
    function opLabel(o) {
        return ({ contains: qsTr("contains"), begins: qsTr("begins with"), ends: qsTr("ends with"), is: qsTr("is"), isnot: qsTr("is not") })[o] ?? o
    }
    function hintFor(f) {
        return ({ kind: "TrueType · CFF", "class": "Serif · Sans-serif · Handwriting · Ornamental · Symbolic",
                  trait: "Bold · Italic · Monospaced · Condensed · Expanded · Variable · Colour glyphs",
                  location: "Yours · System", weight: "Thin … Regular … Bold … Black", language: qsTr("a language name or code, e.g. German or de"),
                  tag: qsTr("a tag, e.g. display"), script: "Latin · Greek · Cyrillic · Arabic · Devanagari" })[f] ?? ""
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
                id: ruleRow
                required property int index
                // The rule this row edits. Inside a ComboBox's onActivated,
                // `index` is the signal's argument (the chosen menu entry),
                // so the handlers below must say ruleRow.index.
                readonly property var rule: sheet.rules[index] ?? ({ field: "family", op: "contains", value: "" })
                Layout.fillWidth: true
                spacing: Theme.s2
                ComboBox {
                    Layout.preferredWidth: 150
                    model: backend.ruleFields()
                    displayText: sheet.fieldLabel(currentText)
                    currentIndex: Math.max(0, backend.ruleFields().indexOf(ruleRow.rule.field))
                    delegate: ItemDelegate { required property string modelData; required property int index; width: parent ? parent.width : 150; text: sheet.fieldLabel(modelData) }
                    onActivated: { const r = sheet.rules.slice(); r[ruleRow.index] = Object.assign({}, r[ruleRow.index], { field: currentText }); sheet.rules = r }
                }
                ComboBox {
                    Layout.preferredWidth: 120
                    model: backend.ruleOps()
                    displayText: sheet.opLabel(currentText)
                    currentIndex: Math.max(0, backend.ruleOps().indexOf(ruleRow.rule.op))
                    delegate: ItemDelegate { required property string modelData; required property int index; width: parent ? parent.width : 120; text: sheet.opLabel(modelData) }
                    onActivated: { const r = sheet.rules.slice(); r[ruleRow.index] = Object.assign({}, r[ruleRow.index], { op: currentText }); sheet.rules = r }
                }
                TextField {
                    Layout.fillWidth: true
                    text: ruleRow.rule.value
                    placeholderText: sheet.hintFor(ruleRow.rule.field)
                    onTextEdited: { const r = sheet.rules.slice(); r[ruleRow.index] = Object.assign({}, r[ruleRow.index], { value: text }); sheet.rules = r }
                }
                ToolButton { icon.name: "x"; enabled: sheet.rules.length > 1; onClicked: { const r = sheet.rules.slice(); r.splice(ruleRow.index, 1); sheet.rules = r } }
            }
        }
        Button { text: qsTr("Add a rule"); icon.name: "plus"; flat: true; onClicked: sheet.rules = sheet.rules.concat([{ field: "family", op: "contains", value: "" }]) }
    }
}
