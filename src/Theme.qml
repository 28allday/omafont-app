pragma Singleton

import QtQuick
import Omafont 1.0

// The single source of every colour, dimension, radius, duration and type size
// in OmaFont. Nothing outside this file may contain a colour literal or a magic
// dimension. The palette is the shared Oma design system, and it is FIXED:
// a surface that judges type must stay colour-stable whatever the desktop is
// doing, because a surround that changes hue changes how the letterforms read.
//
// The one exception is the ACCENT, which follows the Omarchy theme. Buttons,
// selection and focus are chrome and belong to the desktop. The specimen is not
// chrome and never takes an interface colour but the ink.
QtObject {
    id: theme

    readonly property string appName: "OmaFont"

    // ── Colour ──────────────────────────────────────────────────────────────
    readonly property color windowBg: "#0B0E13"
    readonly property color panelBg: "#111722"
    readonly property color panelRaised: "#171E2A"
    readonly property color controlBg: "#1B2431"
    readonly property color controlHover: "#222D3D"
    readonly property color border: highContrast ? "#6E7C92" : "#293241"
    readonly property color borderStrong: highContrast ? "#9AA8BD" : "#3A4658"

    readonly property color textPrimary: "#E7EDF7"
    readonly property color textSecondary: highContrast ? "#D2DAE6" : "#A7B1C1"
    readonly property color textMuted: highContrast ? "#AFB9C8" : "#727E90"

    // The desktop's accent when Omarchy is there, the Oma accent when it is not
    // (plain Arch, a container, CI) — and the app has to look finished either way.
    readonly property color omaAccent: "#27C2FF"
    readonly property color accent: OmarchyTheme.accentFollowed ? OmarchyTheme.accent : omaAccent
    readonly property color accentPressed: Qt.darker(accent, 1.35)
    readonly property color warning: "#FBBF24"
    readonly property color danger: "#EF4444"
    readonly property color success: "#34D399"

    // Derived: an accent can be pale gold or deep violet and the label on top
    // has to read on both, so Rec.709 luminance decides near-black or near-white.
    readonly property color accentText:
        (0.2126 * accent.r + 0.7152 * accent.g + 0.0722 * accent.b) > 0.55 ? "#04121A" : "#F2F7FF"

    function hovered(base) { return Qt.lighter(base, 1.06) }
    function pressedOn(base) { return Qt.darker(base, 1.08) }
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    // "1 style" / "18 styles": no translator is loaded, so %n forms would
    // print their "(s)" literally.
    function plural(n, one, many) { return n + " " + (n === 1 ? one : many) }
    readonly property real disabledOpacity: 0.38

    // ── The specimen surface ────────────────────────────────────────────────
    // Darker than the panels so the type reads as the object and the surround
    // as nothing; the ink is the primary text colour, never the accent.
    readonly property color pasteboard: "#1A1F26"
    readonly property color showBg: "#000000"
    readonly property color specimenInk: textPrimary
    readonly property color dropHighlight: withAlpha(accent, 0.10)

    // ── Spacing ─────────────────────────────────────────────────────────────
    readonly property int s1: Math.round(4 * densityScale)
    readonly property int s2: Math.round(8 * densityScale)
    readonly property int s3: Math.round(12 * densityScale)
    readonly property int s4: Math.round(16 * densityScale)
    readonly property int s5: Math.round(24 * densityScale)
    readonly property int s6: Math.round(40 * densityScale)

    // ── Radius ──────────────────────────────────────────────────────────────
    readonly property int rControl: 4
    readonly property int rMenu: 6
    readonly property int rCard: 6
    readonly property int rPanel: 0
    readonly property int rChip: 3

    // ── Lines ───────────────────────────────────────────────────────────────
    readonly property int hairline: 1
    readonly property int focusRing: 1
    readonly property int selectionRing: 2
    readonly property int activeUnderline: 2

    // ── Density ─────────────────────────────────────────────────────────────
    property string density: "compact"
    readonly property real densityScale: density === "comfortable" ? 1.25
                                       : density === "normal" ? 1.12
                                       : 1.0

    // ── Type ────────────────────────────────────────────────────────────────
    readonly property string fontFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("Inter") >= 0) return "Inter"
        if (families.indexOf("Noto Sans") >= 0) return "Noto Sans"
        return "sans-serif"
    }
    readonly property string monoFamily: {
        const families = Qt.fontFamilies()
        if (families.indexOf("JetBrainsMono Nerd Font") >= 0) return "JetBrainsMono Nerd Font"
        if (families.indexOf("Noto Sans Mono") >= 0) return "Noto Sans Mono"
        return "monospace"
    }

    readonly property int fsBase: 12
    readonly property int fsLabel: 12
    readonly property int fsControl: 13
    readonly property int fsHeading: 13
    readonly property int fsTitle: 15
    readonly property int fsSection: 14
    readonly property int fsCaption: 10
    readonly property int wHeading: Font.DemiBold
    readonly property int wNormal: Font.Normal
    readonly property real capsTracking: 0.8

    // The specimen's own scale: the display name, then the five labelled lines.
    readonly property int fsSpecimenName: 64
    readonly property var specimenSizes: [56, 34, 24, 17, 12]
    readonly property int fsSpecimenCharset: 20
    readonly property int fsRailName: 15

    // ── Control metrics ─────────────────────────────────────────────────────
    readonly property int hTitleBar: 34
    readonly property int hToolbar: Math.round(48 * densityScale)
    readonly property int hStatusBar: 28
    readonly property int hControl: Math.round(28 * densityScale)
    readonly property int hToolButton: Math.round(34 * densityScale)
    readonly property int hRow: Math.round(30 * densityScale)
    readonly property int hRailRow: Math.round(36 * densityScale)
    readonly property int hCardHeader: Math.round(36 * densityScale)
    readonly property int hNavHeader: Math.round(36 * densityScale)
    readonly property int hToolTile: Math.round(58 * densityScale)
    readonly property int wSidebar: 208
    readonly property int wFactLabel: 88
    readonly property int wNavigator: 272
    readonly property int wNavigatorMin: 200
    readonly property int wInspector: 320
    readonly property int wInspectorMin: 280
    readonly property int wSearch: 280
    readonly property int wSpecimenGutter: 44
    readonly property int szIcon: 16
    readonly property int szIconLarge: 20
    readonly property int szIconHero: 40
    readonly property int szIconHit: Math.round(28 * densityScale)
    readonly property int szStatusDot: 8
    readonly property int wSplitter: 4

    // ── Motion ──────────────────────────────────────────────────────────────
    readonly property bool reducedMotion: backend.reducedMotion
    readonly property bool highContrast: backend.highContrast
    readonly property int dFast: reducedMotion ? 0 : 100
    readonly property int dNormal: reducedMotion ? 0 : 130
    readonly property int dSlow: 160
    readonly property int easing: Easing.OutCubic
    readonly property int tooltipDelay: 450
}
