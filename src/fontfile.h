#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// What a font FILE says about itself, read straight from its sfnt tables.
// fontconfig knows families, styles, languages and glyph coverage; it does not
// carry the designer, the copyright, the licence or the glyph count, and those
// are the lines the Info pane is for. So: a small reader for `name`, `maxp`,
// `head`, `OS/2` and `post`, plus the structural checks that make up Validate.
struct FontFileInfo {
    bool ok = false;               // parsed as a font file at all
    QString error;                 // why not, when !ok
    QStringList problems;          // validation findings that make the font unsafe to install
    QStringList warnings;          // findings worth knowing about, not blocking

    QString kind;                  // "TrueType", "OpenType PostScript", "TrueType Collection", "OpenType PostScript Collection"
    int faceCount = 1;             // >1 for a collection
    int glyphCount = 0;
    int unitsPerEm = 0;

    // The `name` table, English where there is a choice.
    QString copyright;             // 0
    QString family;                // 1 (16 preferred)
    QString subfamily;             // 2 (17 preferred)
    QString uniqueId;              // 3
    QString fullName;              // 4
    QString version;               // 5
    QString postscriptName;        // 6
    QString trademark;             // 7
    QString manufacturer;          // 8
    QString designer;              // 9
    QString description;           // 10
    QString vendorUrl;             // 11
    QString designerUrl;           // 12
    QString license;               // 13
    QString licenseUrl;            // 14

    // OS/2
    int weightClass = 0;           // 100..900
    int widthClass = 0;            // 1..9, 5 normal
    int fsType = 0;                // embedding permissions
    QString vendorId;              // achVendID
    bool bold = false;
    bool italic = false;
    int familyClass = 0;           // sFamilyClass high byte
    QByteArray panose;             // 10 bytes
    // post
    bool fixedPitch = false;
    // tables present
    QStringList tables;
    bool variable = false;         // fvar
    bool color = false;            // COLR / CBDT / sbix / SVG

    // Derived, for people.
    QString classification() const;   // "Serif", "Sans-serif", "Handwriting", "Ornamental", "Symbolic", "Unknown"
    QString embedding() const;        // what fsType allows, in words
    QStringList traits() const;       // Bold, Italic, Monospaced, Condensed, Expanded, Variable, Colour
};

namespace FontFile {
// Reads one face of a file. `faceIndex` picks a face in a collection.
FontFileInfo read(const QString &path, int faceIndex = 0);
// The same, from bytes already in memory (tests, and collections).
FontFileInfo parse(const QByteArray &bytes, int faceIndex = 0);
// OpenType script tag → name, e.g. "latn" → "Latin"; the tag itself when unknown.
QString scriptName(const QString &tag);
// fontconfig language code → name, e.g. "ta" → "Tamil"; the code when unknown.
QString languageName(const QString &code);
// OS/2 usWeightClass → "Thin" … "Black".
QString weightName(int weightClass);
// OS/2 usWidthClass → "Ultra-condensed" … "Ultra-expanded".
QString widthName(int widthClass);
}
