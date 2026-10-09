import QtQuick
import QtQuick.Templates as T
import Omafont 1.0

// Plain text always: labels show names and prose read out of font files, and
// AutoText would render (and fetch images for) anything that looks like HTML.
T.Label {
    id: control
    textFormat: Text.PlainText
    color: Theme.textPrimary
    linkColor: Theme.accent
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fsControl
}
