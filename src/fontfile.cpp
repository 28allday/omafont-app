#include "fontfile.h"

#include <QFile>
#include <QHash>
#include <QLocale>
#include <QtEndian>

namespace {

constexpr qint64 kMaxFontBytes = 256LL * 1024 * 1024;

quint32 u32(const QByteArray &b, qsizetype at) {
    if (at < 0 || at + 4 > b.size()) return 0;
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + at));
}
quint16 u16(const QByteArray &b, qsizetype at) {
    if (at < 0 || at + 2 > b.size()) return 0;
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + at));
}
qint16 s16(const QByteArray &b, qsizetype at) { return static_cast<qint16>(u16(b, at)); }

struct Table {
    QString tag;
    quint32 offset = 0;
    quint32 length = 0;
};

QString tagAt(const QByteArray &b, qsizetype at) {
    if (at + 4 > b.size()) return {};
    return QString::fromLatin1(b.constData() + at, 4);
}

// Mac Roman, for name records on platform 1. Only the upper half differs.
QString fromMacRoman(const QByteArray &bytes) {
    static const char16_t upper[128] = {
        0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
        0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
        0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
        0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
        0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
        0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
        0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
        0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,
    };
    QString out;
    out.reserve(bytes.size());
    for (unsigned char c : bytes) out.append(c < 128 ? QChar(c) : QChar(upper[c - 128]));
    return out;
}

QString fromUtf16BE(const QByteArray &bytes) {
    QString out;
    out.reserve(bytes.size() / 2);
    for (qsizetype i = 0; i + 1 < bytes.size(); i += 2)
        out.append(QChar(quint16((uchar(bytes[i]) << 8) | uchar(bytes[i + 1]))));
    return out;
}

// Rank a name record: Windows English first, then any Windows Unicode, then
// Mac English, then whatever is there.
int nameRank(quint16 platform, quint16 encoding, quint16 language) {
    if (platform == 3 && (encoding == 1 || encoding == 10) && language == 0x409) return 0;
    if (platform == 3 && (encoding == 1 || encoding == 10)) return 1;
    if (platform == 0) return 2;
    if (platform == 1 && encoding == 0 && language == 0) return 3;
    if (platform == 1 && encoding == 0) return 4;
    return 9;
}

void readNames(const QByteArray &b, const Table &name, FontFileInfo &info) {
    if (name.length < 6) { info.warnings.append(QStringLiteral("The name table is too short to read")); return; }
    const qsizetype base = name.offset;
    const quint16 count = u16(b, base + 2);
    const quint16 stringOffset = u16(b, base + 4);
    QHash<int, QPair<int, QString>> best;  // id → (rank, text)
    for (quint16 i = 0; i < count; ++i) {
        const qsizetype rec = base + 6 + qsizetype(i) * 12;
        if (rec + 12 > b.size()) break;
        const quint16 platform = u16(b, rec), encoding = u16(b, rec + 2), language = u16(b, rec + 4);
        const quint16 id = u16(b, rec + 6), length = u16(b, rec + 8), offset = u16(b, rec + 10);
        if (id > 25) continue;
        const qsizetype at = base + stringOffset + offset;
        if (at < 0 || at + length > b.size() || at + length > qsizetype(name.offset) + name.length) continue;
        const QByteArray raw = b.mid(at, length);
        QString text;
        if (platform == 3 || platform == 0) text = fromUtf16BE(raw);
        else if (platform == 1 && encoding == 0) text = fromMacRoman(raw);
        else continue;
        text = text.trimmed();
        if (text.isEmpty()) continue;
        const int rank = nameRank(platform, encoding, language);
        if (!best.contains(id) || rank < best[id].first) best[id] = {rank, text};
    }
    auto get = [&best](int id) { return best.contains(id) ? best[id].second : QString(); };
    info.copyright = get(0);
    info.family = !get(16).isEmpty() ? get(16) : get(1);
    info.subfamily = !get(17).isEmpty() ? get(17) : get(2);
    info.uniqueId = get(3);
    info.fullName = get(4);
    info.version = get(5);
    info.postscriptName = get(6);
    info.trademark = get(7);
    info.manufacturer = get(8);
    info.designer = get(9);
    info.description = get(10);
    info.vendorUrl = get(11);
    info.designerUrl = get(12);
    info.license = get(13);
    info.licenseUrl = get(14);
}

}  // namespace

