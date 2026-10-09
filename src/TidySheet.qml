import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The Tidy room: everything worth cleaning up, each with a fix. A fix that
// deletes files asks first (Main owns the confirmation sheet); deactivating is
// undone from the Deactivated source, so it does not.
Sheet {
    id: sheet
    objectName: "tidySheet"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.s5 * 2, 760)
    height: Math.min(parent.height - Theme.s5 * 2, 620)
    modal: true
    title: qsTr("Tidy up")
    standardButtons: Dialog.Close
    onAboutToShow: backend.buildTidy()

    Connections {
        target: backend
        function onScanFinished() { if (sheet.visible) backend.buildTidy() }
    }

    signal confirmRequested(string title, string text, string button, var action)

    function under(file) { return file.indexOf(backend.fontRoot + "/") === 0 }
    function isOtf(file) { return file.toLowerCase().endsWith(".otf") }
    function fileList(files) { return files.map(f => f.substring(f.lastIndexOf("/") + 1)).join(", ") }
    // Keep one format of OTF/TTF twins: your copies of the other are deleted,
    // package copies only deactivated (as backend.tidyKeepFormat does).
    function keepFormat(family, files, ext) {
        const going = files.filter(f => under(f) && (ext === "otf" ? !isOtf(f) : isOtf(f)))
        if (going.length === 0) { backend.tidyKeepFormat(family, ext); return }
        confirmRequested(qsTr("Keep the %1 copies of %2?").arg(ext.toUpperCase()).arg(family),
                         qsTr("%1 will be deleted from your font folder: %2. Package copies are only deactivated.")
                             .arg(Theme.plural(going.length, qsTr("file"), qsTr("files"))).arg(fileList(going)),
                         qsTr("Delete"), () => backend.tidyKeepFormat(family, ext))
    }
    function removeFiles(files) {
        const going = files.filter(under)
        confirmRequested(qsTr("Remove %1?").arg(Theme.plural(going.length, qsTr("file"), qsTr("files"))),
                         qsTr("These will be deleted from your font folder: %1. To hide them without deleting, deactivate them instead.").arg(fileList(going)),
                         qsTr("Remove"), () => backend.tidyRemoveFiles(files))
    }

    readonly property int errors: backend.tidyReport.filter(r => r.severity === "error").length
    readonly property int warnings: backend.tidyReport.filter(r => r.severity === "warning").length

    contentItem: ColumnLayout {
        spacing: Theme.s2
        RowLayout {
            spacing: Theme.s2
            Icon {
                name: sheet.errors > 0 ? "circle-alert" : backend.tidyReport.length > 0 ? "brush-cleaning" : "circle-check"
                color: sheet.errors > 0 ? Theme.danger : sheet.warnings > 0 ? Theme.warning : Theme.success
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textPrimary
                font.weight: Theme.wHeading
                text: backend.tidyReport.length === 0 ? qsTr("Nothing to tidy. Every family is in order.")
                    : qsTr("%1 worth a look").arg(Theme.plural(backend.tidyReport.length, qsTr("thing"), qsTr("things")))
            }
            Button { flat: true; icon.name: "refresh-cw"; text: qsTr("Check again"); onClicked: backend.buildTidy() }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.tidyReport
            spacing: Theme.s2
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: column.implicitHeight + Theme.s3 * 2
                radius: Theme.rCard
                color: Theme.panelRaised
                border.width: Theme.hairline
                border.color: Theme.border
                readonly property bool mine: (modelData.files ?? []).length > 0 && modelData.files.every(f => f.indexOf(backend.fontRoot + "/") === 0)
                ColumnLayout {
                    id: column
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.s3
                    spacing: Theme.s2
                    RowLayout {
                        spacing: Theme.s2
                        Icon {
                            name: modelData.severity === "error" ? "circle-alert" : modelData.severity === "warning" ? "copy" : "info"
                            color: modelData.severity === "error" ? Theme.danger : modelData.severity === "warning" ? Theme.warning : Theme.textMuted
                        }
                        Label { Layout.fillWidth: true; text: modelData.title; font.weight: Theme.wHeading; wrapMode: Text.Wrap }
                        Button {
                            flat: true
                            visible: (modelData.family ?? "") !== ""
                            text: qsTr("Show")
                            onClicked: { backend.selectedName = modelData.family; sheet.close() }
                        }
                    }
                    Label { Layout.fillWidth: true; text: modelData.detail; color: Theme.textSecondary; font.pixelSize: Theme.fsLabel; wrapMode: Text.Wrap }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        // OTF/TTF twins: keep one format.
                        Button { visible: modelData.kind === "twins"; enabled: !backend.busy; text: qsTr("Keep OTF"); onClicked: sheet.keepFormat(modelData.family, modelData.files ?? [], "otf") }
                        Button { visible: modelData.kind === "twins"; enabled: !backend.busy; text: qsTr("Keep TTF"); onClicked: sheet.keepFormat(modelData.family, modelData.files ?? [], "ttf") }
                        // Static beside variable: the static copies are deactivated, not deleted.
                        Button { visible: modelData.kind === "variable"; enabled: !backend.busy; text: qsTr("Keep the variable font"); onClicked: backend.tidyKeepVariable(modelData.family) }
                        // Broken files and stray non-Latin fonts.
                        Button { visible: modelData.kind === "invalid" || modelData.kind === "nolatin"; enabled: !backend.busy; icon.name: "eye-off"; text: qsTr("Deactivate"); onClicked: backend.tidyDeactivateFiles(modelData.files) }
                        Button { visible: (modelData.kind === "invalid" || modelData.kind === "nolatin") && row.mine; enabled: !backend.busy; icon.name: "trash-2"; text: qsTr("Remove…"); onClicked: sheet.removeFiles(modelData.files ?? []) }
                        Label {
                            visible: modelData.kind === "twins" || modelData.kind === "variable"
                            text: modelData.kind === "variable" ? qsTr("The static copies are deactivated, not deleted.")
                                : row.mine ? qsTr("Your copies are deleted, after asking; package copies are deactivated.")
                                : qsTr("Package copies are deactivated, never deleted.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fsCaption
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
