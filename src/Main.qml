import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

ApplicationWindow {
    id: win
    width: 1440
    height: 860
    minimumWidth: 980
    minimumHeight: 560
    visible: true
    title: (backend.previewTitle !== "" ? backend.previewTitle + " — " : "") + Theme.appName
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
    color: Theme.windowBg

    readonly property bool dialogOpen: Sheets.count > 0
    readonly property bool textEntryFocused: activeFocusItem !== null
                                             && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)
    readonly property bool compactToolbar: width < 1280
    property bool sidebarOpen: true
    readonly property bool canRemove: (backend.selected.user ?? false) && !backend.busy && !backend.staged
    readonly property bool canTerminal: backend.canSetTerminalFont && (backend.selected.mono ?? false) && !backend.busy && !backend.staged
    readonly property bool hasFamily: backend.hasSelection && !backend.staged

    // ── Chrome pieces ───────────────────────────────────────────────────────
    component ToolAction: Button {
        property string tip: ""
        flat: true
        implicitHeight: Theme.hToolButton
        display: win.compactToolbar && icon.name !== "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: Theme.tooltipDelay
        ToolTip.text: tip
        Accessible.name: text
    }
    component ToolbarRule: Rectangle {
        implicitWidth: Theme.hairline
        Layout.fillHeight: true
        Layout.topMargin: Theme.s3
        Layout.bottomMargin: Theme.s3
        Layout.leftMargin: Theme.s1
        Layout.rightMargin: Theme.s1
        color: Theme.border
    }
    component StatusText: Label {
        color: Theme.textSecondary
        font.pixelSize: Theme.fsLabel
    }
    component StatusRule: Rectangle {
        implicitWidth: Theme.hairline
        implicitHeight: Theme.s4
        color: Theme.border
    }
    component Hairline: Rectangle {
        color: Theme.border
        height: Theme.hairline
    }
    component ViewButton: ToolButton {
        property string view: ""
        checked: backend.view === view
        onClicked: backend.view = view
        ToolTip.visible: hovered
        ToolTip.delay: Theme.tooltipDelay
    }

    // ── Keys ────────────────────────────────────────────────────────────────
    // Single-family actions (Ctrl+M/T/D/E/S, Ctrl+Shift+V) stand down while a
    // text field has the keyboard: Ctrl+S in the note must not star, Ctrl+D in
    // the sample must not deactivate.
    Shortcut { sequences: [StandardKey.Open]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.openPicker() }
    Shortcut { sequences: [StandardKey.Refresh, "Ctrl+R"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.refresh() }
    Shortcut { sequences: [StandardKey.Find, "/"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused
               onActivated: { searchField.forceActiveFocus(); searchField.selectAll() } }
    Shortcut { sequence: "Ctrl+L"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.showCoverage = !backend.showCoverage }
    Shortcut { sequence: "Ctrl+B"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.sidebarOpen = !win.sidebarOpen }
    Shortcut { sequence: "Ctrl+1"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.view = "specimen" }
    Shortcut { sequence: "Ctrl+2"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.view = "grid" }
    Shortcut { sequence: "Ctrl+3"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.view = "list" }
    Shortcut { sequence: "Ctrl+4"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.view = "repertoire" }
    Shortcut { sequence: "Ctrl+5"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.view = "compare" }
    Shortcut { sequence: "Ctrl+M"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.hasFamily; onActivated: { backend.toggleCompare(backend.selectedName); if (backend.compareList.length > 0) backend.view = "compare" } }
    Shortcut { sequence: "Ctrl+Shift+T"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: tidySheet.open() }
    Shortcut { sequences: ["Ctrl++", "Ctrl+="]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.specimenScale = backend.specimenScale * 1.15 }
    Shortcut { sequence: "Ctrl+-"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.specimenScale = backend.specimenScale / 1.15 }
    Shortcut { sequence: "Ctrl+0"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.specimenScale = 1 }
    Shortcut { sequence: "Ctrl+I"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && backend.staged && !backend.busy; onActivated: backend.installStaged() }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut
               enabled: !win.dialogOpen && backend.staged && !backend.busy && !win.textEntryFocused; onActivated: backend.installStaged() }
    Shortcut { sequence: "Delete"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.canRemove; onActivated: removeSheet.open() }
    Shortcut { sequence: "Ctrl+T"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.canTerminal; onActivated: backend.setAsTerminalFont() }
    Shortcut { sequence: "Ctrl+D"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.hasFamily && !backend.busy
               onActivated: (backend.selected.inactive ?? false) ? backend.activateSelected() : backend.deactivateSelected() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.hasFamily; onActivated: backend.exportSelected() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.hasFamily; onActivated: backend.toggleStar(backend.selectedName) }
    Shortcut { sequence: "Ctrl+Shift+V"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused && win.hasFamily; onActivated: { backend.validateSelected(); validationSheet.open() } }
    Shortcut { sequences: [StandardKey.Print]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasFamily; onActivated: backend.saveSpecimenPdf("specimen") }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen
               onActivated: {
                   if (backend.staged || backend.staging) { backend.clearStage(); backend.selectFirst(); return }
                   if (backend.filter !== "") { backend.filter = ""; searchField.text = ""; return }
                   if (win.textEntryFocused) rail.forceActiveFocus()
               } }
    Shortcut { sequences: ["?", "F1"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.textEntryFocused; onActivated: helpSheet.open() }
    Shortcut { sequences: [StandardKey.Quit]; context: Qt.ApplicationShortcut; onActivated: win.close() }

    // ── Layout ──────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title and menus
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.hTitleBar
            color: Theme.windowBg
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s4
                anchors.rightMargin: Theme.s3
                spacing: Theme.s3
                Label { text: Theme.appName; color: Theme.textPrimary; font.pixelSize: Theme.fsTitle; font.weight: Font.Bold }
                MenuBar {
                    Layout.fillHeight: true
                    Menu {
                        title: qsTr("File")
                        MenuItem { text: qsTr("Install fonts…"); icon.name: "folder-plus"; onTriggered: backend.openPicker() }
                        MenuItem { text: qsTr("Rescan"); icon.name: "refresh-cw"; enabled: !backend.scanning; onTriggered: backend.refresh() }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Export family…"); icon.name: "folder-output"; enabled: win.hasFamily; onTriggered: backend.exportSelected() }
                        Menu {
                            title: qsTr("Save as PDF")
                            MenuItem { text: qsTr("Specimen"); enabled: win.hasFamily; onTriggered: backend.saveSpecimenPdf("specimen") }
                            MenuItem { text: qsTr("Waterfall"); enabled: win.hasFamily; onTriggered: backend.saveSpecimenPdf("waterfall") }
                            MenuItem { text: qsTr("Repertoire"); enabled: win.hasFamily; onTriggered: backend.saveSpecimenPdf("repertoire") }
                        }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Tidy up…"); icon.name: "brush-cleaning"; onTriggered: tidySheet.open() }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Quit"); onTriggered: win.close() }
                    }
                    Menu {
                        title: qsTr("Font")
                        MenuItem { text: qsTr("Install what is staged"); icon.name: "download"; enabled: backend.staged && !backend.busy; onTriggered: backend.installStaged() }
                        MenuItem { text: qsTr("Clear the stage"); icon.name: "x"; enabled: backend.staged || backend.staging; onTriggered: { backend.clearStage(); backend.selectFirst() } }
                        MenuSeparator {}
                        MenuItem {
                            text: (backend.selected.inactive ?? false) ? qsTr("Activate") : qsTr("Deactivate")
                            icon.name: (backend.selected.inactive ?? false) ? "eye" : "eye-off"
                            enabled: win.hasFamily && !backend.busy
                            onTriggered: (backend.selected.inactive ?? false) ? backend.activateSelected() : backend.deactivateSelected()
                        }
                        MenuItem { text: (backend.selected.starred ?? false) ? qsTr("Unstar") : qsTr("Star"); icon.name: "star"; enabled: win.hasFamily; onTriggered: backend.toggleStar(backend.selectedName) }
                        MenuItem { text: backend.compareList.indexOf(backend.selectedName) >= 0 ? qsTr("Take out of Compare") : qsTr("Add to Compare"); icon.name: "columns-2"; enabled: win.hasFamily
                                   onTriggered: { backend.toggleCompare(backend.selectedName); if (backend.compareList.length > 0) backend.view = "compare" } }
                        MenuItem { text: qsTr("Validate"); icon.name: "shield-check"; enabled: win.hasFamily; onTriggered: { backend.validateSelected(); validationSheet.open() } }
                        MenuItem { text: qsTr("Show in file manager"); icon.name: "external-link"; enabled: win.hasFamily; onTriggered: backend.revealSelected() }
                        Menu {
                            id: addToMenu
                            title: qsTr("Add to collection")
                            enabled: win.hasFamily
                            Instantiator {
                                model: backend.collections
                                delegate: MenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    checkable: true
                                    checked: backend.inCollection(modelData.id, backend.selectedName)
                                    onTriggered: checked ? backend.addToCollection(modelData.id, backend.selectedName) : backend.removeFromCollection(modelData.id, backend.selectedName)
                                }
                                onObjectAdded: (index, object) => addToMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => addToMenu.removeItem(object)
                            }
                            MenuSeparator { visible: backend.collections.length > 0 }
                            MenuItem { text: qsTr("New collection…"); icon.name: "plus"; onTriggered: nameSheet.openNew(qsTr("New collection")) }
                        }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Set as terminal font"); icon.name: "terminal"; enabled: win.canTerminal; onTriggered: backend.setAsTerminalFont() }
                        MenuItem { text: qsTr("Remove…"); icon.name: "trash-2"; enabled: win.canRemove; onTriggered: removeSheet.open() }
                    }
                    Menu {
                        title: qsTr("View")
                        MenuItem { text: qsTr("Specimen"); checkable: true; checked: backend.view === "specimen"; onTriggered: backend.view = "specimen" }
                        MenuItem { text: qsTr("Grid"); checkable: true; checked: backend.view === "grid"; onTriggered: backend.view = "grid" }
                        MenuItem { text: qsTr("List"); checkable: true; checked: backend.view === "list"; onTriggered: backend.view = "list" }
                        MenuItem { text: qsTr("Repertoire"); checkable: true; checked: backend.view === "repertoire"; onTriggered: backend.view = "repertoire" }
                        MenuItem { text: qsTr("Compare"); checkable: true; checked: backend.view === "compare"; onTriggered: backend.view = "compare" }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Bigger"); onTriggered: backend.specimenScale = backend.specimenScale * 1.15 }
                        MenuItem { text: qsTr("Smaller"); onTriggered: backend.specimenScale = backend.specimenScale / 1.15 }
                        MenuItem { text: qsTr("Actual size"); onTriggered: backend.specimenScale = 1 }
                        MenuSeparator {}
                        Menu {
                            title: qsTr("Sort by")
                            MenuItem { text: qsTr("Name"); checkable: true; checked: backend.sortOrder === "name"; onTriggered: backend.sortOrder = "name" }
                            MenuItem { text: qsTr("Recently added"); checkable: true; checked: backend.sortOrder === "added"; onTriggered: backend.sortOrder = "added" }
                            MenuItem { text: qsTr("Number of styles"); checkable: true; checked: backend.sortOrder === "styles"; onTriggered: backend.sortOrder = "styles" }
                            MenuItem { text: qsTr("Foundry"); checkable: true; checked: backend.sortOrder === "foundry"; onTriggered: backend.sortOrder = "foundry" }
                        }
                        MenuItem { text: qsTr("Group weights into one family"); checkable: true; checked: backend.groupWeights; onTriggered: backend.groupWeights = !backend.groupWeights }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Sidebar"); checkable: true; checked: win.sidebarOpen; onTriggered: win.sidebarOpen = !win.sidebarOpen }
                        MenuItem { text: qsTr("Language fonts"); checkable: true; checked: backend.showCoverage; onTriggered: backend.showCoverage = !backend.showCoverage }
                        MenuItem { text: qsTr("Reset the sample text"); enabled: backend.sample !== backend.defaultSample; onTriggered: backend.sample = backend.defaultSample }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Less movement"); checkable: true; checked: backend.reducedMotion; onTriggered: backend.reducedMotion = !backend.reducedMotion }
                        MenuItem { text: qsTr("Stronger contrast"); checkable: true; checked: backend.highContrast; onTriggered: backend.highContrast = !backend.highContrast }
                    }
                    Menu {
                        title: qsTr("Collections")
                        MenuItem { text: qsTr("New collection…"); icon.name: "plus"; onTriggered: nameSheet.openNew(qsTr("New collection")) }
                        MenuItem { text: qsTr("New smart collection…"); icon.name: "search"; onTriggered: smartSheet.openNew() }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Activate everything in this collection"); icon.name: "eye"; enabled: backend.source.indexOf("coll:") === 0; onTriggered: backend.activateCollection(backend.source.substring(5)) }
                        MenuItem { text: qsTr("Deactivate everything in this collection"); icon.name: "eye-off"; enabled: backend.source.indexOf("coll:") === 0; onTriggered: backend.deactivateCollection(backend.source.substring(5)) }
                        MenuItem { text: qsTr("Export this collection…"); icon.name: "folder-output"; enabled: backend.source.indexOf("coll:") === 0; onTriggered: backend.exportCollection(backend.source.substring(5)) }
                        MenuSeparator {}
                        MenuItem { text: qsTr("Edit this smart collection…"); enabled: backend.source.indexOf("smart:") === 0; onTriggered: smartSheet.openEdit(backend.source.substring(6)) }
                        MenuItem { text: qsTr("Rename this collection…"); enabled: backend.source.indexOf("coll:") === 0 || backend.source.indexOf("smart:") === 0
                                   onTriggered: nameSheet.openRename(backend.source.substring(backend.source.indexOf(":") + 1), backend.sourceTitle) }
                        MenuItem { text: qsTr("Delete this collection…"); icon.name: "trash-2"; enabled: backend.source.indexOf("coll:") === 0 || backend.source.indexOf("smart:") === 0
                                   onTriggered: win.confirmDeleteCollection(backend.source.substring(backend.source.indexOf(":") + 1), backend.sourceTitle, backend.source.indexOf("smart:") === 0) }
                    }
                    Menu {
                        title: qsTr("Help")
                        MenuItem { text: qsTr("Keyboard shortcuts"); icon.name: "keyboard"; onTriggered: helpSheet.open() }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        // Toolbar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.hToolbar
            color: Theme.panelBg
            Hairline { anchors.top: parent.top; width: parent.width }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s3
                anchors.rightMargin: Theme.s3
                spacing: Theme.s1

                ToolButton { icon.name: "panel-left"; checked: win.sidebarOpen; onClicked: win.sidebarOpen = !win.sidebarOpen
                             ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: qsTr("Sidebar  Ctrl+B") }
                ToolAction { objectName: "toolbarInstall"; icon.name: "folder-plus"; text: qsTr("Install…"); tip: qsTr("Install fonts from a file, folder or archive  Ctrl+O"); onClicked: backend.openPicker() }
                ToolAction { objectName: "toolbarRescan"; icon.name: "refresh-cw"; text: qsTr("Rescan"); tip: qsTr("Read the font list again  Ctrl+R"); enabled: !backend.scanning; onClicked: backend.refresh() }
                ToolbarRule {}
                TextField {
                    id: searchField
                    objectName: "searchField"
                    Layout.preferredWidth: Theme.wSearch
                    Layout.maximumWidth: Theme.wSearch
                    placeholderText: backend.searchScope === "designer" ? qsTr("Designer or foundry")
                                   : backend.searchScope === "characters" ? qsTr("Characters the font must have")
                                   : qsTr("Filter by name")
                    leftPadding: Theme.s2 + Theme.szIcon + Theme.s2
                    rightPadding: Theme.szIconHit + Theme.s1
                    onTextChanged: backend.filter = text
                    Keys.onEscapePressed: { if (text !== "") { text = ""; backend.filter = "" } else rail.forceActiveFocus() }
                    Keys.onDownPressed: rail.forceActiveFocus()
                    Icon { name: "search"; color: Theme.textMuted; x: Theme.s2; anchors.verticalCenter: parent.verticalCenter }
                    ToolButton {
                        objectName: "searchScope"
                        icon.name: searchField.text !== "" ? "x" : "chevron-down"
                        anchors.right: parent.right; anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: { if (searchField.text !== "") { searchField.text = ""; backend.filter = "" } else scopeMenu.popup() }
                        ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
                        ToolTip.text: searchField.text !== "" ? qsTr("Clear") : qsTr("What to search")
                    }
                    Menu {
                        id: scopeMenu
                        MenuItem { text: qsTr("Names"); checkable: true; checked: backend.searchScope === "names"; onTriggered: backend.searchScope = "names" }
                        MenuItem { text: qsTr("Designer or foundry"); checkable: true; checked: backend.searchScope === "designer"; onTriggered: backend.searchScope = "designer" }
                        MenuItem { text: qsTr("Supports these characters"); checkable: true; checked: backend.searchScope === "characters"; onTriggered: backend.searchScope = "characters" }
                    }
                }
                ToolAction {
                    objectName: "toolbarCoverage"
                    icon.name: backend.showCoverage ? "eye" : "eye-off"
                    text: backend.showCoverage ? qsTr("Language fonts shown") : qsTr("%1 hidden").arg(Theme.plural(backend.coverageCount, qsTr("language font"), qsTr("language fonts")))
                    tip: qsTr("Show or fold away the per-script Noto families  Ctrl+L")
                    checked: backend.showCoverage
                    visible: backend.coverageCount > 0 && (backend.source === "all" || backend.source === "system")
                    onClicked: backend.showCoverage = !backend.showCoverage
                }
                Item { Layout.fillWidth: true }

                // The view: specimen, grid, list, repertoire; and how big.
                Rectangle {
                    implicitWidth: viewRow.implicitWidth + Theme.s1 * 2
                    implicitHeight: Theme.hControl + Theme.s1
                    radius: Theme.rControl
                    color: Theme.withAlpha(Theme.controlBg, 0.55)
                    border.width: Theme.hairline
                    border.color: Theme.border
                    RowLayout {
                        id: viewRow
                        anchors.centerIn: parent
                        spacing: 0
                        ViewButton { objectName: "viewSpecimen"; view: "specimen"; icon.name: "type"; ToolTip.text: qsTr("Specimen  Ctrl+1") }
                        ViewButton { objectName: "viewGrid"; view: "grid"; icon.name: "layout-grid"; ToolTip.text: qsTr("Grid  Ctrl+2") }
                        ViewButton { objectName: "viewList"; view: "list"; icon.name: "list"; ToolTip.text: qsTr("List  Ctrl+3") }
                        ViewButton { objectName: "viewRepertoire"; view: "repertoire"; icon.name: "grid-3x3"; ToolTip.text: qsTr("Repertoire  Ctrl+4") }
                        ViewButton { objectName: "viewCompare"; view: "compare"; icon.name: "columns-2"; ToolTip.text: backend.compareList.length > 0 ? qsTr("Compare · %1  Ctrl+5").arg(backend.compareList.length) : qsTr("Compare  Ctrl+5") }
                    }
                }
                Slider {
                    id: sizeSlider
                    objectName: "sizeSlider"
                    Layout.preferredWidth: 110
                    from: 0.5; to: 2.5
                    value: backend.specimenScale
                    onMoved: backend.specimenScale = value
                    ToolTip.visible: hovered || pressed
                    ToolTip.delay: Theme.tooltipDelay
                    ToolTip.text: qsTr("Text size  %1%  (Ctrl + / −)").arg(Math.round(backend.specimenScale * 100))
                }
                ToolbarRule {}
                ToolAction {
                    objectName: "toolbarTerminal"
                    icon.name: "terminal"; text: qsTr("Terminal font")
                    tip: qsTr("Make this monospace family the terminal font  Ctrl+T")
                    visible: backend.canSetTerminalFont
                    enabled: win.canTerminal
                    onClicked: backend.setAsTerminalFont()
                }
                ToolAction {
                    objectName: "toolbarDeactivate"
                    icon.name: (backend.selected.inactive ?? false) ? "eye" : "eye-off"
                    text: (backend.selected.inactive ?? false) ? qsTr("Activate") : qsTr("Deactivate")
                    tip: qsTr("Hide this family from every app without deleting it  Ctrl+D")
                    enabled: win.hasFamily && !backend.busy
                    onClicked: (backend.selected.inactive ?? false) ? backend.activateSelected() : backend.deactivateSelected()
                }
                ToolAction {
                    objectName: "toolbarRemove"
                    icon.name: "trash-2"; text: qsTr("Remove")
                    tip: qsTr("Remove this family from your fonts  Delete")
                    enabled: win.canRemove
                    onClicked: removeSheet.open()
                }
                ToolbarRule { visible: backend.staged }
                Button {
                    objectName: "toolbarInstallStaged"
                    visible: backend.staged
                    highlighted: true
                    icon.name: "download"
                    text: backend.stagedInstalled ? qsTr("Reinstall") : qsTr("Install")
                    enabled: !backend.busy && backend.stagedSelected.length > 0
                    onClicked: backend.installStaged()
                    ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay
                    ToolTip.text: qsTr("Copy the staged fonts into your font folder  Enter")
                }
            }
        }

        // Body: sidebar, rail, the view, inspector
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                visible: win.sidebarOpen
                Layout.preferredWidth: Theme.wSidebar
                Layout.minimumWidth: Theme.wSidebar
                Layout.fillHeight: true
                onNewCollectionRequested: nameSheet.openNew(qsTr("New collection"))
                onNewSmartRequested: smartSheet.openNew()
                onEditSmartRequested: (id) => smartSheet.openEdit(id)
                onRenameRequested: (id, name) => nameSheet.openRename(id, name)
                onRenameTagRequested: (tag) => nameSheet.openRename("tag:" + tag, tag)
                onDeleteRequested: (id, name, smart) => win.confirmDeleteCollection(id, name, smart)
                onDeleteTagRequested: (tag) => {
                    const facet = backend.tagFacets.find(t => t.name === tag)
                    confirmSheet.ask(qsTr("Remove #%1 from every family?").arg(tag),
                                     qsTr("The tag comes off %1. The fonts themselves are not touched. This cannot be undone.")
                                         .arg(Theme.plural(facet ? facet.count : 0, qsTr("family"), qsTr("families"))),
                                     qsTr("Remove tag"), () => backend.deleteTag(tag))
                }
            }
            Rectangle { visible: win.sidebarOpen; implicitWidth: Theme.hairline; Layout.fillHeight: true; color: Theme.border }

            FamilyRail {
                id: rail
                Layout.preferredWidth: Theme.wNavigator
                Layout.minimumWidth: Theme.wNavigatorMin
                Layout.fillHeight: true
            }
            Rectangle { implicitWidth: Theme.hairline; Layout.fillHeight: true; color: Theme.border }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: backend.staged ? 0 : backend.view === "grid" ? 1 : backend.view === "list" ? 2 : backend.view === "repertoire" ? 3 : backend.view === "compare" ? 4 : 0
                Specimen { id: specimen }
                FamilyGrid { id: familyGrid }
                FamilyList { id: familyList }
                Repertoire { id: repertoire }
                CompareView { id: compareView }
            }
            Rectangle { implicitWidth: Theme.hairline; Layout.fillHeight: true; color: Theme.border }

            Inspector {
                id: inspector
                Layout.preferredWidth: Theme.wInspector
                Layout.minimumWidth: Theme.wInspectorMin
                Layout.fillHeight: true
                onRemoveRequested: removeSheet.open()
                onValidationRequested: validationSheet.open()
                onAddToCollectionRequested: nameSheet.openNew(qsTr("New collection"))
            }
        }

        // Footer
        Rectangle {
            objectName: "statusBar"
            Layout.fillWidth: true
            implicitHeight: Theme.hStatusBar
            color: Theme.windowBg
            Hairline { anchors.top: parent.top; width: parent.width }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s4
                anchors.rightMargin: Theme.s3
                spacing: Theme.s3
                StatusText { text: backend.sourceTitle; color: Theme.textPrimary; font.weight: Theme.wHeading }
                StatusRule {}
                StatusText { text: backend.scanning ? qsTr("Reading fonts…") : Theme.plural(backend.visibleCount, qsTr("family"), qsTr("families")) }
                StatusRule {}
                StatusText { text: qsTr("Yours %1").arg(backend.userCount) }
                StatusRule {}
                StatusText { text: qsTr("System %1").arg(backend.systemCount) }
                StatusRule { visible: backend.readingMetadata }
                StatusText { visible: backend.readingMetadata; text: qsTr("Reading font details…"); color: Theme.textMuted }
                Item { Layout.fillWidth: true }
                BusyIndicator { visible: backend.busy || backend.staging; running: visible; implicitWidth: Theme.szIcon; implicitHeight: Theme.szIcon }
                StatusText {
                    objectName: "statusMessage"
                    text: backend.status
                    visible: text !== ""
                    color: backend.statusIsError ? Theme.danger : Theme.textSecondary
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: win.width / 2
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                StatusRule {}
                StatusText { text: backend.tildePath(backend.fontRoot); color: Theme.textMuted; elide: Text.ElideMiddle; Layout.maximumWidth: Theme.wInspector }
            }
        }
    }

    // Anything droppable, anywhere on the window: files, folders, archives.
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (event) => {
            if (!event.hasUrls) return
            backend.stageUrls(event.urls)
            event.acceptProposedAction()
        }
        Rectangle {
            anchors.fill: parent
            visible: drop.containsDrag
            color: Theme.dropHighlight
            border.width: Theme.selectionRing
            border.color: Theme.accent
            Column {
                anchors.centerIn: parent
                spacing: Theme.s2
                Icon { name: "download"; size: Theme.szIconHero; color: Theme.accent; anchors.horizontalCenter: parent.horizontalCenter }
                Label { text: qsTr("Drop to stage for installing"); color: Theme.textPrimary; font.pixelSize: Theme.fsTitle; font.weight: Theme.wHeading }
            }
        }
    }

    // Anything that deletes files or library data asks first. removeSheet is
    // the family's own remove; confirmSheet serves the rest (Tidy, collections,
    // tags), each caller passing what will go and the action to run.
    Sheet {
        id: removeSheet
        objectName: "removeSheet"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s5 * 2, 460)
        modal: true
        title: qsTr("Remove %1?").arg(backend.selected.name ?? "")
        // Only files in your own font folder are deleted; package files of the
        // same family are not counted, because they are not touched.
        readonly property int fileCount: (backend.selected.files ?? []).filter(f => f.indexOf(backend.fontRoot + "/") === 0).length
        standardButtons: Dialog.Cancel | Dialog.Discard
        Component.onCompleted: standardButton(Dialog.Discard).text = qsTr("Remove")
        onDiscarded: { backend.removeSelected(); close() }
        contentItem: ColumnLayout {
            spacing: Theme.s2
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: qsTr("%1 will be deleted from %2. System fonts are never touched from here. To hide a font without deleting it, deactivate it instead.")
                      .arg(Theme.plural(removeSheet.fileCount, qsTr("file"), qsTr("files")))
                      .arg((backend.selected.directory ?? "").replace(/^\/home\/[^/]+/, "~"))
            }
        }
    }

    Sheet {
        id: confirmSheet
        objectName: "confirmSheet"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s5 * 2, 460)
        modal: true
        property string message: ""
        property var action: null
        function ask(heading, text, button, run) {
            title = heading
            message = text
            action = run
            standardButton(Dialog.Discard).text = button
            open()
        }
        standardButtons: Dialog.Cancel | Dialog.Discard
        onDiscarded: { const run = action; action = null; close(); if (run) run() }
        onRejected: action = null
        contentItem: ColumnLayout {
            spacing: Theme.s2
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: confirmSheet.message
            }
        }
    }
    function confirmDeleteCollection(id, name, smart) {
        confirmSheet.ask(qsTr("Delete %1?").arg(name),
                         smart ? qsTr("The smart collection and its rules are deleted. The fonts it shows are not touched. This cannot be undone.")
                               : qsTr("The collection is deleted. The fonts in it are not touched. This cannot be undone."),
                         qsTr("Delete"),
                         () => smart ? backend.deleteSmartCollection(id) : backend.deleteCollection(id))
    }

    NameSheet {
        id: nameSheet
        onNamed: (id, name) => {
            if (id === "") {
                const family = win.hasFamily ? backend.selectedName : ""
                backend.addCollection(name)
                if (family !== "" && backend.source.indexOf("coll:") === 0) backend.addToCollection(backend.source.substring(5), family)
            } else if (id.indexOf("tag:") === 0) {
                backend.renameTag(id.substring(4), name)
            } else {
                backend.renameCollection(id, name)
            }
        }
    }
    SmartCollectionSheet { id: smartSheet }
    TidySheet { id: tidySheet; onConfirmRequested: (heading, text, button, run) => confirmSheet.ask(heading, text, button, run) }
    ValidationSheet { id: validationSheet }
    HelpSheet { id: helpSheet }
}