QString FontFileInfo::classification() const {
    // PANOSE first: byte 0 is the family kind, byte 1 the serif style.
    if (panose.size() >= 2) {
        const int kind = uchar(panose[0]);
        const int serif = uchar(panose[1]);
        if (kind == 2 && serif >= 11 && serif <= 15) return QStringLiteral("Sans-serif");
        if (kind == 2 && serif >= 2) return QStringLiteral("Serif");
        if (kind == 3) return QStringLiteral("Handwriting");
        if (kind == 4) return QStringLiteral("Ornamental");
        if (kind == 5) return QStringLiteral("Symbolic");
    }
    switch (familyClass) {
    case 1: case 2: case 3: case 4: case 5: case 7: return QStringLiteral("Serif");
    case 8: return QStringLiteral("Sans-serif");
    case 9: return QStringLiteral("Ornamental");
    case 10: return QStringLiteral("Handwriting");
    case 12: return QStringLiteral("Symbolic");
    }
    return QStringLiteral("Unknown");
}

QString FontFileInfo::embedding() const {
    const int type = fsType & 0x000F;
    QString base;
    if (type == 0) base = QStringLiteral("No embedding restrictions");
    else if (type & 0x0002) base = QStringLiteral("Restricted: may not be embedded");
    else if (type & 0x0004) base = QStringLiteral("Preview and print only");
    else if (type & 0x0008) base = QStringLiteral("Editable embedding");
    else base = QStringLiteral("Installable");
    QStringList extra;
    if (fsType & 0x0100) extra.append(QStringLiteral("no subsetting"));
    if (fsType & 0x0200) extra.append(QStringLiteral("bitmaps only"));
    return extra.isEmpty() ? base : base + QStringLiteral(" (") + extra.join(QStringLiteral(", ")) + QLatin1Char(')');
}

QStringList FontFileInfo::traits() const {
    QStringList out;
    if (bold || weightClass >= 600) out.append(QStringLiteral("Bold"));
    if (italic) out.append(QStringLiteral("Italic"));
    if (fixedPitch) out.append(QStringLiteral("Monospaced"));
    if (widthClass > 0 && widthClass < 5) out.append(QStringLiteral("Condensed"));
    if (widthClass > 5) out.append(QStringLiteral("Expanded"));
    if (variable) out.append(QStringLiteral("Variable"));
    if (color) out.append(QStringLiteral("Colour glyphs"));
    return out;
}

FontFileInfo FontFile::read(const QString &path, int faceIndex) {
    FontFileInfo info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { info.error = QStringLiteral("Could not open the file"); return info; }
    if (f.size() > kMaxFontBytes) { info.error = QStringLiteral("The file is too large to be a font"); return info; }
    if (f.size() < 12) { info.error = QStringLiteral("The file is too small to be a font"); return info; }
    return parse(f.readAll(), faceIndex);
}

