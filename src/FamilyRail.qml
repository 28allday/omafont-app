import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The list of installed families: Yours first, then System, each name set in
// its own face so the rail shows fonts rather than a column of names. Only
// families that set Latin draw themselves; an icon font or a Tamil font would
// otherwise render its name as a row of unrelated symbols.
Rectangle {
    id: rail
    color: Theme.panelBg

    // Tab and every caller land on the list itself, where the arrow keys live.
    function forceActiveFocus() { list.forceActiveFocus() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s3
            implicitHeight: Theme.hNavHeader
            spacing: Theme.s2
            Label { text: backend.sourceTitle; font.weight: Theme.wHeading; color: Theme.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: backend.visibleCount; color: Theme.textMuted; font.pixelSize: Theme.fsLabel }
            Item { Layout.fillWidth: true }
            ToolButton {
                icon.name: "square-plus"
                onClicked: backend.openPicker()
                ToolTip.visible: hovered; ToolTip.delay: Theme.tooltipDelay; ToolTip.text: qsTr("Install fonts…")
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: Theme.hairline; color: Theme.border }

        ListView {
            id: list
            objectName: "familyList"
            activeFocusOnTab: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.families
            currentIndex: backend.families.indexOf(backend.selectedName)
            highlightFollowsCurrentItem: true
            highlightMoveDuration: Theme.dFast
            keyNavigationEnabled: false
            keyNavigationWraps: false
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // Delegates past the viewport stay built, so scrolling does not
            // reload faces on every wheel notch.
            cacheBuffer: Theme.hRailRow * 20

            // Keys move the SELECTION, explicitly. Reacting to currentIndex
            // instead would also fire when a rescan resets the model, and
            // steal the selection from the font that was just installed.
            function step(delta) {
                const i = Math.max(0, Math.min(count - 1, (currentIndex < 0 ? 0 : currentIndex) + delta))
                const name = backend.families.nameAt(i)
                if (name === "") return
                if (backend.staged || backend.staging) backend.clearStage()
                backend.selectedName = name
            }
            Keys.onUpPressed: step(-1)
            Keys.onDownPressed: step(1)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Home) { step(-count); event.accepted = true }
                else if (event.key === Qt.Key_End) { step(count); event.accepted = true }
                else if (event.key === Qt.Key_PageDown) { step(12); event.accepted = true }
                else if (event.key === Qt.Key_PageUp) { step(-12); event.accepted = true }
            }
            Connections {
                target: backend
                function onSelectionChanged() {
                    const i = backend.families.indexOf(backend.selectedName)
                    if (i >= 0) list.positionViewAtIndex(i, ListView.Contain)
                }
            }

            section.property: "group"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                required property string section
                width: ListView.view.width
                height: Theme.hRow
                color: Theme.panelBg
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s4
                    anchors.rightMargin: Theme.s3
                    spacing: Theme.s2
                    Label {
                        text: section === "Yours" ? qsTr("Yours") : qsTr("System")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                        font.weight: Theme.wHeading
                        font.letterSpacing: Theme.capsTracking
                        font.capitalization: Font.AllUppercase
                    }
                    Label {
                        text: section === "Yours" ? backend.userCount : backend.systemCount
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                        visible: backend.filter === ""
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: section === "Yours"
                        text: qsTr("removable")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                    }
                }
            }

            delegate: ItemDelegate {
                id: row
                required property int index
                required property string name
                required property bool user
                required property bool mono
                required property bool latin
                required property bool coverage
                required property bool inactive
                required property bool duplicate
                required property bool starred
                required property var members
                required property string previewFile
                required property var styles
                width: ListView.view.width
                height: Theme.hRailRow
                highlighted: name === backend.selectedName && !backend.staged
                leftPadding: Theme.s4
                rightPadding: Theme.s3
                onClicked: {
                    if (backend.staged || backend.staging) backend.clearStage()
                    backend.selectedName = name
                    list.forceActiveFocus()
                }
                Accessible.name: name

                // The family's own face, loaded from its file rather than by
                // name: Qt snapshots installed families at start-up, so a
                // family installed a minute ago would not resolve by name.
                Loader {
                    id: face
                    active: row.latin && row.previewFile !== ""
                    sourceComponent: FontLoader { source: backend.fileUrl(row.previewFile) }
                }
                readonly property bool faceReady: face.item !== null && face.item.status === FontLoader.Ready

                contentItem: RowLayout {
                    spacing: Theme.s2
                    Rectangle {
                        implicitWidth: Theme.activeUnderline
                        Layout.fillHeight: true
                        Layout.topMargin: Theme.s2
                        Layout.bottomMargin: Theme.s2
                        radius: 1
                        color: row.highlighted ? Theme.accent : "transparent"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        color: row.inactive ? Theme.textMuted : row.highlighted ? Theme.textPrimary : Theme.textSecondary
                        font.family: row.faceReady ? face.item.name : Theme.fontFamily
                        font.pixelSize: row.faceReady ? Theme.fsRailName : Theme.fsControl
                        font.strikeout: row.inactive
                    }
                    Icon { visible: row.starred; name: "star"; size: Theme.szIcon - 2; color: Theme.warning }
                    Label {
                        visible: row.members.length > 0 && !row.mono && !row.coverage
                        text: row.styles.length
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                        font.family: Theme.monoFamily
                        ToolTip.visible: membersHover.hovered
                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.text: qsTr("Includes %1").arg(row.members.join(", "))
                        HoverHandler { id: membersHover }
                    }
                    Icon { visible: row.inactive; name: "eye-off"; size: Theme.szIcon - 2; color: Theme.textMuted }
                    Icon { visible: row.duplicate && !row.inactive; name: "copy"; size: Theme.szIcon - 2; color: Theme.warning }
                    Label {
                        visible: row.coverage
                        text: qsTr("script")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fsCaption
                    }
                    Rectangle {
                        visible: row.mono
                        implicitWidth: Theme.szIcon + 2
                        implicitHeight: Theme.szIcon + 2
                        radius: Theme.rChip
                        color: Theme.withAlpha(Theme.accent, 0.12)
                        border.width: Theme.hairline
                        border.color: Theme.withAlpha(Theme.accent, 0.5)
                        Text {
                            anchors.centerIn: parent
                            text: "M"
                            font.family: Theme.monoFamily
                            font.pixelSize: Theme.fsCaption
                            font.weight: Theme.wHeading
                            color: Theme.accent
                        }
                        ToolTip.visible: monoHover.hovered
                        ToolTip.delay: Theme.tooltipDelay
                        ToolTip.text: qsTr("Monospace")
                        HoverHandler { id: monoHover }
                    }
                }
                background: Rectangle {
                    color: row.down ? Theme.pressedOn(Theme.controlHover)
                         : row.highlighted ? Theme.withAlpha(Theme.accent, 0.10)
                         : row.hovered ? Theme.controlHover : "transparent"
                }
            }

            // What the rail says when there is nothing to list.
            Item {
                anchors.fill: parent
                visible: list.count === 0
                Column {
                    anchors.centerIn: parent
                    width: parent.width - Theme.s5 * 2
                    spacing: Theme.s2
                    Icon { name: "search"; size: Theme.szIconLarge; color: Theme.textMuted; anchors.horizontalCenter: parent.horizontalCenter }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        text: backend.scanning ? qsTr("Reading the font list…")
                            : backend.filter !== "" ? qsTr("Nothing matches “%1”").arg(backend.filter)
                            : qsTr("No fonts found")
                    }
                }
            }
        }
    }
}
