import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The library sidebar: where the families come from. Fixed sources first
// (All, Yours, System, Monospace, Recently Added, Deactivated, Duplicates), a
// language picker, then the collections and smart collections you make.
Rectangle {
    id: sidebar
    color: Theme.panelBg

    signal newCollectionRequested()
    signal newSmartRequested()
    signal editSmartRequested(string id)
    signal renameRequested(string id, string name)
    signal renameTagRequested(string tag)
    // Deleting library data cannot be undone, so Main asks first.
    signal deleteRequested(string id, string name, bool smart)
    signal deleteTagRequested(string tag)

    component SourceRow: ItemDelegate {
        id: row
        property string source: ""
        property string label: ""
        property string iconName: ""
        property int count: -1
        property bool editable: false
        // Full width in the sidebar's ColumnLayout, which overrides `width`.
        Layout.fillWidth: true
        width: ListView.view ? ListView.view.width : parent.width
        height: Theme.hRow
        highlighted: backend.source === source
        leftPadding: Theme.s4
        rightPadding: Theme.s3
        onClicked: backend.source = source
        Accessible.name: label
        contentItem: RowLayout {
            spacing: Theme.s2
            Icon { name: row.iconName; size: Theme.szIcon; color: row.highlighted ? Theme.accent : Theme.textMuted; visible: row.iconName !== "" }
            Text {
                Layout.fillWidth: true
                text: row.label
                textFormat: Text.PlainText
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: row.highlighted ? Theme.textPrimary : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fsControl
            }
            Label {
                visible: row.count >= 0
                text: row.count
                color: Theme.textMuted
                font.pixelSize: Theme.fsCaption
                font.family: Theme.monoFamily
            }
        }
        background: Rectangle {
            radius: Theme.rControl
            color: row.down ? Theme.pressedOn(Theme.controlHover)
                 : row.highlighted ? Theme.withAlpha(Theme.accent, 0.10)
                 : row.hovered ? Theme.controlHover : "transparent"
        }
    }
    component Heading: RowLayout {
        property string text: ""
        property string action: ""
        signal actionClicked()
        Layout.fillWidth: true
        Layout.leftMargin: Theme.s4
        Layout.rightMargin: Theme.s2
        Layout.topMargin: Theme.s3
        spacing: Theme.s2
        Label {
            text: parent.text
            color: Theme.textMuted
            font.pixelSize: Theme.fsCaption
            font.weight: Theme.wHeading
            font.letterSpacing: Theme.capsTracking
            font.capitalization: Font.AllUppercase
        }
        Item { Layout.fillWidth: true }
        ToolButton {
            visible: parent.action !== ""
            icon.name: "plus"
            onClicked: parent.actionClicked()
            ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: parent.action
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.s4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 0

            Item { implicitHeight: Theme.s2 }
            Heading { text: qsTr("Library") }
            SourceRow { source: "all"; label: qsTr("All Fonts"); iconName: "type"; count: backend.totalCount }
            SourceRow { source: "yours"; label: qsTr("Yours"); iconName: "download"; count: backend.userCount }
            SourceRow { source: "system"; label: qsTr("System"); iconName: "package"; count: backend.systemCount }
            SourceRow { source: "starred"; label: qsTr("Starred"); iconName: "star"; count: backend.starredCount; visible: backend.starredCount > 0 || backend.source === "starred" }
            SourceRow { source: "mono"; label: qsTr("Monospace"); iconName: "terminal"; count: backend.monoCount }
            SourceRow { source: "recent"; label: qsTr("Recently Added"); iconName: "clock"; count: backend.recentCount }
            SourceRow { source: "inactive"; label: qsTr("Deactivated"); iconName: "eye-off"; count: backend.inactiveCount; visible: backend.inactiveCount > 0 || backend.source === "inactive" }
            SourceRow { source: "duplicates"; label: qsTr("Duplicates"); iconName: "copy"; count: backend.duplicateCount; visible: backend.duplicateCount > 0 || backend.source === "duplicates" }

            Heading { text: qsTr("By design") }
            Repeater {
                model: backend.designFacets
                SourceRow {
                    required property var modelData
                    source: modelData.key
                    label: modelData.name
                    iconName: modelData.key === "mono" ? "terminal" : modelData.key.indexOf("trait:") === 0 ? "sliders-horizontal" : "type"
                    count: modelData.count
                    Layout.fillWidth: true
                }
            }
            Label {
                visible: backend.designFacets.length === 0
                Layout.leftMargin: Theme.s4; Layout.rightMargin: Theme.s3; Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: backend.readingMetadata ? qsTr("Reading the files…") : qsTr("Nothing classified yet.")
                color: Theme.textMuted
                font.pixelSize: Theme.fsCaption
            }

            Heading { text: qsTr("Writing systems") }
            ComboBox {
                id: scriptBox
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                model: [{ key: "", name: qsTr("Any script"), count: -1 }].concat(backend.scriptFacets)
                textRole: "name"
                valueRole: "key"
                displayText: backend.source.indexOf("script:") === 0 ? backend.sourceTitle : qsTr("Any script")
                onActivated: (index) => backend.source = model[index].key === "" ? "all" : model[index].key
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: scriptBox.width
                    text: modelData.name + (modelData.count >= 0 ? "  ·  " + modelData.count : "")
                    highlighted: scriptBox.highlightedIndex === index
                }
            }

            Heading { text: qsTr("Language") }
            ComboBox {
                id: languageBox
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                model: [{ code: "", name: qsTr("Any language"), count: -1 }].concat(backend.languages)
                textRole: "name"
                valueRole: "code"
                displayText: backend.source.indexOf("lang:") === 0 ? backend.sourceTitle : qsTr("Any language")
                onActivated: (index) => {
                    const code = model[index].code
                    backend.source = code === "" ? "all" : "lang:" + code
                }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: languageBox.width
                    text: modelData.name + (modelData.count >= 0 ? "  ·  " + modelData.count : "")
                    highlighted: languageBox.highlightedIndex === index
                }
                popup.contentItem.implicitHeight: Math.min(400, languageBox.count * Theme.hRow)
            }

            Heading { text: qsTr("Tags") }
            Flow {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                spacing: Theme.s1
                Repeater {
                    model: backend.tagFacets
                    Button {
                        required property var modelData
                        text: "#" + modelData.name + "  " + modelData.count
                        flat: backend.source !== modelData.key
                        checked: backend.source === modelData.key
                        font.pixelSize: Theme.fsLabel
                        onClicked: backend.source = backend.source === modelData.key ? "all" : modelData.key
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: tagMenu.openFor(modelData.name) }
                    }
                }
            }
            Label {
                visible: backend.tagFacets.length === 0
                Layout.leftMargin: Theme.s4; Layout.rightMargin: Theme.s3; Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Tag a family in the Info pane: display, body, a client, a licence…")
                color: Theme.textMuted
                font.pixelSize: Theme.fsCaption
            }

            Heading { text: qsTr("Collections"); action: qsTr("New collection"); onActionClicked: sidebar.newCollectionRequested() }
            Repeater {
                model: backend.collections
                SourceRow {
                    required property var modelData
                    source: "coll:" + modelData.id
                    label: modelData.name
                    iconName: "folder-open"
                    count: modelData.count
                    editable: true
                    Layout.fillWidth: true
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: collectionMenu.openFor(modelData.id, modelData.name, false)
                    }
                }
            }
            Label {
                visible: backend.collections.length === 0
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Group families by hand. Pick a family, then Font ▸ Add to collection.")
                color: Theme.textMuted
                font.pixelSize: Theme.fsCaption
            }

            Heading { text: qsTr("Smart Collections"); action: qsTr("New smart collection"); onActionClicked: sidebar.newSmartRequested() }
            Repeater {
                model: backend.smartCollections
                SourceRow {
                    required property var modelData
                    source: "smart:" + modelData.id
                    label: modelData.name
                    iconName: "search"
                    count: modelData.count
                    Layout.fillWidth: true
                    onDoubleClicked: sidebar.editSmartRequested(modelData.id)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: collectionMenu.openFor(modelData.id, modelData.name, true)
                    }
                }
            }
            Label {
                visible: backend.smartCollections.length === 0
                Layout.leftMargin: Theme.s4
                Layout.rightMargin: Theme.s3
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Rules that pick families for you: by designer, kind, language, weight…")
                color: Theme.textMuted
                font.pixelSize: Theme.fsCaption
            }
        }
    }

    Menu {
        id: tagMenu
        property string tag: ""
        function openFor(t) { tag = t; popup() }
        MenuItem { text: qsTr("Rename tag…"); onTriggered: sidebar.renameTagRequested(tagMenu.tag) }
        MenuItem { text: qsTr("Remove tag from every family…"); onTriggered: sidebar.deleteTagRequested(tagMenu.tag) }
    }
    Menu {
        id: collectionMenu
        property string targetId: ""
        property string targetName: ""
        property bool smart: false
        function openFor(id, name, isSmart) { targetId = id; targetName = name; smart = isSmart; popup() }
        MenuItem { text: qsTr("Edit rules…"); visible: collectionMenu.smart; height: visible ? implicitHeight : 0; onTriggered: sidebar.editSmartRequested(collectionMenu.targetId) }
        MenuItem { text: qsTr("Activate everything in it"); icon.name: "eye"; visible: !collectionMenu.smart; height: visible ? implicitHeight : 0; onTriggered: backend.activateCollection(collectionMenu.targetId) }
        MenuItem { text: qsTr("Deactivate everything in it"); icon.name: "eye-off"; visible: !collectionMenu.smart; height: visible ? implicitHeight : 0; onTriggered: backend.deactivateCollection(collectionMenu.targetId) }
        MenuItem { text: qsTr("Export…"); icon.name: "folder-output"; visible: !collectionMenu.smart; height: visible ? implicitHeight : 0; onTriggered: backend.exportCollection(collectionMenu.targetId) }
        MenuItem { text: qsTr("Rename…"); onTriggered: sidebar.renameRequested(collectionMenu.targetId, collectionMenu.targetName) }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Delete…")
            icon.name: "trash-2"
            onTriggered: sidebar.deleteRequested(collectionMenu.targetId, collectionMenu.targetName, collectionMenu.smart)
        }
    }
}