FontFileInfo FontFile::parse(const QByteArray &b, int faceIndex) {
    FontFileInfo info;
    if (b.size() < 12) { info.error = QStringLiteral("Not a font file"); return info; }
    QString tag = tagAt(b, 0);
    const quint32 magic = u32(b, 0);
    qsizetype dir = 0;
    bool collection = false;
    if (tag == QLatin1String("ttcf")) {
        collection = true;
        info.faceCount = int(u32(b, 8));
        if (info.faceCount <= 0 || info.faceCount > 512) { info.error = QStringLiteral("The collection header is damaged"); return info; }
        if (faceIndex < 0 || faceIndex >= info.faceCount) faceIndex = 0;
        dir = u32(b, 12 + qsizetype(faceIndex) * 4);
        if (dir <= 0 || dir + 12 > b.size()) { info.error = QStringLiteral("The collection points outside the file"); return info; }
        tag = tagAt(b, dir);
    } else if (tag == QLatin1String("wOFF") || tag == QLatin1String("wOF2")) {
        info.error = QStringLiteral("A web font (WOFF); desktop applications cannot use it");
        return info;
    }
    const quint32 sfnt = u32(b, dir);
    const bool trueType = sfnt == 0x00010000 || tag == QLatin1String("true");
    const bool cff = tag == QLatin1String("OTTO");
    if (!trueType && !cff) {
        if (magic == 0x80010000 || b.startsWith("%!PS") || b.startsWith("%!FontType1")) info.error = QStringLiteral("A PostScript Type 1 font; modern applications no longer read these");
        else info.error = QStringLiteral("Not an OpenType or TrueType font");
        return info;
    }
    info.kind = collection ? (cff ? QStringLiteral("OpenType PostScript Collection") : QStringLiteral("TrueType Collection"))
                           : (cff ? QStringLiteral("OpenType PostScript") : QStringLiteral("TrueType"));

    const quint16 numTables = u16(b, dir + 4);
    if (numTables == 0 || numTables > 512) { info.error = QStringLiteral("The table directory is damaged"); return info; }
    QHash<QString, Table> tables;
    for (quint16 i = 0; i < numTables; ++i) {
        const qsizetype rec = dir + 12 + qsizetype(i) * 16;
        if (rec + 16 > b.size()) { info.problems.append(QStringLiteral("The table directory runs past the end of the file")); break; }
        Table t;
        t.tag = tagAt(b, rec);
        t.offset = u32(b, rec + 8);
        t.length = u32(b, rec + 12);
        if (qint64(t.offset) + t.length > b.size()) {
            info.problems.append(QStringLiteral("The %1 table points outside the file").arg(t.tag));
            continue;
        }
        tables.insert(t.tag, t);
        info.tables.append(t.tag);
    }
    info.ok = true;

    for (const char *required : {"cmap", "head", "hhea", "hmtx", "maxp", "name", "post"})
        if (!tables.contains(QLatin1String(required)))
            info.problems.append(QStringLiteral("The required %1 table is missing").arg(QLatin1String(required)));
    if (cff && !tables.contains(QStringLiteral("CFF ")) && !tables.contains(QStringLiteral("CFF2")))
        info.problems.append(QStringLiteral("An OpenType PostScript font without a CFF table"));
    if (trueType && !(tables.contains(QStringLiteral("glyf")) && tables.contains(QStringLiteral("loca")))
        && !tables.contains(QStringLiteral("CFF ")) && !tables.contains(QStringLiteral("CBDT")) && !tables.contains(QStringLiteral("sbix")))
        info.problems.append(QStringLiteral("No glyph outlines: glyf and loca are missing"));
    if (!tables.contains(QStringLiteral("OS/2")))
        info.warnings.append(QStringLiteral("No OS/2 table: weight, width and embedding are unknown"));
    info.variable = tables.contains(QStringLiteral("fvar"));
    info.color = tables.contains(QStringLiteral("COLR")) || tables.contains(QStringLiteral("CBDT"))
              || tables.contains(QStringLiteral("sbix")) || tables.contains(QStringLiteral("SVG "));

    if (tables.contains(QStringLiteral("maxp"))) {
        const Table &t = tables[QStringLiteral("maxp")];
        if (t.length >= 6) info.glyphCount = u16(b, t.offset + 4);
        if (info.glyphCount == 0) info.problems.append(QStringLiteral("The font declares no glyphs"));
    }
    if (tables.contains(QStringLiteral("head"))) {
        const Table &t = tables[QStringLiteral("head")];
        if (t.length >= 54) {
            if (u32(b, t.offset + 12) != 0x5F0F3CF5) info.problems.append(QStringLiteral("The head table's magic number is wrong"));
            info.unitsPerEm = u16(b, t.offset + 18);
            if (info.unitsPerEm < 16 || info.unitsPerEm > 16384) info.warnings.append(QStringLiteral("Units per em is out of range"));
            const quint16 macStyle = u16(b, t.offset + 44);
            info.bold = macStyle & 1;
            info.italic = macStyle & 2;
        } else {
            info.problems.append(QStringLiteral("The head table is too short"));
        }
    }
    if (tables.contains(QStringLiteral("OS/2"))) {
        const Table &t = tables[QStringLiteral("OS/2")];
        if (t.length >= 64) {
            info.weightClass = u16(b, t.offset + 4);
            info.widthClass = u16(b, t.offset + 6);
            info.fsType = u16(b, t.offset + 8);
            info.familyClass = s16(b, t.offset + 30) >> 8;
            info.panose = b.mid(t.offset + 32, 10);
            info.vendorId = QString::fromLatin1(b.constData() + t.offset + 58, 4).trimmed();
            const quint16 fsSelection = u16(b, t.offset + 62);
            if (fsSelection & 0x0001) info.italic = true;
            if (fsSelection & 0x0020) info.bold = true;
            if ((info.fsType & 0x0002) && !(info.fsType & 0x000C))
                info.warnings.append(QStringLiteral("Embedding is restricted: documents may not carry this font"));
        } else {
            info.warnings.append(QStringLiteral("The OS/2 table is too short"));
        }
    }
    if (tables.contains(QStringLiteral("post"))) {
        const Table &t = tables[QStringLiteral("post")];
        if (t.length >= 16) info.fixedPitch = u32(b, t.offset + 12) != 0;
    }
    if (tables.contains(QStringLiteral("name"))) readNames(b, tables[QStringLiteral("name")], info);
    if (info.family.isEmpty()) info.problems.append(QStringLiteral("The font has no family name"));
    if (info.postscriptName.isEmpty()) info.warnings.append(QStringLiteral("No PostScript name"));
    if (tables.contains(QStringLiteral("cmap"))) {
        const Table &t = tables[QStringLiteral("cmap")];
        if (t.length < 4 || u16(b, t.offset + 2) == 0) info.problems.append(QStringLiteral("The cmap table maps no characters"));
    }
    return info;
}

