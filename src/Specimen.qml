import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The type specimen: the name at display size, the sample line at five labelled
// sizes, and the character set. It renders from the font FILE, never from an
// installed family name, so a staged-but-uninstalled font and a just-installed
// one both preview correctly: Qt only snapshots families at start-up.
Rectangle {
    id: specimen
    color: Theme.pasteboard

    readonly property string file: backend.previewFile
    Loader {
        id: loader
        active: specimen.file !== ""
        sourceComponent: FontLoader { source: backend.fileUrl(specimen.file) }
    }
    readonly property bool ready: loader.item !== null && loader.item.status === FontLoader.Ready
    readonly property string family: ready ? loader.item.name : ""
    readonly property bool failed: loader.item !== null && loader.item.status === FontLoader.Error

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: Theme.s6
        anchors.rightMargin: Theme.s6
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.s6 * 2
        clip: true
        visible: specimen.ready
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            y: Theme.s6
            width: flick.width
            spacing: Theme.s4

            // Name at display size, in the face itself.
            Text {
                Layout.fillWidth: true
                text: backend.previewTitle
                textFormat: Text.PlainText
                font.family: specimen.family
                font.pixelSize: Math.round(Theme.fsSpecimenName * backend.specimenScale)
                color: Theme.specimenInk
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            RowLayout {
                spacing: Theme.s2
                Label {
                    text: backend.staged
                        ? qsTr("%1 staged from %2").arg(Theme.plural(backend.stagedSelected.length, qsTr("face"), qsTr("faces"))).arg(backend.stagedSource)
                        : Theme.plural((backend.selected.styles ?? []).length, qsTr("style"), qsTr("styles"))
                    color: Theme.textMuted
                }
                Label { visible: specimen.family !== "" && specimen.family !== backend.previewTitle; text: "·  " + specimen.family; color: Theme.textMuted }
            }

            // The sample line is editable: a font is chosen against the words
            // it will actually set.
            TextField {
                id: sampleField
                objectName: "sampleField"
                Layout.fillWidth: true
                Layout.topMargin: Theme.s3
                text: backend.sample
                placeholderText: qsTr("Type a sample…")
                font.family: Theme.fontFamily
                onEditingFinished: backend.sample = text
                onTextEdited: backend.sample = text
                Keys.onEscapePressed: { focus = false; specimen.forceActiveFocus() }
                background: Rectangle {
                    implicitHeight: Theme.hControl
                    radius: Theme.rControl
                    color: sampleField.activeFocus ? Theme.controlBg : "transparent"
                    border.width: Theme.hairline
                    border.color: sampleField.activeFocus ? Theme.accent : sampleField.hovered ? Theme.borderStrong : Theme.border
                }
            }

            // Five sizes, labelled in a left gutter so the pane reads as a
            // specimen rather than five unexplained repetitions of a sentence.
            Repeater {
                model: Theme.specimenSizes
                RowLayout {
                    required property int modelData
                    Layout.fillWidth: true
                    spacing: Theme.s3
                    Label {
                        Layout.preferredWidth: Theme.wSpecimenGutter
                        Layout.alignment: Qt.AlignTop
                        text: Math.round(modelData * backend.specimenScale)
                        color: Theme.textMuted
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fsCaption
                        horizontalAlignment: Text.AlignRight
                        topPadding: Theme.s1
                    }
                    Text {
                        Layout.fillWidth: true
                        text: backend.sample === "" ? backend.defaultSample : backend.sample
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.family: specimen.family
                        font.pixelSize: Math.round(modelData * backend.specimenScale)
                        color: Theme.specimenInk
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.topMargin: Theme.s3; implicitHeight: Theme.hairline; color: Theme.border }

            // The character set.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s3
                Label {
                    Layout.preferredWidth: Theme.wSpecimenGutter
                    Layout.alignment: Qt.AlignTop
                    text: "Aa"
                    color: Theme.textMuted
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fsCaption
                    horizontalAlignment: Text.AlignRight
                    topPadding: Theme.s1
                }
                Text {
                    Layout.fillWidth: true
                    text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789\n!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n£€¥©®™ àáâäåæçèéêëìíîïñòóôöøùúûüß"
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    lineHeight: 1.35
                    font.family: specimen.family
                    font.pixelSize: Math.round(Theme.fsSpecimenCharset * backend.specimenScale)
                    color: Theme.specimenInk
                }
            }
        }
    }

    // Empty states: nothing chosen, still loading, or a file Qt could not read.
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.s6 * 2, 420)
        spacing: Theme.s2
        visible: !specimen.ready
        Icon {
            name: specimen.failed ? "circle-alert" : backend.staging ? "package" : "type"
            size: Theme.szIconHero
            color: specimen.failed ? Theme.warning : Theme.textMuted
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            font.pixelSize: Theme.fsTitle
            font.weight: Theme.wHeading
            text: specimen.failed ? qsTr("This file would not load")
                : backend.staging ? qsTr("Reading %1…").arg(backend.stagedSource)
                : backend.scanning ? qsTr("Reading your fonts…")
                : qsTr("Choose a family, or drop fonts here")
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.textMuted
            text: specimen.failed ? qsTr("fontconfig knows it, but Qt could not render it. It may be a bitmap or Type 1 font.")
                : backend.staging || backend.scanning ? ""
                : qsTr("A file, a folder or a zip. Everything installable inside is staged and previewed before anything is written.")
        }
    }
}
