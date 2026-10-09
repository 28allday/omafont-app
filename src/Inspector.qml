import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The right-hand panel: what the specimen is showing and what can be done to
// it. With a family selected it is the Info pane: identity, design, legal,
// languages, styles (each in its own face), files, collections, actions. With
// something staged it becomes the install sheet: what was found, validation,
// what is already there and what to do about it, and Install.
Rectangle {
    id: inspector
    color: Theme.panelBg

    signal removeRequested()
    signal validationRequested()
    signal addToCollectionRequested()

    readonly property var sel: backend.selected
    readonly property var info: backend.selectedInfo
    readonly property bool staged: backend.staged
    readonly property bool hasInfo: info.loaded ?? false

    // `sel` is rebuilt on every selectionChanged: metadata batches, a star, a
    // tag. The note and tag fields must survive that, so they follow the
    // family's NAME: only a different family loads new text, and the old
    // family's unsaved note is saved to the old name first.
    property string editingFamily: ""
    // The styles list, replaced only when a face actually changed, so the
    // STYLES rows (each loading its own font file) are not rebuilt per batch.
    property var faces: []
    function followSelection() {
        const f = sel.faces ?? []
        if (JSON.stringify(f) !== JSON.stringify(faces)) faces = f
        const name = staged ? "" : (sel.name ?? "")
        if (name === editingFamily) { noteField.refresh(); return }
        noteField.saveLater()
        editingFamily = name
        noteField.load()
        tagEntry.clear()
    }
    onSelChanged: followSelection()
    onStagedChanged: followSelection()
    Component.onCompleted: followSelection()

    component Chip: Rectangle {
        property string text: ""
        property color tone: Theme.textSecondary
        implicitWidth: chipLabel.implicitWidth + Theme.s2 * 2
        implicitHeight: Theme.szIcon + Theme.s1 + 2
        radius: Theme.rChip
        color: Theme.withAlpha(tone, 0.12)
        border.width: Theme.hairline
        border.color: Theme.withAlpha(tone, 0.45)
        Text {
            id: chipLabel
            anchors.centerIn: parent
            text: parent.text
            textFormat: Text.PlainText
            color: parent.tone
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fsCaption
            font.weight: Theme.wHeading
        }
    }
    // A label / value pair in the Info pane; hidden when there is no value.
    component Fact: RowLayout {
        property string label: ""
        property string value: ""
        property bool mono: false
        visible: value !== ""
        Layout.fillWidth: true
        spacing: Theme.s2
        Label {
            Layout.preferredWidth: Theme.wFactLabel
            Layout.alignment: Qt.AlignTop
            text: label
            color: Theme.textMuted
            font.pixelSize: Theme.fsLabel
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            text: value
            color: Theme.textSecondary
            font.pixelSize: Theme.fsLabel
            font.family: mono ? Theme.monoFamily : Theme.fontFamily
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 6
            elide: Text.ElideRight
        }
    }
    // A long paragraph (copyright, licence, description) folded at four lines.
    component Prose: ColumnLayout {
        property string label: ""
        property string value: ""
        property bool expanded: false
        visible: value !== ""
        Layout.fillWidth: true
        spacing: Theme.s1
        Label { text: label; color: Theme.textMuted; font.pixelSize: Theme.fsLabel }
        Label {
            id: body
            Layout.fillWidth: true
            text: value
            color: Theme.textSecondary
            font.pixelSize: Theme.fsLabel
            wrapMode: Text.Wrap
            maximumLineCount: expanded ? 400 : 4
            elide: Text.ElideRight
        }
        Button {
            visible: body.truncated || expanded
            flat: true
            text: expanded ? qsTr("Show less") : qsTr("Show more")
            onClicked: expanded = !expanded
        }
    }
    component FaceRow: RowLayout {
        property var face: ({})
        property bool forStage: false
        Layout.fillWidth: true
        spacing: Theme.s2
        Loader {
            id: faceLoader
            active: (face.file ?? "") !== ""
            sourceComponent: FontLoader { source: backend.fileUrl(face.file) }
        }
        readonly property bool faceReady: faceLoader.item !== null && faceLoader.item.status === FontLoader.Ready
        Icon {
            visible: forStage && (face.checked ?? false)
            name: !(face.valid ?? true) ? "circle-x" : (face.warnings ?? []).length > 0 ? "circle-alert" : "circle-check"
            color: !(face.valid ?? true) ? Theme.danger : (face.warnings ?? []).length > 0 ? Theme.warning : Theme.success
            size: Theme.szIcon - 2
            ToolTip.visible: statusHover.hovered
            ToolTip.delay: Theme.tooltipDelay
            ToolTip.text: ((face.problems ?? []).concat(face.warnings ?? [])).join("\n") || qsTr("Passed every check")
            HoverHandler { id: statusHover }
        }
        Text {
            Layout.fillWidth: true
            text: forStage ? (face.family + " " + face.style) : ((face.style ?? "") !== "" ? face.style : qsTr("Regular"))
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: (face.disabled ?? false) ? Theme.textMuted : Theme.textPrimary
            font.family: faceReady ? faceLoader.item.name : Theme.fontFamily
            font.pixelSize: faceReady ? Theme.fsRailName : Theme.fsControl
            font.strikeout: face.disabled ?? false
        }
        Chip { visible: face.duplicate ?? false; text: qsTr("duplicate"); tone: Theme.warning }
        Chip { visible: forStage && (face.exists ?? false); text: qsTr("installed"); tone: Theme.warning }
        Label {
            text: (face.ext ?? "").toUpperCase()
            color: Theme.textMuted
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fsCaption
        }
        ToolButton {
            visible: !forStage
            icon.name: (face.disabled ?? false) ? "eye" : "eye-off"
            enabled: !backend.busy
            onClicked: (face.disabled ?? false) ? backend.activateFile(face.file) : backend.deactivateFile(face.file)
            ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
            ToolTip.text: (face.disabled ?? false) ? qsTr("Activate this face") : qsTr("Deactivate this face only")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s3
            implicitHeight: Theme.hNavHeader
            spacing: Theme.s2
            Label { text: inspector.staged ? qsTr("Ready to install") : qsTr("Info"); font.weight: Theme.wHeading; color: Theme.textPrimary }
            Item { Layout.fillWidth: true }
            ToolButton {
                objectName: "compareButton"
                visible: !inspector.staged && (inspector.sel.name ?? "") !== ""
                icon.name: "columns-2"
                checked: backend.compareList.indexOf(inspector.sel.name ?? "") >= 0
                onClicked: { backend.toggleCompare(inspector.sel.name); if (backend.compareList.length > 0) backend.view = "compare" }
                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: checked ? qsTr("Take out of Compare  Ctrl+M") : qsTr("Compare with others  Ctrl+M")
            }
            ToolButton {
                objectName: "starButton"
                visible: !inspector.staged && (inspector.sel.name ?? "") !== ""
                icon.name: "star"
                checked: inspector.sel.starred ?? false
                onClicked: backend.toggleStar(inspector.sel.name)
                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
                ToolTip.text: (inspector.sel.starred ?? false) ? qsTr("Unstar  Ctrl+S") : qsTr("Star this family  Ctrl+S")
            }
            ToolButton {
                visible: inspector.staged || backend.staging
                icon.name: "x"
                onClicked: { backend.clearStage(); backend.selectFirst() }
                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: qsTr("Clear the stage  Esc")
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        Flickable {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: content.implicitHeight + Theme.s4 * 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: content
                x: Theme.s4
                y: Theme.s3
                width: body.width - Theme.s4 * 2
                spacing: Theme.s2

                // ── A selected family: the Info pane ──────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !inspector.staged && (inspector.sel.name ?? "") !== ""
                    spacing: Theme.s2

                    Label {
                        Layout.fillWidth: true
                        text: inspector.sel.name ?? ""
                        font.pixelSize: Theme.fsSection
                        font.weight: Theme.wHeading
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Chip { text: (inspector.sel.user ?? false) ? qsTr("Yours") : qsTr("System"); tone: (inspector.sel.user ?? false) ? Theme.accent : Theme.textSecondary }
                        Chip { visible: inspector.sel.inactive ?? false; text: qsTr("Deactivated"); tone: Theme.warning }
                        Chip { visible: !(inspector.sel.inactive ?? false) && (inspector.sel.disabledCount ?? 0) > 0; text: qsTr("%1 off").arg(Theme.plural(inspector.sel.disabledCount ?? 0, qsTr("face"), qsTr("faces"))); tone: Theme.warning }
                        Chip { visible: inspector.sel.duplicate ?? false; text: qsTr("Duplicates"); tone: Theme.warning }
                        Chip { visible: inspector.sel.mono ?? false; text: qsTr("Monospace"); tone: Theme.accent }
                        Chip { visible: inspector.sel.variable ?? false; text: qsTr("Variable") }
                        Chip { visible: inspector.hasInfo && (inspector.info.color ?? false); text: qsTr("Colour glyphs") }
                        Chip { visible: inspector.sel.coverage ?? false; text: qsTr("Language coverage") }
                        Chip { visible: !(inspector.sel.latin ?? true); text: qsTr("No Latin"); tone: Theme.warning }
                        Chip { text: Theme.plural((inspector.sel.styles ?? []).length, qsTr("style"), qsTr("styles")) }
                    }
                    Label {
                        visible: (inspector.sel.members ?? []).length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Includes %1").arg((inspector.sel.members ?? []).join(", "))
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                    }

                    SectionLabel { text: qsTr("TAGS") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Repeater {
                            model: inspector.sel.tags ?? []
                            Button {
                                required property string modelData
                                text: "#" + modelData
                                font.pixelSize: Theme.fsLabel
                                icon.name: "x"
                                property bool iconTrailing: true
                                onClicked: backend.removeTag(inspector.sel.name, modelData)
                                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: qsTr("Remove this tag")
                            }
                        }
                    }
                    ComboBox {
                        id: tagEntry
                        objectName: "tagEntry"
                        Layout.fillWidth: true
                        editable: true
                        model: backend.allTags().filter(t => (inspector.sel.tags ?? []).indexOf(t) < 0)
                        currentIndex: -1
                        // The typed text lives in the field, not the combo:
                        // the model is rebuilt on every selectionChanged and
                        // must not wipe a half-typed tag. It clears on commit
                        // and when a different family is selected.
                        displayText: tagInput.text
                        function clear() { tagInput.clear(); currentIndex = -1 }
                        function commit() {
                            const t = tagInput.text.trim()
                            if (t === "") return
                            backend.addTag(inspector.editingFamily, t)
                            clear()
                        }
                        onAccepted: commit()
                        onActivated: (index) => { if (index >= 0) { backend.addTag(inspector.editingFamily, model[index]); clear() } }
                        contentItem: TextField {
                            id: tagInput
                            placeholderText: qsTr("Add a tag…")
                            onAccepted: tagEntry.commit()
                            background: null
                        }
                    }

                    SectionLabel { text: qsTr("DETAILS") }
                    Fact { label: qsTr("Kind"); value: inspector.hasInfo ? (inspector.info.kind ?? "") : (inspector.sel.formats ?? []).join(", ").replace(/CFF/g, "OpenType PostScript") }
                    Fact { label: qsTr("Class"); value: inspector.hasInfo && inspector.info.classification !== "Unknown" ? (inspector.info.classification ?? "") : "" }
                    Fact { label: qsTr("Version"); value: inspector.hasInfo ? (inspector.info.version ?? "") : (((inspector.sel.faces ?? [])[0] ?? {}).version ?? "") }
                    Fact { label: qsTr("PostScript"); value: inspector.hasInfo ? (inspector.info.postscriptName ?? "") : (((inspector.sel.faces ?? [])[0] ?? {}).psName ?? ""); mono: true }
                    Fact { label: qsTr("Unique name"); value: inspector.hasInfo ? (inspector.info.uniqueId ?? "") : ""; mono: true }
                    Fact { label: qsTr("Glyphs"); value: inspector.hasInfo && inspector.info.glyphCount > 0 ? String(inspector.info.glyphCount) : "" }
                    Fact { label: qsTr("Units per em"); value: inspector.hasInfo && inspector.info.unitsPerEm > 0 ? String(inspector.info.unitsPerEm) : "" }
                    Fact { label: qsTr("Weight"); value: inspector.hasInfo && inspector.info.weightClass > 0 ? inspector.info.weightName + " (" + inspector.info.weightClass + ")" : "" }
                    Fact { label: qsTr("Width"); value: inspector.hasInfo ? (inspector.info.widthName ?? "") : "" }
                    Fact { label: qsTr("Traits"); value: inspector.hasInfo ? (inspector.info.traits ?? []).join(", ") : "" }
                    Fact { label: qsTr("Embedding"); value: inspector.hasInfo ? (inspector.info.embedding ?? "") : "" }
                    Label { visible: !inspector.hasInfo && backend.readingMetadata; text: qsTr("Reading the file's own details…"); color: Theme.textMuted; font.pixelSize: Theme.fsLabel }

                    SectionLabel { visible: inspector.hasInfo && ((inspector.info.designer ?? "") !== "" || (inspector.info.manufacturer ?? "") !== "" || (inspector.sel.foundry ?? "") !== ""); text: qsTr("DESIGN") }
                    Fact { label: qsTr("Designer"); value: inspector.hasInfo ? (inspector.info.designer ?? "") : "" }
                    Fact { label: qsTr("Manufacturer"); value: inspector.hasInfo ? (inspector.info.manufacturer ?? "") : "" }
                    Fact { label: qsTr("Foundry"); value: (inspector.sel.foundry ?? "") !== "unknown" ? (inspector.sel.foundry ?? "") : "" }
                    Fact { label: qsTr("Trademark"); value: inspector.hasInfo ? (inspector.info.trademark ?? "") : "" }
                    Fact { label: qsTr("Designer URL"); value: inspector.hasInfo ? (inspector.info.designerUrl ?? "") : ""; mono: true }
                    Fact { label: qsTr("Vendor URL"); value: inspector.hasInfo ? (inspector.info.vendorUrl ?? "") : ""; mono: true }

                    SectionLabel { visible: inspector.hasInfo && ((inspector.info.copyright ?? "") !== "" || (inspector.info.license ?? "") !== ""); text: qsTr("LEGAL") }
                    Prose { label: qsTr("Copyright"); value: inspector.hasInfo ? (inspector.info.copyright ?? "") : "" }
                    Prose { label: qsTr("Licence"); value: inspector.hasInfo ? (inspector.info.license ?? "") : "" }
                    Fact { label: qsTr("Licence URL"); value: inspector.hasInfo ? (inspector.info.licenseUrl ?? "") : ""; mono: true }
                    Prose { label: qsTr("Description"); value: inspector.hasInfo ? (inspector.info.description ?? "") : "" }

                    SectionLabel { visible: (inspector.sel.languages ?? []).length > 0; text: qsTr("LANGUAGES  ·  %1").arg((inspector.sel.languages ?? []).length) }
                    Label {
                        id: languageList
                        property bool expanded: false
                        visible: (inspector.sel.languages ?? []).length > 0
                        Layout.fillWidth: true
                        text: (expanded ? (inspector.sel.languages ?? []) : (inspector.sel.languages ?? []).slice(0, 14)).join(", ")
                              + (!expanded && (inspector.sel.languages ?? []).length > 14 ? qsTr(" and %1 more").arg((inspector.sel.languages ?? []).length - 14) : "")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fsLabel
                        wrapMode: Text.Wrap
                    }
                    Button { visible: (inspector.sel.languages ?? []).length > 14; flat: true; text: languageList.expanded ? qsTr("Show less") : qsTr("Show all"); onClicked: languageList.expanded = !languageList.expanded }

                    SectionLabel { text: qsTr("STYLES") }
                    Repeater {
                        model: inspector.faces
                        FaceRow { required property var modelData; face: modelData }
                    }

                    SectionLabel { text: qsTr("ON DISK") }
                    Label {
                        Layout.fillWidth: true
                        text: (inspector.sel.directory ?? "").replace(/^\/home\/[^/]+/, "~")
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fsLabel
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: Theme.plural((inspector.sel.files ?? []).length, qsTr("file"), qsTr("files")) + ", " + backend.formatBytes(inspector.sel.bytes ?? 0)
                              + ((inspector.sel.added ?? "") !== "" ? qsTr(" · added %1").arg(new Date(inspector.sel.added).toLocaleDateString(Qt.locale(), Locale.ShortFormat)) : "")
                              + ((inspector.sel.source ?? "") !== "" ? qsTr(" from %1").arg(inspector.sel.source) : "")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                    }
                    Label {
                        visible: (inspector.sel.scripts ?? []).length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Writing systems: %1").arg((inspector.sel.scripts ?? []).join(", "))
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                    }

                    SectionLabel { text: qsTr("NOTE") }
                    TextArea {
                        id: noteField
                        objectName: "noteField"
                        Layout.fillWidth: true
                        placeholderText: qsTr("Why you have it, where it is licensed, what it is for…")
                        wrapMode: TextEdit.Wrap
                        // What the library holds for editingFamily; text that
                        // differs from it is an edit not yet saved.
                        property string saved: ""
                        function load() { saved = inspector.sel.note ?? ""; text = saved }
                        function refresh() { if (!activeFocus && text === saved) load() }
                        function save() {
                            if (inspector.editingFamily === "" || text === saved) return
                            saved = text
                            backend.setNote(inspector.editingFamily, text)
                        }
                        // Leaving a family happens inside a `sel` change; saving
                        // there would change `sel` again mid-update, so the old
                        // family's note is written once the change has settled.
                        function saveLater() {
                            if (inspector.editingFamily === "" || text === saved) return
                            const family = inspector.editingFamily, note = text
                            Qt.callLater(() => backend.setNote(family, note))
                        }
                        onEditingFinished: save()
                        onActiveFocusChanged: if (!activeFocus) save()
                        // Ctrl+S here saves the note; the window's star shortcut stands down while typing.
                        Keys.onPressed: (event) => { if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) { save(); event.accepted = true } }
                    }
                    Label {
                        visible: !(inspector.sel.user ?? false)
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Belongs to a package. It can be deactivated, not removed; pacman owns it.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                    }

                    SectionLabel { text: qsTr("COLLECTIONS") }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Repeater {
                            model: backend.collections
                            Button {
                                required property var modelData
                                readonly property bool member: (inspector.sel.collections ?? []).indexOf(modelData.id) >= 0
                                text: modelData.name
                                checked: member
                                flat: !member
                                onClicked: member ? backend.removeFromCollection(modelData.id, inspector.sel.name) : backend.addToCollection(modelData.id, inspector.sel.name)
                                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
                                ToolTip.text: member ? qsTr("Remove from %1").arg(modelData.name) : qsTr("Add to %1").arg(modelData.name)
                            }
                        }
                        Button { flat: true; icon.name: "plus"; text: backend.collections.length === 0 ? qsTr("New collection") : ""; onClicked: inspector.addToCollectionRequested() }
                    }

                    SectionLabel { text: qsTr("ACTIONS") }
                    Button {
                        Layout.fillWidth: true
                        icon.name: (inspector.sel.inactive ?? false) ? "eye" : "eye-off"
                        text: (inspector.sel.inactive ?? false) ? qsTr("Activate") : qsTr("Deactivate")
                        enabled: !backend.busy
                        onClicked: (inspector.sel.inactive ?? false) ? backend.activateSelected() : backend.deactivateSelected()
                    }
                    Button { Layout.fillWidth: true; icon.name: "shield-check"; text: qsTr("Validate"); onClicked: { backend.validateSelected(); inspector.validationRequested() } }
                    Button { Layout.fillWidth: true; icon.name: "folder-output"; text: qsTr("Export…"); onClicked: backend.exportSelected() }
                    Button { Layout.fillWidth: true; icon.name: "external-link"; text: qsTr("Show in file manager"); onClicked: backend.revealSelected() }
                    Button {
                        id: pdfButton
                        Layout.fillWidth: true
                        icon.name: "printer"
                        text: qsTr("Save specimen as PDF")
                        property bool opensMenu: true
                        onClicked: pdfMenu.popup(pdfButton, 0, pdfButton.height)
                        Menu {
                            id: pdfMenu
                            MenuItem { text: qsTr("Specimen"); onTriggered: backend.saveSpecimenPdf("specimen") }
                            MenuItem { text: qsTr("Waterfall"); onTriggered: backend.saveSpecimenPdf("waterfall") }
                            MenuItem { text: qsTr("Repertoire"); onTriggered: backend.saveSpecimenPdf("repertoire") }
                        }
                    }
                    Button {
                        Layout.fillWidth: true
                        visible: backend.canSetTerminalFont
                        icon.name: "terminal"
                        text: qsTr("Set as terminal font")
                        enabled: (inspector.sel.mono ?? false) && !backend.busy
                        onClicked: backend.setAsTerminalFont()
                        ToolTip.visible: hovered && !enabled
                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.text: qsTr("Only a monospace family can be the terminal font")
                    }
                    Button {
                        Layout.fillWidth: true
                        icon.name: "trash-2"
                        text: qsTr("Remove from your fonts")
                        enabled: (inspector.sel.user ?? false) && !backend.busy
                        onClicked: inspector.removeRequested()
                    }
                }

                // ── A staged install ──────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: inspector.staged
                    spacing: Theme.s2

                    Label {
                        Layout.fillWidth: true
                        text: backend.stagedGroup
                        font.pixelSize: Theme.fsSection
                        font.weight: Theme.wHeading
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("from %1").arg(backend.stagedSource)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                        elide: Text.ElideMiddle
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Chip { text: Theme.plural(backend.stagedSelected.length, qsTr("face"), qsTr("faces")); tone: Theme.accent }
                        Chip { visible: backend.stagedInstalled; text: qsTr("Already installed"); tone: Theme.warning }
                        Chip { visible: backend.stagedTruncated; text: qsTr("Truncated"); tone: Theme.warning }
                    }

                    // Validation, before anything is written.
                    SectionLabel { text: qsTr("VALIDATION") }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s2
                        Icon {
                            name: backend.stagedProblems > 0 ? "circle-alert" : backend.stagedWarnings > 0 ? "info" : "circle-check"
                            color: backend.stagedProblems > 0 ? Theme.danger : backend.stagedWarnings > 0 ? Theme.warning : Theme.success
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fsLabel
                            text: backend.stagedProblems > 0 ? qsTr("%1 with serious problems, left out unless you say otherwise").arg(backend.stagedProblems)
                                : backend.stagedWarnings > 0 ? qsTr("Minor findings in %1; they will install").arg(backend.stagedWarnings)
                                : qsTr("No problems found")
                        }
                        Button { flat: true; text: qsTr("Report…"); onClicked: { backend.validateStaged(); inspector.validationRequested() } }
                    }
                    CheckBox {
                        visible: backend.stagedProblems > 0
                        text: qsTr("Install the ones with problems anyway")
                        checked: backend.installInvalid
                        onToggled: backend.installInvalid = checked
                    }

                    // Already there: replace, skip, or keep both.
                    SectionLabel { visible: backend.stagedExisting > 0; text: qsTr("ALREADY INSTALLED  ·  %1").arg(backend.stagedExisting) }
                    RowLayout {
                        visible: backend.stagedExisting > 0
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Button { Layout.fillWidth: true; text: qsTr("Replace"); checked: backend.conflictPolicy === "replace"; onClicked: backend.conflictPolicy = "replace" }
                        Button { Layout.fillWidth: true; text: qsTr("Skip"); checked: backend.conflictPolicy === "skip"; onClicked: backend.conflictPolicy = "skip" }
                        Button { Layout.fillWidth: true; text: qsTr("Keep both"); checked: backend.conflictPolicy === "keep"; onClicked: backend.conflictPolicy = "keep" }
                    }
                    Label {
                        visible: backend.stagedExisting > 0
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                        text: backend.conflictPolicy === "skip" ? qsTr("Files already in place are left as they are.")
                            : backend.conflictPolicy === "keep" ? qsTr("New copies are saved beside the old ones with a -2 suffix.")
                            : qsTr("Files already in place are overwritten.")
                    }

                    // OTF and TTF twins: pick one, or take both.
                    SectionLabel { visible: backend.stagedOtfCount > 0 && backend.stagedTtfCount > 0; text: qsTr("FORMAT") }
                    RowLayout {
                        visible: backend.stagedOtfCount > 0 && backend.stagedTtfCount > 0
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Button { Layout.fillWidth: true; text: qsTr("Both"); checked: backend.stagedFormat === "all"; onClicked: backend.stagedFormat = "all" }
                        Button { Layout.fillWidth: true; text: qsTr("OTF · %1").arg(backend.stagedOtfCount); checked: backend.stagedFormat === "otf"; onClicked: backend.stagedFormat = "otf" }
                        Button { Layout.fillWidth: true; text: qsTr("TTF · %1").arg(backend.stagedTtfCount); checked: backend.stagedFormat === "ttf"; onClicked: backend.stagedFormat = "ttf" }
                    }

                    SectionLabel { text: qsTr("FAMILIES") }
                    Repeater {
                        model: backend.stagedFamilies
                        RowLayout {
                            required property string modelData
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: modelData; elide: Text.ElideRight; color: Theme.textPrimary }
                            Chip { visible: backend.families.indexOf(modelData) >= 0 || backend.stagedInstalled; text: qsTr("installed"); tone: Theme.warning }
                        }
                    }

                    SectionLabel { text: qsTr("FACES") }
                    Repeater {
                        model: backend.stagedSelected
                        FaceRow { required property var modelData; face: modelData; forStage: true }
                    }

                    SectionLabel { text: qsTr("WILL GO TO") }
                    Label {
                        Layout.fillWidth: true
                        text: backend.tildePath(backend.fontRoot + "/" + backend.stagedGroup.replace(/[^A-Za-z0-9._ -]/g, "").replace(/\s+/g, "-").replace(/^[-.]+/, ""))
                        color: Theme.textSecondary
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fsLabel
                        wrapMode: Text.WrapAnywhere
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("No password is needed. Apps already running will see the new font after they restart; that is fontconfig, not OmaFont.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsLabel
                    }

                    Button {
                        objectName: "inspectorInstall"
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.s2
                        highlighted: true
                        icon.name: "download"
                        text: backend.stagedInstalled ? qsTr("Reinstall") : qsTr("Install")
                        enabled: !backend.busy && backend.stagedSelected.length > 0
                        onClicked: backend.installStaged()
                    }
                    Button { Layout.fillWidth: true; text: qsTr("Clear"); onClicked: { backend.clearStage(); backend.selectFirst() } }
                }

                // ── Nothing at all ────────────────────────────────────────
                Label {
                    visible: !inspector.staged && (inspector.sel.name ?? "") === ""
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    text: backend.staging ? qsTr("Reading what was dropped…") : qsTr("Select a family to see its details.")
                }
            }
        }
    }
}