QString FontFile::scriptName(const QString &tag) {
    static const QHash<QString, QString> names = {
        {"DFLT", "Default"}, {"latn", "Latin"}, {"cyrl", "Cyrillic"}, {"grek", "Greek"}, {"arab", "Arabic"},
        {"hebr", "Hebrew"}, {"deva", "Devanagari"}, {"dev2", "Devanagari"}, {"beng", "Bengali"}, {"bng2", "Bengali"},
        {"guru", "Gurmukhi"}, {"gur2", "Gurmukhi"}, {"gujr", "Gujarati"}, {"gjr2", "Gujarati"}, {"orya", "Odia"},
        {"ory2", "Odia"}, {"taml", "Tamil"}, {"tml2", "Tamil"}, {"telu", "Telugu"}, {"tel2", "Telugu"},
        {"knda", "Kannada"}, {"knd2", "Kannada"}, {"mlym", "Malayalam"}, {"mlm2", "Malayalam"}, {"sinh", "Sinhala"},
        {"thai", "Thai"}, {"lao ", "Lao"}, {"tibt", "Tibetan"}, {"mymr", "Myanmar"}, {"mym2", "Myanmar"},
        {"geor", "Georgian"}, {"armn", "Armenian"}, {"hang", "Hangul"}, {"jamo", "Hangul Jamo"}, {"hani", "Han"},
        {"kana", "Katakana"}, {"hira", "Hiragana"}, {"bopo", "Bopomofo"}, {"ethi", "Ethiopic"}, {"khmr", "Khmer"},
        {"mong", "Mongolian"}, {"syrc", "Syriac"}, {"thaa", "Thaana"}, {"cher", "Cherokee"}, {"cans", "Canadian Syllabics"},
        {"ogam", "Ogham"}, {"runr", "Runic"}, {"yi  ", "Yi"}, {"tfng", "Tifinagh"}, {"nko ", "N'Ko"}, {"vai ", "Vai"},
        {"java", "Javanese"}, {"bali", "Balinese"}, {"sund", "Sundanese"}, {"copt", "Coptic"}, {"glag", "Glagolitic"},
        {"goth", "Gothic"}, {"brai", "Braille"}, {"math", "Mathematical"}, {"musc", "Musical"}, {"adlm", "Adlam"},
        {"osge", "Osage"}, {"hmng", "Pahawh Hmong"}, {"olck", "Ol Chiki"}, {"lisu", "Lisu"}, {"tale", "Tai Le"},
        {"talu", "New Tai Lue"}, {"lana", "Tai Tham"}, {"cham", "Cham"}, {"kali", "Kayah Li"}, {"lepc", "Lepcha"},
        {"limb", "Limbu"}, {"sylo", "Syloti Nagri"}, {"tglg", "Tagalog"}, {"buhd", "Buhid"}, {"hano", "Hanunoo"},
        {"tagb", "Tagbanwa"}, {"shaw", "Shavian"}, {"dsrt", "Deseret"}, {"xpeo", "Old Persian"}, {"ugar", "Ugaritic"},
        {"phnx", "Phoenician"}, {"ital", "Old Italic"}, {"linb", "Linear B"}, {"cprt", "Cypriot"}, {"egyp", "Egyptian Hieroglyphs"},
    };
    const QString key = tag.leftJustified(4, QLatin1Char(' '));
    return names.value(key, tag.trimmed());
}

