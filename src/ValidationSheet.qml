import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omafont 1.0

// The validation report: one row per file, with what is wrong or worth knowing.
Sheet {
    id: sheet
    objectName: "validationSheet"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent.width - Theme.s5 * 2, 720)
    height: Math.min(parent.height - Theme.s5 * 2, 560)
    modal: true
    title: backend.validationTitle
    standardButtons: Dialog.Close
    onClosed: backend.clearValidation()

    readonly property int errors: backend.validationReport.filter(r => r.status === "error").length
    readonly property int warnings: backend.validationReport.filter(r => r.status === "warning").length

    contentItem: ColumnLayout {
        spacing: Theme.s2
        RowLayout {
            spacing: Theme.s2
            Icon { name: sheet.errors > 0 ? "circle-alert" : "circle-check"; color: sheet.errors > 0 ? Theme.danger : sheet.warnings > 0 ? Theme.warning : Theme.success }
            Label {
                text: sheet.errors > 0 ? qsTr("Serious problems found in %1 of %2").arg(sheet.errors).arg(Theme.plural(backend.validationReport.length, qsTr("file"), qsTr("files")))
                    : sheet.warnings > 0 ? qsTr("Minor problems found in %1 of %2").arg(sheet.warnings).arg(Theme.plural(backend.validationReport.length, qsTr("file"), qsTr("files")))
                    : qsTr("No problems found in %1").arg(Theme.plural(backend.validationReport.length, qsTr("file"), qsTr("files")))
                color: Theme.textPrimary
                font.weight: Theme.wHeading
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.validationReport
            spacing: Theme.s1
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: column.implicitHeight + Theme.s3 * 2
                radius: Theme.rControl
                color: Theme.panelRaised
                border.width: Theme.hairline
                border.color: Theme.border
                ColumnLayout {
                    id: column
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.s3
                    spacing: Theme.s1
                    RowLayout {
                        spacing: Theme.s2
                        Icon {
                            name: modelData.status === "error" ? "circle-alert" : modelData.status === "warning" ? "info" : "circle-check"
                            color: modelData.status === "error" ? Theme.danger : modelData.status === "warning" ? Theme.warning : Theme.success
                        }
                        Label { text: modelData.name; font.weight: Theme.wHeading; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Label { text: (modelData.kind ?? "") + (modelData.glyphCount > 0 ? " · " + Theme.plural(modelData.glyphCount, qsTr("glyph"), qsTr("glyphs")) : ""); color: Theme.textMuted; font.pixelSize: Theme.fsLabel }
                    }
                    Repeater {
                        model: modelData.problems
                        Label { required property string modelData; text: "✕  " + modelData; color: Theme.danger; font.pixelSize: Theme.fsLabel; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                    Repeater {
                        model: modelData.warnings
                        Label { required property string modelData; text: "!  " + modelData; color: Theme.warning; font.pixelSize: Theme.fsLabel; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                    Label { visible: modelData.status === "ok"; text: qsTr("Passed every check"); color: Theme.textMuted; font.pixelSize: Theme.fsLabel }
                }
            }
        }
    }
}