QString FontFile::languageName(const QString &code) {
    // fontconfig codes are BCP-47-ish: "en", "zh-cn", "az-az", "pa", "ber-dz".
    static const QHash<QString, QString> special = {
        {"aa", "Afar"}, {"ab", "Abkhazian"}, {"af", "Afrikaans"}, {"ak", "Akan"}, {"am", "Amharic"}, {"an", "Aragonese"},
        {"ar", "Arabic"}, {"as", "Assamese"}, {"ast", "Asturian"}, {"av", "Avaric"}, {"ay", "Aymara"}, {"az-az", "Azerbaijani"},
        {"az-ir", "Azerbaijani (Iran)"}, {"ba", "Bashkir"}, {"be", "Belarusian"}, {"ber-dz", "Berber (Algeria)"}, {"ber-ma", "Berber (Morocco)"},
        {"bg", "Bulgarian"}, {"bh", "Bihari"}, {"bho", "Bhojpuri"}, {"bi", "Bislama"}, {"bin", "Bini"}, {"bm", "Bambara"},
        {"bn", "Bengali"}, {"bo", "Tibetan"}, {"br", "Breton"}, {"brx", "Bodo"}, {"bs", "Bosnian"}, {"bua", "Buriat"},
        {"byn", "Blin"}, {"ca", "Catalan"}, {"ce", "Chechen"}, {"ch", "Chamorro"}, {"chm", "Mari"}, {"chr", "Cherokee"},
        {"co", "Corsican"}, {"crh", "Crimean Tatar"}, {"cs", "Czech"}, {"csb", "Kashubian"}, {"cu", "Church Slavonic"}, {"cv", "Chuvash"},
        {"cy", "Welsh"}, {"da", "Danish"}, {"de", "German"}, {"doi", "Dogri"}, {"dv", "Divehi"}, {"dz", "Dzongkha"},
        {"ee", "Ewe"}, {"el", "Greek"}, {"en", "English"}, {"eo", "Esperanto"}, {"es", "Spanish"}, {"et", "Estonian"},
        {"eu", "Basque"}, {"fa", "Persian"}, {"fat", "Fanti"}, {"ff", "Fulah"}, {"fi", "Finnish"}, {"fil", "Filipino"},
        {"fj", "Fijian"}, {"fo", "Faroese"}, {"fr", "French"}, {"fur", "Friulian"}, {"fy", "Western Frisian"}, {"ga", "Irish"},
        {"gd", "Scottish Gaelic"}, {"gez", "Geez"}, {"gl", "Galician"}, {"gn", "Guarani"}, {"gu", "Gujarati"}, {"gv", "Manx"},
        {"ha", "Hausa"}, {"haw", "Hawaiian"}, {"he", "Hebrew"}, {"hi", "Hindi"}, {"hne", "Chhattisgarhi"}, {"ho", "Hiri Motu"},
        {"hr", "Croatian"}, {"hsb", "Upper Sorbian"}, {"ht", "Haitian"}, {"hu", "Hungarian"}, {"hy", "Armenian"}, {"hz", "Herero"},
        {"ia", "Interlingua"}, {"id", "Indonesian"}, {"ie", "Interlingue"}, {"ig", "Igbo"}, {"ii", "Sichuan Yi"}, {"ik", "Inupiaq"},
        {"io", "Ido"}, {"is", "Icelandic"}, {"it", "Italian"}, {"iu", "Inuktitut"}, {"ja", "Japanese"}, {"jv", "Javanese"},
        {"ka", "Georgian"}, {"kaa", "Kara-Kalpak"}, {"kab", "Kabyle"}, {"ki", "Kikuyu"}, {"kj", "Kuanyama"}, {"kk", "Kazakh"},
        {"kl", "Greenlandic"}, {"km", "Khmer"}, {"kn", "Kannada"}, {"ko", "Korean"}, {"kok", "Konkani"}, {"kr", "Kanuri"},
        {"ks", "Kashmiri"}, {"ku-am", "Kurdish (Armenia)"}, {"ku-iq", "Kurdish (Iraq)"}, {"ku-ir", "Kurdish (Iran)"}, {"ku-tr", "Kurdish (Turkey)"},
        {"kum", "Kumyk"}, {"kv", "Komi"}, {"kw", "Cornish"}, {"kwm", "Kwambi"}, {"ky", "Kyrgyz"}, {"la", "Latin"},
        {"lah", "Lahnda"}, {"lb", "Luxembourgish"}, {"lez", "Lezghian"}, {"lg", "Ganda"}, {"li", "Limburgish"}, {"ln", "Lingala"},
        {"lo", "Lao"}, {"lt", "Lithuanian"}, {"lv", "Latvian"}, {"mai", "Maithili"}, {"mg", "Malagasy"}, {"mh", "Marshallese"},
        {"mi", "Maori"}, {"mk", "Macedonian"}, {"ml", "Malayalam"}, {"mn-cn", "Mongolian (China)"}, {"mn-mn", "Mongolian"}, {"mni", "Manipuri"},
        {"mo", "Moldavian"}, {"mr", "Marathi"}, {"ms", "Malay"}, {"mt", "Maltese"}, {"my", "Burmese"}, {"na", "Nauru"},
        {"nb", "Norwegian Bokmål"}, {"nds", "Low German"}, {"ne", "Nepali"}, {"ng", "Ndonga"}, {"nl", "Dutch"}, {"nn", "Norwegian Nynorsk"},
        {"no", "Norwegian"}, {"nqo", "N'Ko"}, {"nr", "South Ndebele"}, {"nso", "Northern Sotho"}, {"nv", "Navajo"}, {"ny", "Chichewa"},
        {"oc", "Occitan"}, {"om", "Oromo"}, {"or", "Odia"}, {"os", "Ossetic"}, {"ota", "Ottoman Turkish"}, {"pa", "Punjabi"},
        {"pa-pk", "Punjabi (Pakistan)"}, {"pap-an", "Papiamento (Antilles)"}, {"pap-aw", "Papiamento (Aruba)"}, {"pes", "Iranian Persian"},
        {"pl", "Polish"}, {"prs", "Dari"}, {"ps-af", "Pashto (Afghanistan)"}, {"ps-pk", "Pashto (Pakistan)"}, {"pt", "Portuguese"},
        {"qu", "Quechua"}, {"quz", "Cusco Quechua"}, {"rm", "Romansh"}, {"rn", "Rundi"}, {"ro", "Romanian"}, {"ru", "Russian"},
        {"rw", "Kinyarwanda"}, {"sa", "Sanskrit"}, {"sah", "Yakut"}, {"sat", "Santali"}, {"sc", "Sardinian"}, {"sco", "Scots"},
        {"sd", "Sindhi"}, {"se", "Northern Sami"}, {"sel", "Selkup"}, {"sg", "Sango"}, {"sh", "Serbo-Croatian"}, {"shs", "Shuswap"},
        {"si", "Sinhala"}, {"sid", "Sidamo"}, {"sk", "Slovak"}, {"sl", "Slovenian"}, {"sm", "Samoan"}, {"sma", "Southern Sami"},
        {"smj", "Lule Sami"}, {"smn", "Inari Sami"}, {"sms", "Skolt Sami"}, {"sn", "Shona"}, {"so", "Somali"}, {"sq", "Albanian"},
        {"sr", "Serbian"}, {"ss", "Swati"}, {"st", "Southern Sotho"}, {"su", "Sundanese"}, {"sv", "Swedish"}, {"sw", "Swahili"},
        {"syr", "Syriac"}, {"szl", "Silesian"}, {"ta", "Tamil"}, {"te", "Telugu"}, {"tg", "Tajik"}, {"th", "Thai"},
        {"ti-er", "Tigrinya (Eritrea)"}, {"ti-et", "Tigrinya (Ethiopia)"}, {"tig", "Tigre"}, {"tk", "Turkmen"}, {"tl", "Tagalog"},
        {"tn", "Tswana"}, {"to", "Tongan"}, {"tr", "Turkish"}, {"ts", "Tsonga"}, {"tt", "Tatar"}, {"tw", "Twi"},
        {"ty", "Tahitian"}, {"tyv", "Tuvinian"}, {"ug", "Uyghur"}, {"uk", "Ukrainian"}, {"und-zmth", "Mathematical notation"},
        {"und-zsye", "Emoji"}, {"ur", "Urdu"}, {"uz", "Uzbek"}, {"ve", "Venda"}, {"vi", "Vietnamese"}, {"vo", "Volapük"},
        {"vot", "Votic"}, {"wa", "Walloon"}, {"wal", "Wolaytta"}, {"wen", "Sorbian"}, {"wo", "Wolof"}, {"xh", "Xhosa"},
        {"yap", "Yapese"}, {"yi", "Yiddish"}, {"yo", "Yoruba"}, {"za", "Zhuang"}, {"zh-cn", "Chinese (Simplified)"},
        {"zh-hk", "Chinese (Hong Kong)"}, {"zh-mo", "Chinese (Macao)"}, {"zh-sg", "Chinese (Singapore)"}, {"zh-tw", "Chinese (Traditional)"},
        {"zu", "Zulu"}, {"agr", "Aguaruna"}, {"anp", "Angika"}, {"ayc", "Southern Aymara"}, {"bem", "Bemba"}, {"ce", "Chechen"},
        {"cmn", "Mandarin"}, {"dsb", "Lower Sorbian"}, {"ff", "Fulah"}, {"gez", "Geez"}, {"hak", "Hakka"}, {"hif", "Fiji Hindi"},
        {"ky", "Kyrgyz"}, {"lij", "Ligurian"}, {"lzh", "Literary Chinese"}, {"mag", "Magahi"}, {"mfe", "Morisyen"}, {"mhr", "Eastern Mari"},
        {"miq", "Miskito"}, {"mjw", "Karbi"}, {"nan", "Min Nan"}, {"nhn", "Central Nahuatl"}, {"niu", "Niuean"}, {"rif", "Riffian"},
        {"sgs", "Samogitian"}, {"szl", "Silesian"}, {"tcy", "Tulu"}, {"the", "Chitwania Tharu"}, {"tpi", "Tok Pisin"}, {"unm", "Unami"},
        {"wae", "Walser"}, {"yue", "Cantonese"}, {"yuw", "Yau"},
    };
    if (special.contains(code)) return special.value(code);
    const QLocale locale(code);
    if (locale.language() != QLocale::C && locale.language() != QLocale::AnyLanguage) {
        const QString name = QLocale::languageToString(locale.language());
        if (!name.isEmpty() && name != QLatin1String("C")) return name;
    }
    return code;
}

QString FontFile::weightName(int w) {
    if (w <= 0) return {};
    if (w < 150) return QStringLiteral("Thin");
    if (w < 250) return QStringLiteral("Extra-light");
    if (w < 350) return QStringLiteral("Light");
    if (w < 450) return QStringLiteral("Regular");
    if (w < 550) return QStringLiteral("Medium");
    if (w < 650) return QStringLiteral("Semi-bold");
    if (w < 750) return QStringLiteral("Bold");
    if (w < 850) return QStringLiteral("Extra-bold");
    return QStringLiteral("Black");
}

QString FontFile::widthName(int w) {
    switch (w) {
    case 1: return QStringLiteral("Ultra-condensed");
    case 2: return QStringLiteral("Extra-condensed");
    case 3: return QStringLiteral("Condensed");
    case 4: return QStringLiteral("Semi-condensed");
    case 5: return QStringLiteral("Normal");
    case 6: return QStringLiteral("Semi-expanded");
    case 7: return QStringLiteral("Expanded");
    case 8: return QStringLiteral("Extra-expanded");
    case 9: return QStringLiteral("Ultra-expanded");
    }
    return {};
}
