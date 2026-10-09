#include "backend.h"
#include "filepicker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace {

// Nothing here resolves through whatever PATH the session carries: the tools
// are the system's own, and a user-writable directory ahead of /usr/bin must
// not be able to substitute them.
const QString kSafePath = QStringLiteral("/usr/local/bin:/usr/bin:/bin");

// The base Noto faces are ordinary, choosable text fonts, so they stay visible;
// it is the per-script variants that get folded away.
const QStringList kCoverageKeep = {
    QStringLiteral("Noto Sans"), QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans Mono"),
    QStringLiteral("Noto Sans Display"), QStringLiteral("Noto Serif Display"),
    QStringLiteral("Noto Color Emoji"), QStringLiteral("Noto Emoji"),
};

constexpr int kRecentDays = 7;

bool caseInsensitiveLess(const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
}

QString slantName(int slant) {
    if (slant >= 110) return QStringLiteral("Oblique");
    if (slant >= 100) return QStringLiteral("Italic");
    return QStringLiteral("Roman");
}

}  // namespace

// Every helper process runs with a fixed PATH and a UTF-8 C locale.
QProcessEnvironment safeEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"), kSafePath);
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    return env;
}

// fontconfig's weight scale → OS/2 usWeightClass.
int FontFace::weightClass() const {
    if (weight <= 0) return 100;
    if (weight <= 40) return 200;
    if (weight <= 50) return 300;
    if (weight <= 80) return 400;
    if (weight <= 100) return 500;
    if (weight <= 180) return 600;
    if (weight <= 200) return 700;
    if (weight <= 205) return 800;
    return 900;
}

QString FontFamily::representative() const {
    if (faces.isEmpty()) return Backend::regularFace(files);
    const FontFace *best = nullptr;
    int bestScore = 1 << 30;
    for (const FontFace &face : faces) {
        if (face.disabled) continue;
        const QString style = face.style.toLower();
        int score = qAbs(face.weight - 80) + (face.slant > 0 ? 300 : 0) + qAbs(face.width - 100) * 2 + (face.variable ? 20 : 0);
        // Legacy families declare one weight for every face; the style name
        // then decides: Regular, Book or Roman before Medium before the rest.
        if (style.contains(QLatin1String("italic")) || style.contains(QLatin1String("oblique"))) score += 300;
        if (style == QLatin1String("regular") || style == QLatin1String("book") || style == QLatin1String("roman") || style == QLatin1String("normal") || style.isEmpty()) score += 0;
        else if (style == QLatin1String("medium") || style == QLatin1String("text")) score += 5;
        else score += 10;
        if (!best || score < bestScore) { best = &face; bestScore = score; }
    }
    return best ? best->file : Backend::regularFace(files);
}

QString FontFamily::fcName() const {
    const QString rep = representative();
    for (const FontFace &face : faces) if (face.file == rep) return face.family;
    return faces.isEmpty() ? name : faces.first().family;
}

QVariantMap faceToMap(const FontFace &face) {
    QVariantMap m;
    m.insert(QStringLiteral("family"), face.family);
    m.insert(QStringLiteral("style"), face.style);
    m.insert(QStringLiteral("file"), face.file);
    m.insert(QStringLiteral("ext"), Backend::extensionOf(face.file));
    m.insert(QStringLiteral("mono"), face.mono);
    m.insert(QStringLiteral("psName"), face.psName);
    m.insert(QStringLiteral("fullName"), face.fullName);
    m.insert(QStringLiteral("version"), face.version);
    m.insert(QStringLiteral("format"), face.format);
    m.insert(QStringLiteral("foundry"), face.foundry);
    m.insert(QStringLiteral("weight"), FontFile::weightName(face.weightClass()));
    m.insert(QStringLiteral("weightClass"), face.weightClass());
    m.insert(QStringLiteral("slant"), slantName(face.slant));
    m.insert(QStringLiteral("width"), face.width);
    m.insert(QStringLiteral("variable"), face.variable);
    m.insert(QStringLiteral("index"), face.index);
    m.insert(QStringLiteral("disabled"), face.disabled);
    m.insert(QStringLiteral("duplicate"), face.duplicate);
    m.insert(QStringLiteral("user"), face.user);
    m.insert(QStringLiteral("bytes"), QFileInfo(face.file).size());
    return m;
}

QVariantList facesToList(const QList<FontFace> &faces) {
    QVariantList out;
    out.reserve(faces.size());
    for (const FontFace &face : faces) out.append(faceToMap(face));
    return out;
}

QVariantMap infoToMap(const FontFileInfo &i) {
    QVariantMap m;
    m.insert(QStringLiteral("ok"), i.ok);
    m.insert(QStringLiteral("error"), i.error);
    m.insert(QStringLiteral("problems"), i.problems);
    m.insert(QStringLiteral("warnings"), i.warnings);
    m.insert(QStringLiteral("kind"), i.kind);
    m.insert(QStringLiteral("faceCount"), i.faceCount);
    m.insert(QStringLiteral("glyphCount"), i.glyphCount);
    m.insert(QStringLiteral("unitsPerEm"), i.unitsPerEm);
    m.insert(QStringLiteral("copyright"), i.copyright);
    m.insert(QStringLiteral("family"), i.family);
    m.insert(QStringLiteral("subfamily"), i.subfamily);
    m.insert(QStringLiteral("uniqueId"), i.uniqueId);
    m.insert(QStringLiteral("fullName"), i.fullName);
    m.insert(QStringLiteral("version"), i.version);
    m.insert(QStringLiteral("postscriptName"), i.postscriptName);
    m.insert(QStringLiteral("trademark"), i.trademark);
    m.insert(QStringLiteral("manufacturer"), i.manufacturer);
    m.insert(QStringLiteral("designer"), i.designer);
    m.insert(QStringLiteral("description"), i.description);
    m.insert(QStringLiteral("vendorUrl"), i.vendorUrl);
    m.insert(QStringLiteral("designerUrl"), i.designerUrl);
    m.insert(QStringLiteral("license"), i.license);
    m.insert(QStringLiteral("licenseUrl"), i.licenseUrl);
    m.insert(QStringLiteral("weightClass"), i.weightClass);
    m.insert(QStringLiteral("weightName"), FontFile::weightName(i.weightClass));
    m.insert(QStringLiteral("widthClass"), i.widthClass);
    m.insert(QStringLiteral("widthName"), FontFile::widthName(i.widthClass));
    m.insert(QStringLiteral("vendorId"), i.vendorId);
    m.insert(QStringLiteral("embedding"), i.embedding());
    m.insert(QStringLiteral("classification"), i.classification());
    m.insert(QStringLiteral("traits"), i.traits());
    m.insert(QStringLiteral("variable"), i.variable);
    m.insert(QStringLiteral("color"), i.color);
    m.insert(QStringLiteral("fixedPitch"), i.fixedPitch);
    m.insert(QStringLiteral("tables"), i.tables);
    return m;
}

// ── FamilyModel ─────────────────────────────────────────────────────────────

FamilyModel::FamilyModel(QObject *parent) : QAbstractListModel(parent) {}

int FamilyModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant FamilyModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const FontFamily &f = m_rows.at(index.row());
    switch (role) {
    case NameRole: return f.name;
    case GroupRole: return f.user ? QStringLiteral("Yours") : QStringLiteral("System");
    case StylesRole: return f.styles;
    case FilesRole: return f.files;
    case FacesRole: return facesToList(f.faces);
    case UserRole: return f.user;
    case MonoRole: return f.mono;
    case LatinRole: return f.latin;
    case PreviewFileRole: return f.representative();
    case CoverageRole: return Backend::isCoverage(f.name);
    case InactiveRole: return f.inactive();
    case DuplicateRole: return f.duplicate;
    case VariableRole: return f.variable;
    case FormatRole: return f.formats.join(QStringLiteral(", "));
    case StyleCountRole: return f.styles.size();
    case StarredRole: return f.starred;
    case TagsRole: return f.tags;
    case MembersRole: return f.members;
    }
    return {};
}

QHash<int, QByteArray> FamilyModel::roleNames() const {
    return {
        {NameRole, "name"}, {GroupRole, "group"}, {StylesRole, "styles"}, {FilesRole, "files"}, {FacesRole, "faces"},
        {UserRole, "user"}, {MonoRole, "mono"}, {LatinRole, "latin"}, {PreviewFileRole, "previewFile"},
        {CoverageRole, "coverage"}, {InactiveRole, "inactive"}, {DuplicateRole, "duplicate"}, {VariableRole, "variable"},
        {FormatRole, "format"}, {StyleCountRole, "styleCount"}, {StarredRole, "starred"}, {TagsRole, "tags"},
        {MembersRole, "members"},
    };
}

void FamilyModel::setRows(const QList<FontFamily> &rows) {
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

QString FamilyModel::nameAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).name : QString(); }

int FamilyModel::indexOf(const QString &name) const {
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows.at(i).name == name) return i;
    return -1;
}

// ── Backend ─────────────────────────────────────────────────────────────────

Backend::Backend(QObject *parent) : QObject(parent) {
    m_fontRoot = userFontRoot();

    // omarchy-font-set lives under $OMARCHY_PATH/bin, which is not a standard
    // prefix, so it is resolved from the environment rather than found on PATH.
    const QString omarchy = qEnvironmentVariable("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
    const QString tool = omarchy + QStringLiteral("/bin/omarchy-font-set");
    if (QFileInfo(tool).isExecutable()) m_fontSetTool = tool;

    QSettings settings;
    m_sample = settings.value(QStringLiteral("sample"), defaultSample()).toString();
    m_showCoverage = settings.value(QStringLiteral("showCoverage"), false).toBool();
    m_view = settings.value(QStringLiteral("view"), QStringLiteral("specimen")).toString();
    m_groupWeights = settings.value(QStringLiteral("groupWeights"), true).toBool();
    m_sortOrder = settings.value(QStringLiteral("sortOrder"), QStringLiteral("name")).toString();
    m_compare = settings.value(QStringLiteral("compare")).toStringList();
    m_specimenScale = qBound(0.5, settings.value(QStringLiteral("specimenScale"), 1.0).toReal(), 2.5);
    m_reducedMotion = settings.value(QStringLiteral("reducedMotion"), false).toBool();
    m_highContrast = settings.value(QStringLiteral("highContrast"), false).toBool();

    m_libraryPath = Library::defaultPath();
    QString error;
    if (!m_library.load(m_libraryPath, &error)) {
        m_library = Library();
        m_libraryUnreadable = true;
        setStatus(tr("Could not read the library (%1); nothing will be saved over it").arg(error), true);
    }

    m_picker = new PortalFileChooser(this);
    connect(m_picker, &PortalFileChooser::selectedMany, this, [this](const QList<QUrl> &urls) {
        m_picking = false;
        stageUrls(urls);
    });
    connect(m_picker, &PortalFileChooser::selected, this, [this](const QUrl &url) {
        if (m_pendingPdfKind.isEmpty()) return;
        const QString kind = m_pendingPdfKind;
        m_pendingPdfKind.clear();
        m_picking = false;
        QString path = url.toLocalFile();
        if (path.isEmpty()) return;
        if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
        const QString err = writeSpecimenPdf(path, kind, m_selectedName);
        if (err.isEmpty()) setStatus(tr("Wrote %1").arg(QFileInfo(path).fileName()));
        else setStatus(tr("Could not write the PDF: %1").arg(err), true);
    });
    connect(m_picker, &PortalFileChooser::folderSelected, this, [this](const QUrl &url) {
        m_picking = false;
        if (!url.isLocalFile()) return;
        if (!m_pendingExportCollection.isEmpty()) {
            const QString id = m_pendingExportCollection;
            m_pendingExportCollection.clear();
            const Collection *c = nullptr;
            for (const Collection &x : m_library.collections) if (x.id == id) c = &x;
            if (!c) return;
            int total = 0;
            for (const QString &name : c->families) {
                const FontFamily *fam = family(name);
                if (!fam) continue;
                const QString sub = url.toLocalFile() + QLatin1Char('/') + slug(fam->name);
                QDir().mkpath(sub);
                int copied = 0;
                const QString err = exportFiles(fam->files, sub, &copied);
                if (!err.isEmpty()) { setStatus(tr("Export failed: %1").arg(err), true); return; }
                total += copied;
            }
            setStatus(tr("Exported %1: %2 files to %3").arg(c->name).arg(total).arg(QDir(url.toLocalFile()).dirName()));
            emit exported(url.toLocalFile(), total);
            return;
        }
        const FontFamily *f = selectedFamily();
        if (!f) return;
        int copied = 0;
        const QString err = exportFiles(f->files, url.toLocalFile(), &copied);
        if (err.isEmpty()) {
            setStatus(tr("Exported %1 to %2").arg(f->name, QDir(url.toLocalFile()).dirName()));
            emit exported(url.toLocalFile(), copied);
        } else {
            setStatus(tr("Export failed: %1").arg(err), true);
        }
    });
    connect(m_picker, &PortalFileChooser::canceled, this, [this] { m_picking = false; m_pendingPdfKind.clear(); m_pendingExportCollection.clear(); });
    connect(m_picker, &PortalFileChooser::failed, this, [this](const QString &message) {
        m_picking = false;
        m_pendingPdfKind.clear();
        setStatus(tr("Could not open a file chooser: %1").arg(message), true);
    });
}

Backend::~Backend() {
    stopMetadata();
    for (QThread *t : std::as_const(m_metaThreads)) t->wait();
    for (QProcess *p : {m_scanProcess, m_latinProcess, m_charsetProcess, m_extractProcess, m_stageScanProcess,
                        m_cacheProcess, m_fontSetProcess, m_repertoireProcess}) {
        if (!p) continue;
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(500); }
    }
}

// ── Pure helpers ────────────────────────────────────────────────────────────

QString Backend::tildePath(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home) return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/'))) return QLatin1Char('~') + path.mid(home.size());
    return path;
}

QString Backend::userFontRoot() {
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty() || !data.startsWith(QLatin1Char('/'))) data = QDir::homePath() + QStringLiteral("/.local/share");
    return QDir::cleanPath(data + QStringLiteral("/fonts"));
}

QString Backend::extensionOf(const QString &path) {
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (dot < 0 || dot < slash) return {};
    return path.mid(dot + 1).toLower();
}

bool Backend::isDesktopFont(const QString &path) {
    const QString ext = extensionOf(path);
    return ext == QLatin1String("ttf") || ext == QLatin1String("otf") || ext == QLatin1String("ttc");
}

bool Backend::isArchive(const QString &path) {
    const QString lower = path.toLower();
    for (const char *suffix : {".zip", ".tar", ".tar.gz", ".tgz", ".tar.xz", ".tar.zst", ".tar.bz2", ".7z"})
        if (lower.endsWith(QLatin1String(suffix))) return true;
    return false;
}

bool Backend::isCoverage(const QString &family) {
    if (!family.startsWith(QLatin1String("Noto "))) return false;
    return !kCoverageKeep.contains(family);
}

// fontconfig family names are free text; the directory name is not.
QString Backend::slug(const QString &family) {
    QString s = family;
    s.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]")));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
    s.remove(QRegularExpression(QStringLiteral("^[-.]+")));
    if (s.isEmpty()) return QStringLiteral("Custom");
    return s.left(64);
}

QString Backend::commonGroup(const QStringList &families) {
    if (families.isEmpty()) return {};
    if (families.size() == 1) return families.first();
    const QStringList words = families.first().split(QLatin1Char(' '));
    QStringList common;
    for (int w = 0; w < words.size(); ++w) {
        bool ok = true;
        for (int i = 1; i < families.size(); ++i) {
            const QStringList other = families.at(i).split(QLatin1Char(' '));
            if (other.size() <= w || other.at(w) != words.at(w)) { ok = false; break; }
        }
        if (!ok) break;
        common.append(words.at(w));
    }
    return common.isEmpty() ? families.first() : common.join(QLatin1Char(' '));
}

QString Backend::regularFace(const QStringList &files) {
    if (files.isEmpty()) return {};
    static const QRegularExpression regular(QStringLiteral("-?regular\\."), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression shortR(QStringLiteral("[-_]R\\."));
    for (const QString &f : files)
        if (regular.match(f).hasMatch() || shortR.match(f).hasMatch()) return f;
    return files.first();
}

// One fc-list / fc-scan pass gives everything the list and the Info pane need
// from fontconfig. Tab-separated; a family or style name cannot carry a tab.
QString Backend::fcFormat() {
    return QStringLiteral("%{family[0]}\t%{style[0]}\t%{file}\t%{spacing}\t%{postscriptname}\t%{fullname}\t"
                          "%{fontversion}\t%{fontformat}\t%{foundry}\t%{weight}\t%{slant}\t%{width}\t%{variable}\t"
                          "%{index}\t%{lang}\n");
}

// A variable font's default face reports its axis as a range, "[0 210]".
// It is read as the value nearest `preferred` inside that range.
static int axisValue(const QString &text, int preferred) {
    if (!text.startsWith(QLatin1Char('['))) return text.toInt();
    const QStringList ends = text.mid(1, text.size() - 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (ends.size() != 2) return preferred;
    const int lo = ends.at(0).toInt(), hi = ends.at(1).toInt();
    return std::clamp(preferred, std::min(lo, hi), std::max(lo, hi));
}

FontFace Backend::parseFace(const QStringList &parts) {
    FontFace face;
    auto at = [&parts](int i) { return i < parts.size() ? parts.at(i).trimmed() : QString(); };
    face.family = at(0);
    face.style = at(1);
    face.file = at(2);
    face.mono = at(3) == QLatin1String("100");
    face.psName = at(4);
    face.fullName = at(5);
    bool ok = false;
    const double raw = at(6).toDouble(&ok);
    if (ok && raw > 0) {
        QString v = QString::number(raw / 65536.0, 'f', 3);
        while (v.endsWith(QLatin1Char('0')) && !v.endsWith(QLatin1String(".0"))) v.chop(1);
        face.version = v;
    }
    face.format = at(7);
    face.foundry = at(8);
    face.weight = axisValue(at(9), 80);
    face.slant = axisValue(at(10), 0);
    face.width = at(11).isEmpty() ? 100 : axisValue(at(11), 100);
    face.variable = at(12).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    face.index = at(13).toInt();
    for (const QString &code : at(14).split(QLatin1Char('|'), Qt::SkipEmptyParts)) face.langs.append(code.trimmed());
    return face;
}

QList<FontFamily> Backend::parseFcList(const QString &text, const QString &userRoot, bool *truncated) {
    QHash<QString, FontFamily> map;
    QHash<QString, int> seen;  // file#index#family → position in that family's faces
    const QString userPrefix = userRoot.isEmpty() ? QString() : userRoot + QLatin1Char('/');
    QStringList lines = text.split(QLatin1Char('\n'));
    // Output cut at the byte cap ends mid-line; that last line is not a face.
    if (truncated && *truncated && !lines.isEmpty()) lines.removeLast();
    if (lines.size() > kMaxListLines && truncated) *truncated = true;
    const int cap = std::min<qsizetype>(lines.size(), kMaxListLines);
    for (int i = 0; i < cap; ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
        if (parts.size() < 3) continue;
        FontFace face = parseFace(parts);
        if (face.family.isEmpty() || face.file.isEmpty()) continue;
        face.user = !userPrefix.isEmpty() && face.file.startsWith(userPrefix);
        // fontconfig lists a variable font's default face and its "Regular"
        // named instance under the same file and index, in either order. They
        // are one face: the named one's style, the default's variable flag.
        const QString key = face.file + QLatin1Char('#') + QString::number(face.index) + QLatin1Char('#') + face.family;
        if (const auto it = seen.constFind(key); it != seen.cend()) {
            FontFamily &f = map[face.family];
            FontFace &kept = f.faces[it.value()];
            if (kept.style.isEmpty() && !face.style.isEmpty()) {
                const bool variable = kept.variable;
                kept = face;
                kept.variable = kept.variable || variable;
                if (!f.styles.contains(face.style)) f.styles.append(face.style);
            }
            if (face.variable) { kept.variable = true; f.variable = true; }
            continue;
        }
        FontFamily &f = map[face.family];
        seen.insert(key, f.faces.size());
        f.name = face.family;
        if (!face.style.isEmpty() && !f.styles.contains(face.style)) f.styles.append(face.style);
        if (!f.files.contains(face.file)) f.files.append(face.file);
        if (face.user) f.user = true;
        if (face.mono) f.mono = true;
        if (face.variable) f.variable = true;
        if (!face.format.isEmpty() && !f.formats.contains(face.format)) f.formats.append(face.format);
        if (f.foundry.isEmpty() && !face.foundry.isEmpty() && face.foundry != QLatin1String("unknown")) f.foundry = face.foundry;
        for (const QString &code : face.langs) if (!f.langs.contains(code)) f.langs.append(code);
        f.faces.append(face);
    }
    QList<FontFamily> out = map.values();
    for (FontFamily &f : out) {
        std::sort(f.langs.begin(), f.langs.end());
        std::sort(f.faces.begin(), f.faces.end(), [](const FontFace &a, const FontFace &b) {
            if (a.weight != b.weight) return a.weight < b.weight;
            if (a.slant != b.slant) return a.slant < b.slant;
            return caseInsensitiveLess(a.style, b.style);
        });
    }
    std::sort(out.begin(), out.end(), [](const FontFamily &a, const FontFamily &b) { return caseInsensitiveLess(a.name, b.name); });
    return out;
}

QList<FontFace> Backend::parseFcScan(const QString &text) {
    QList<FontFace> faces;
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int cap = std::min<qsizetype>(lines.size(), 20000);
    for (int i = 0; i < cap; ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
        if (parts.size() < 3) continue;
        FontFace face = parseFace(parts);
        if (!isDesktopFont(face.file)) continue;
        faces.append(face);
    }
    std::sort(faces.begin(), faces.end(), [](const FontFace &a, const FontFace &b) {
        return caseInsensitiveLess(a.family + QLatin1Char(' ') + a.style, b.family + QLatin1Char(' ') + b.style);
    });
    return faces;
}

// "20-7e a0-ff 100 102" → every codepoint, in order.
QList<quint32> Backend::parseCharset(const QString &text) {
    QList<quint32> out;
    for (const QString &token : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const int dash = token.indexOf(QLatin1Char('-'));
        bool ok1 = false, ok2 = false;
        if (dash > 0) {
            const quint32 a = token.left(dash).toUInt(&ok1, 16), b = token.mid(dash + 1).toUInt(&ok2, 16);
            if (ok1 && ok2 && b >= a && b - a < 70000) for (quint32 c = a; c <= b; ++c) out.append(c);
        } else {
            const quint32 a = token.toUInt(&ok1, 16);
            if (ok1) out.append(a);
        }
        if (out.size() > 200000) break;
    }
    return out;
}

// The same family and style in two files is a duplicate. One copy wins at
// render time without anyone choosing which, so it is pointed out.
void Backend::markDuplicates(QList<FontFamily> &families) {
    for (FontFamily &f : families) {
        QHash<QString, int> count;
        for (const FontFace &face : f.faces) count[face.style.toLower()]++;
        f.duplicate = false;
        for (FontFace &face : f.faces) {
            face.duplicate = count.value(face.style.toLower()) > 1;
            if (face.duplicate) f.duplicate = true;
        }
    }
}

// Weight, width and slant words that legacy "family per weight" naming puts
// into the family name: "Acme Sans Black", "Acme Sans Light", "Acme Sans XLight". Not
// optical sizes (Display, Text, Caption) — those are sibling designs.
bool Backend::isStyleWord(const QString &word) {
    static const QSet<QString> words = {
        QStringLiteral("thin"), QStringLiteral("hairline"), QStringLiteral("ultralight"), QStringLiteral("ultra"),
        QStringLiteral("extralight"), QStringLiteral("xlight"), QStringLiteral("light"), QStringLiteral("book"),
        QStringLiteral("regular"), QStringLiteral("normal"), QStringLiteral("roman"), QStringLiteral("medium"),
        QStringLiteral("demibold"), QStringLiteral("demi"), QStringLiteral("semibold"), QStringLiteral("semi"),
        QStringLiteral("bold"), QStringLiteral("extrabold"), QStringLiteral("xbold"), QStringLiteral("ultrabold"),
        QStringLiteral("black"), QStringLiteral("heavy"), QStringLiteral("extrablack"), QStringLiteral("fat"),
        QStringLiteral("poster"), QStringLiteral("italic"), QStringLiteral("oblique"), QStringLiteral("condensed"),
        QStringLiteral("cond"), QStringLiteral("compressed"), QStringLiteral("narrow"), QStringLiteral("extended"),
        QStringLiteral("expanded"), QStringLiteral("wide"), QStringLiteral("semicondensed"), QStringLiteral("extracondensed"),
    };
    return words.contains(word.toLower());
}

// Fold "Acme Sans Black", "Acme Sans Light" and friends into "Acme Sans": the words
// after the base are weights, so they become part of the style. A base that
// does not exist as a family of its own is still made when two or more
// weight-named families share it.
void Backend::groupSuperfamilies(QList<FontFamily> &families) {
    QHash<QString, int> indexOf;
    for (int i = 0; i < families.size(); ++i) indexOf.insert(families.at(i).name, i);

    // name → (base, trailing style words)
    QHash<QString, QPair<QString, QStringList>> plan;
    QHash<QString, int> baseCount;
    for (const FontFamily &f : families) {
        const QStringList words = f.name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (words.size() < 2) continue;
        int cut = words.size();
        while (cut > 1 && isStyleWord(words.at(cut - 1))) --cut;
        if (cut == words.size()) continue;  // no trailing style words
        // Prefer the longest existing base: "Noto Sans Mono Bold" → "Noto Sans Mono".
        QString base;
        for (int c = words.size() - 1; c >= 1; --c) {
            const QString candidate = QStringList(words.mid(0, c)).join(QLatin1Char(' '));
            if (indexOf.contains(candidate)) {
                bool allStyle = true;
                for (int k = c; k < words.size(); ++k) if (!isStyleWord(words.at(k))) { allStyle = false; break; }
                if (allStyle) { base = candidate; cut = c; break; }
            }
        }
        if (base.isEmpty()) base = QStringList(words.mid(0, cut)).join(QLatin1Char(' '));
        plan.insert(f.name, {base, words.mid(cut)});
        baseCount[base]++;
    }
    auto folds = [&](const QString &base) { return indexOf.contains(base) || baseCount.value(base) >= 2; };
    // A base that itself folds hands its faces on: "Foo Condensed Light" →
    // "Foo Condensed" → "Foo", style "Condensed Light". Otherwise Foo
    // Condensed would be rebuilt as a row of its own and its faces shown twice.
    QSet<QString> chained;
    for (auto it = plan.begin(); it != plan.end(); ++it) {
        for (int guard = 0; guard < 8; ++guard) {
            const auto up = plan.constFind(it->first);
            if (up == plan.cend() || !folds(up->first)) break;
            it->second = up->second + it->second;
            it->first = up->first;
            chained.insert(it.key());
        }
    }

    QList<FontFamily> out;
    QHash<QString, int> outIndex;
    auto ensureBase = [&](const QString &base) -> FontFamily & {
        if (!outIndex.contains(base)) {
            FontFamily fresh;
            if (indexOf.contains(base)) fresh = families.at(indexOf.value(base));
            else fresh.name = base;
            outIndex.insert(base, out.size());
            out.append(fresh);
        }
        return out[outIndex.value(base)];
    };
    for (const FontFamily &f : families) {
        const auto it = plan.constFind(f.name);
        const bool fold = it != plan.cend() && (folds(it->first) || chained.contains(f.name));
        if (!fold) {
            if (!outIndex.contains(f.name)) { outIndex.insert(f.name, out.size()); out.append(f); }
            else out[outIndex.value(f.name)] = f;
            continue;
        }
        FontFamily &base = ensureBase(it->first);
        const QString prefix = it->second.join(QLatin1Char(' '));
        for (FontFace face : f.faces) {
            QString style = face.style.trimmed();
            if (style.compare(QLatin1String("Regular"), Qt::CaseInsensitive) == 0 || style.isEmpty()) style = prefix;
            else if (style.startsWith(prefix, Qt::CaseInsensitive)) {}
            else style = prefix + QLatin1Char(' ') + style;
            face.style = style;
            base.faces.append(face);
            if (!base.files.contains(face.file)) base.files.append(face.file);
            if (!base.styles.contains(face.style)) base.styles.append(face.style);
        }
        base.members.append(f.name);
        base.user = base.user || f.user;
        base.mono = base.mono || f.mono;
        base.latin = base.latin || f.latin;
        base.variable = base.variable || f.variable;
        base.disabledCount += f.disabledCount;
        for (const QString &fmt : f.formats) if (!base.formats.contains(fmt)) base.formats.append(fmt);
        if (base.foundry.isEmpty()) base.foundry = f.foundry;
        for (const QString &code : f.langs) if (!base.langs.contains(code)) base.langs.append(code);
    }
    for (FontFamily &f : out) {
        if (f.members.isEmpty()) continue;
        std::sort(f.members.begin(), f.members.end(), caseInsensitiveLess);
        std::sort(f.langs.begin(), f.langs.end());
        std::sort(f.faces.begin(), f.faces.end(), [](const FontFace &a, const FontFace &b) {
            if (a.weight != b.weight) return a.weight < b.weight;
            if (a.width != b.width) return a.width < b.width;
            if (a.slant != b.slant) return a.slant < b.slant;
            return caseInsensitiveLess(a.style, b.style);
        });
        f.styles.clear();
        for (const FontFace &face : f.faces) if (!f.styles.contains(face.style)) f.styles.append(face.style);
    }
    std::sort(out.begin(), out.end(), [](const FontFamily &a, const FontFamily &b) { return caseInsensitiveLess(a.name, b.name); });
    families = out;
}

// Writing systems a family can set, from fontconfig's language list. A script
// is present when the font covers a language written in it.
QStringList Backend::scriptsFor(const QStringList &langs) {
    static const QList<QPair<QString, QStringList>> table = {
        {QStringLiteral("Latin"), {QStringLiteral("en")}},
        {QStringLiteral("Greek"), {QStringLiteral("el")}},
        {QStringLiteral("Cyrillic"), {QStringLiteral("ru"), QStringLiteral("uk"), QStringLiteral("bg"), QStringLiteral("sr"), QStringLiteral("be"), QStringLiteral("mk"), QStringLiteral("kk")}},
        {QStringLiteral("Arabic"), {QStringLiteral("ar"), QStringLiteral("fa"), QStringLiteral("ur"), QStringLiteral("ug"), QStringLiteral("ps-af")}},
        {QStringLiteral("Hebrew"), {QStringLiteral("he"), QStringLiteral("yi")}},
        {QStringLiteral("Devanagari"), {QStringLiteral("hi"), QStringLiteral("mr"), QStringLiteral("ne"), QStringLiteral("sa")}},
        {QStringLiteral("Bengali"), {QStringLiteral("bn"), QStringLiteral("as")}},
        {QStringLiteral("Gurmukhi"), {QStringLiteral("pa")}},
        {QStringLiteral("Gujarati"), {QStringLiteral("gu")}},
        {QStringLiteral("Tamil"), {QStringLiteral("ta")}},
        {QStringLiteral("Telugu"), {QStringLiteral("te")}},
        {QStringLiteral("Kannada"), {QStringLiteral("kn")}},
        {QStringLiteral("Malayalam"), {QStringLiteral("ml")}},
        {QStringLiteral("Sinhala"), {QStringLiteral("si")}},
        {QStringLiteral("Thai"), {QStringLiteral("th")}},
        {QStringLiteral("Lao"), {QStringLiteral("lo")}},
        {QStringLiteral("Tibetan"), {QStringLiteral("bo"), QStringLiteral("dz")}},
        {QStringLiteral("Myanmar"), {QStringLiteral("my")}},
        {QStringLiteral("Khmer"), {QStringLiteral("km")}},
        {QStringLiteral("Georgian"), {QStringLiteral("ka")}},
        {QStringLiteral("Armenian"), {QStringLiteral("hy")}},
        {QStringLiteral("Ethiopic"), {QStringLiteral("am"), QStringLiteral("ti-er"), QStringLiteral("ti-et")}},
        {QStringLiteral("Hangul"), {QStringLiteral("ko")}},
        {QStringLiteral("Japanese"), {QStringLiteral("ja")}},
        {QStringLiteral("Chinese"), {QStringLiteral("zh-cn"), QStringLiteral("zh-tw"), QStringLiteral("zh-hk"), QStringLiteral("zh-sg")}},
        {QStringLiteral("Emoji"), {QStringLiteral("und-zsye")}},
        {QStringLiteral("Mathematics"), {QStringLiteral("und-zmth")}},
    };
    QStringList out;
    for (const auto &row : table)
        for (const QString &code : row.second)
            if (langs.contains(code)) { out.append(row.first); break; }
    return out;
}

QString Backend::formatBytes(qint64 bytes) const {
    if (bytes < 1024) return tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0));
    return tr("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

QString Backend::defaultSample() const { return QStringLiteral("The quick brown fox jumps over the lazy dog"); }

// ── Simple state ────────────────────────────────────────────────────────────

void Backend::setFilter(const QString &filter) {
    if (m_filter == filter) return;
    m_filter = filter;
    emit filterChanged();
    if (m_searchScope == QLatin1String("characters")) startCharsetSearch();
    else rebuildRows();
}

void Backend::setSearchScope(const QString &scope) {
    const QString s = (scope == QLatin1String("designer") || scope == QLatin1String("characters")) ? scope : QStringLiteral("names");
    if (m_searchScope == s) return;
    m_searchScope = s;
    emit filterChanged();
    if (s == QLatin1String("characters")) startCharsetSearch();
    else rebuildRows();
}

void Backend::setSource(const QString &source) {
    if (m_source == source) return;
    m_source = source.isEmpty() ? QStringLiteral("all") : source;
    emit sourceChanged();
    rebuildRows();
    if (!m_selectedName.isEmpty() && m_model.indexOf(m_selectedName) < 0 && !staged()) selectFirst();
}

QString Backend::sourceTitle() const {
    if (m_source == QLatin1String("all")) return tr("All Fonts");
    if (m_source == QLatin1String("yours")) return tr("Yours");
    if (m_source == QLatin1String("system")) return tr("System");
    if (m_source == QLatin1String("mono")) return tr("Monospace");
    if (m_source == QLatin1String("recent")) return tr("Recently Added");
    if (m_source == QLatin1String("inactive")) return tr("Deactivated");
    if (m_source == QLatin1String("duplicates")) return tr("Duplicates");
    if (m_source == QLatin1String("starred")) return tr("Starred");
    if (m_source.startsWith(QLatin1String("tag:"))) return QStringLiteral("#") + m_source.mid(4);
    if (m_source.startsWith(QLatin1String("class:"))) return m_source.mid(6);
    if (m_source.startsWith(QLatin1String("script:"))) return m_source.mid(7);
    if (m_source.startsWith(QLatin1String("trait:"))) return m_source.mid(6);
    if (m_source.startsWith(QLatin1String("lang:"))) return FontFile::languageName(m_source.mid(5));
    if (m_source.startsWith(QLatin1String("coll:"))) {
        for (const Collection &c : m_library.collections) if (c.id == m_source.mid(5)) return c.name;
    }
    if (m_source.startsWith(QLatin1String("smart:"))) {
        for (const SmartCollection &s : m_library.smart) if (s.id == m_source.mid(6)) return s.name;
    }
    return m_source;
}

void Backend::setShowCoverage(bool show) {
    if (m_showCoverage == show) return;
    m_showCoverage = show;
    QSettings().setValue(QStringLiteral("showCoverage"), show);
    emit showCoverageChanged();
    rebuildRows();
}

void Backend::setGroupWeights(bool on) {
    if (m_groupWeights == on) return;
    m_groupWeights = on;
    QSettings().setValue(QStringLiteral("groupWeights"), on);
    emit groupWeightsChanged();
    m_pendingSelect = m_selectedName;
    refresh();
}

void Backend::setSortOrder(const QString &order) {
    const QString o = (order == QLatin1String("added") || order == QLatin1String("styles") || order == QLatin1String("foundry")) ? order : QStringLiteral("name");
    if (m_sortOrder == o) return;
    m_sortOrder = o;
    QSettings().setValue(QStringLiteral("sortOrder"), o);
    emit sortOrderChanged();
    rebuildRows();
}

void Backend::setView(const QString &view) {
    const QString v = (view == QLatin1String("grid") || view == QLatin1String("list") || view == QLatin1String("repertoire") || view == QLatin1String("compare")) ? view : QStringLiteral("specimen");
    if (m_view == v) return;
    m_view = v;
    QSettings().setValue(QStringLiteral("view"), v);
    emit viewChanged();
}

void Backend::setSpecimenScale(qreal scale) {
    const qreal s = qBound(0.5, scale, 2.5);
    if (qFuzzyCompare(m_specimenScale, s)) return;
    m_specimenScale = s;
    QSettings().setValue(QStringLiteral("specimenScale"), s);
    emit specimenScaleChanged();
}

void Backend::setSample(const QString &sample) {
    if (m_sample == sample) return;
    m_sample = sample;
    QSettings().setValue(QStringLiteral("sample"), sample);
    emit sampleChanged();
}

void Backend::setReducedMotion(bool on) {
    if (m_reducedMotion == on) return;
    m_reducedMotion = on;
    QSettings().setValue(QStringLiteral("reducedMotion"), on);
    emit reducedMotionChanged();
}

void Backend::setHighContrast(bool on) {
    if (m_highContrast == on) return;
    m_highContrast = on;
    QSettings().setValue(QStringLiteral("highContrast"), on);
    emit highContrastChanged();
}

void Backend::setStatus(const QString &message, bool error) {
    if (m_status == message && m_statusIsError == error) return;
    m_status = message;
    m_statusIsError = error;
    emit statusChanged();
}

void Backend::setScanning(bool on) { if (m_scanning != on) { m_scanning = on; emit scanningChanged(); } }
void Backend::setBusy(bool on) { if (m_busy != on) { m_busy = on; emit busyChanged(); } }

// ── Counts ──────────────────────────────────────────────────────────────────

int Backend::userCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return f.user; }); }
int Backend::systemCount() const { return m_families.size() - userCount(); }
int Backend::coverageCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return isCoverage(f.name) && !f.user; }); }
int Backend::monoCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return f.mono; }); }
int Backend::recentCount() const {
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kRecentDays);
    return std::count_if(m_families.cbegin(), m_families.cend(), [&](const FontFamily &f) { return f.added.isValid() && f.added >= cutoff; });
}
int Backend::inactiveCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return f.disabledCount > 0; }); }
int Backend::duplicateCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return f.duplicate; }); }

int Backend::starredCount() const { return std::count_if(m_families.cbegin(), m_families.cend(), [](const FontFamily &f) { return f.starred; }); }

QString Backend::designClassOf(const QString &familyName) const {
    const FontFamily *f = family(familyName);
    if (!f) return {};
    const FontFileInfo *i = representativeInfo(*f);
    return i ? i->classification() : QString();
}

// Browse by look: what the files say the design is, plus the traits.
QVariantList Backend::designFacets() const {
    QHash<QString, int> counts;
    for (const FontFamily &f : m_families) {
        const FontFileInfo *i = representativeInfo(f);
        const QString cls = i ? i->classification() : QString();
        if (!cls.isEmpty() && cls != QLatin1String("Unknown")) counts[QStringLiteral("class:") + cls]++;
        if (f.mono) counts[QStringLiteral("mono")]++;
        if (f.variable) counts[QStringLiteral("trait:Variable")]++;
        if (i && i->color) counts[QStringLiteral("trait:Colour")]++;
    }
    const QList<QPair<QString, QString>> order = {
        {QStringLiteral("class:Serif"), tr("Serif")}, {QStringLiteral("class:Sans-serif"), tr("Sans-serif")},
        {QStringLiteral("class:Handwriting"), tr("Handwriting")}, {QStringLiteral("class:Ornamental"), tr("Ornamental")},
        {QStringLiteral("class:Symbolic"), tr("Symbolic")}, {QStringLiteral("mono"), tr("Monospace")},
        {QStringLiteral("trait:Variable"), tr("Variable")}, {QStringLiteral("trait:Colour"), tr("Colour glyphs")},
    };
    QVariantList out;
    for (const auto &row : order) {
        if (counts.value(row.first) == 0) continue;
        QVariantMap m;
        m.insert(QStringLiteral("key"), row.first);
        m.insert(QStringLiteral("name"), row.second);
        m.insert(QStringLiteral("count"), counts.value(row.first));
        out.append(m);
    }
    return out;
}

QVariantList Backend::scriptFacets() const {
    QHash<QString, int> counts;
    for (const FontFamily &f : m_families) for (const QString &s : f.scripts) counts[s]++;
    QStringList names = counts.keys();
    std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) {
        if (counts.value(a) != counts.value(b)) return counts.value(a) > counts.value(b);
        return caseInsensitiveLess(a, b);
    });
    QVariantList out;
    for (const QString &n : names) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), QStringLiteral("script:") + n);
        m.insert(QStringLiteral("name"), n);
        m.insert(QStringLiteral("count"), counts.value(n));
        out.append(m);
    }
    return out;
}

QVariantList Backend::tagFacets() const {
    QHash<QString, int> counts;
    for (const FontFamily &f : m_families) for (const QString &tag : f.tags) counts[tag]++;
    QVariantList out;
    for (const QString &tag : m_library.allTags()) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), QStringLiteral("tag:") + tag);
        m.insert(QStringLiteral("name"), tag);
        m.insert(QStringLiteral("count"), counts.value(tag));
        out.append(m);
    }
    return out;
}

QVariantList Backend::languages() const {
    QHash<QString, int> counts;
    for (const FontFamily &f : m_families) for (const QString &code : f.langs) counts[code]++;
    QList<QPair<QString, QString>> rows;  // name, code
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) rows.append({FontFile::languageName(it.key()), it.key()});
    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return caseInsensitiveLess(a.first, b.first); });
    QVariantList out;
    for (const auto &r : rows) {
        QVariantMap m;
        m.insert(QStringLiteral("code"), r.second);
        m.insert(QStringLiteral("name"), r.first);
        m.insert(QStringLiteral("count"), counts.value(r.second));
        out.append(m);
    }
    return out;
}

// ── Selection ───────────────────────────────────────────────────────────────

const FontFamily *Backend::family(const QString &name) const {
    for (const FontFamily &f : m_families) if (f.name == name) return &f;
    return nullptr;
}

const FontFamily *Backend::selectedFamily() const { return m_selectedName.isEmpty() ? nullptr : family(m_selectedName); }
bool Backend::hasSelection() const { return selectedFamily() != nullptr; }

const FontFileInfo *Backend::info(const QString &file) const {
    auto it = m_info.constFind(file);
    if (it != m_info.cend()) return &it.value();
    auto st = m_stagedInfo.constFind(file);
    if (st != m_stagedInfo.cend()) return &st.value();
    return nullptr;
}

const FontFileInfo *Backend::representativeInfo(const FontFamily &f) const {
    const FontFileInfo *best = info(f.representative());
    if (best) return best;
    for (const QString &file : f.files) if (const FontFileInfo *i = info(file)) return i;
    return nullptr;
}

void Backend::setSelectedName(const QString &name) {
    if (m_selectedName == name) return;
    m_selectedName = name;
    emit selectionChanged();
    emit previewChanged();
}

QVariantMap Backend::selected() const {
    const FontFamily *f = selectedFamily();
    if (!f) return {};
    QVariantMap m;
    m.insert(QStringLiteral("name"), f->name);
    m.insert(QStringLiteral("styles"), f->styles);
    m.insert(QStringLiteral("files"), f->files);
    m.insert(QStringLiteral("faces"), facesToList(f->faces));
    m.insert(QStringLiteral("user"), f->user);
    m.insert(QStringLiteral("mono"), f->mono);
    m.insert(QStringLiteral("latin"), f->latin);
    m.insert(QStringLiteral("variable"), f->variable);
    m.insert(QStringLiteral("duplicate"), f->duplicate);
    m.insert(QStringLiteral("inactive"), f->inactive());
    m.insert(QStringLiteral("disabledCount"), f->disabledCount);
    m.insert(QStringLiteral("formats"), f->formats);
    m.insert(QStringLiteral("foundry"), f->foundry);
    m.insert(QStringLiteral("langs"), f->langs);
    QStringList languageNames;
    for (const QString &code : f->langs) languageNames.append(FontFile::languageName(code));
    std::sort(languageNames.begin(), languageNames.end(), caseInsensitiveLess);
    m.insert(QStringLiteral("languages"), languageNames);
    m.insert(QStringLiteral("added"), f->added.isValid() ? f->added.toString(Qt::ISODate) : QString());
    m.insert(QStringLiteral("previewFile"), f->representative());
    m.insert(QStringLiteral("coverage"), isCoverage(f->name));
    qint64 bytes = 0;
    for (const QString &file : f->files) bytes += QFileInfo(file).size();
    m.insert(QStringLiteral("bytes"), bytes);
    m.insert(QStringLiteral("directory"), f->files.isEmpty() ? QString() : QFileInfo(f->files.first()).absolutePath());
    QStringList collectionIds;
    for (const Collection &c : m_library.collections) if (c.families.contains(f->name)) collectionIds.append(c.id);
    m.insert(QStringLiteral("collections"), collectionIds);
    m.insert(QStringLiteral("tags"), f->tags);
    m.insert(QStringLiteral("starred"), f->starred);
    m.insert(QStringLiteral("note"), f->note);
    m.insert(QStringLiteral("source"), f->source);
    m.insert(QStringLiteral("members"), f->members);
    m.insert(QStringLiteral("scripts"), f->scripts);
    m.insert(QStringLiteral("fcName"), f->fcName());
    return m;
}

QVariantMap Backend::selectedInfo() const {
    const FontFamily *f = selectedFamily();
    if (!f) return {};
    const FontFileInfo *i = representativeInfo(*f);
    QVariantMap m = i ? infoToMap(*i) : QVariantMap();
    m.insert(QStringLiteral("loaded"), i != nullptr);
    // Scripts, from the whole family's language coverage grouped by script is
    // more than fontconfig says; what it does say per face is the OpenType
    // layout tags, which the Info pane shows as scripts.
    return m;
}

// The families pinned for side-by-side comparison, with what a row needs.
QVariantList Backend::compareRows() const {
    QVariantList out;
    for (const QString &name : m_compare) {
        const FontFamily *f = family(name);
        if (!f) continue;
        QVariantMap m;
        m.insert(QStringLiteral("name"), f->name);
        m.insert(QStringLiteral("file"), f->representative());
        m.insert(QStringLiteral("styles"), f->styles.size());
        m.insert(QStringLiteral("latin"), f->latin);
        const FontFileInfo *i = representativeInfo(*f);
        m.insert(QStringLiteral("designer"), i ? i->designer : QString());
        m.insert(QStringLiteral("classification"), i ? i->classification() : QString());
        m.insert(QStringLiteral("user"), f->user);
        out.append(m);
    }
    return out;
}

void Backend::toggleCompare(const QString &familyName) {
    if (familyName.isEmpty()) return;
    if (m_compare.contains(familyName)) m_compare.removeAll(familyName);
    else { if (m_compare.size() >= 6) m_compare.removeFirst(); m_compare.append(familyName); }
    QSettings().setValue(QStringLiteral("compare"), m_compare);
    emit compareChanged();
}

void Backend::clearCompare() {
    if (m_compare.isEmpty()) return;
    m_compare.clear();
    QSettings().setValue(QStringLiteral("compare"), m_compare);
    emit compareChanged();
}

QVariantMap Backend::fileInfo(const QString &path) const {
    if (const FontFileInfo *i = info(path)) return infoToMap(*i);
    return infoToMap(FontFile::read(path));
}

void Backend::selectFirst() {
    if (m_model.rowCount() == 0) { setSelectedName(QString()); return; }
    setSelectedName(m_model.rows().first().name);
}

// ── Rows ────────────────────────────────────────────────────────────────────

bool Backend::passesSource(const FontFamily &f) const {
    if (m_source == QLatin1String("all")) return true;
    if (m_source == QLatin1String("yours")) return f.user;
    if (m_source == QLatin1String("system")) return !f.user;
    if (m_source == QLatin1String("mono")) return f.mono;
    if (m_source == QLatin1String("recent")) return f.added.isValid() && f.added >= QDateTime::currentDateTime().addDays(-kRecentDays);
    if (m_source == QLatin1String("inactive")) return f.disabledCount > 0;
    if (m_source == QLatin1String("duplicates")) return f.duplicate;
    if (m_source == QLatin1String("starred")) return f.starred;
    if (m_source.startsWith(QLatin1String("lang:"))) return f.langs.contains(m_source.mid(5));
    if (m_source.startsWith(QLatin1String("tag:"))) return f.tags.contains(m_source.mid(4));
    if (m_source.startsWith(QLatin1String("script:"))) return f.scripts.contains(m_source.mid(7));
    if (m_source.startsWith(QLatin1String("class:"))) { const FontFileInfo *i = representativeInfo(f); return i && i->classification() == m_source.mid(6); }
    if (m_source == QLatin1String("trait:Variable")) return f.variable;
    if (m_source == QLatin1String("trait:Colour")) { const FontFileInfo *i = representativeInfo(f); return i && i->color; }
    if (m_source.startsWith(QLatin1String("coll:"))) {
        for (const Collection &c : m_library.collections) if (c.id == m_source.mid(5)) return c.families.contains(f.name);
        return false;
    }
    if (m_source.startsWith(QLatin1String("smart:"))) {
        for (const SmartCollection &s : m_library.smart) if (s.id == m_source.mid(6)) return Library::matches(s, f, representativeInfo(f));
        return false;
    }
    return true;
}

bool Backend::passesFilter(const FontFamily &f) const {
    const QString needle = m_filter.trimmed();
    if (needle.isEmpty()) return true;
    if (m_searchScope == QLatin1String("characters")) return m_charsetMatches.contains(f.name);
    if (m_searchScope == QLatin1String("designer")) {
        if (f.foundry.contains(needle, Qt::CaseInsensitive)) return true;
        const FontFileInfo *i = representativeInfo(f);
        return i && (i->designer.contains(needle, Qt::CaseInsensitive) || i->manufacturer.contains(needle, Qt::CaseInsensitive)
                     || i->vendorId.compare(needle, Qt::CaseInsensitive) == 0);
    }
    if (f.name.contains(needle, Qt::CaseInsensitive)) return true;
    for (const QString &m : f.members) if (m.contains(needle, Qt::CaseInsensitive)) return true;
    if (needle.startsWith(QLatin1Char('#'))) { for (const QString &tag : f.tags) if (tag.contains(needle.mid(1), Qt::CaseInsensitive)) return true; return false; }
    for (const QString &tag : f.tags) if (tag.contains(needle, Qt::CaseInsensitive)) return true;
    for (const QString &s : f.styles) if (s.contains(needle, Qt::CaseInsensitive)) return true;
    for (const FontFace &face : f.faces)
        if (face.psName.contains(needle, Qt::CaseInsensitive) || face.fullName.contains(needle, Qt::CaseInsensitive)) return true;
    return false;
}

// Rows for the rail: user fonts first because they are the ones you can act
// on, and the language-coverage fonts folded away unless asked for. Typing a
// filter searches everything: someone who types "tamil" wants the Tamil fonts.
void Backend::rebuildRows() {
    const bool filtering = !m_filter.trimmed().isEmpty();
    const bool fold = !filtering && !m_showCoverage
                   && (m_source == QLatin1String("all") || m_source == QLatin1String("system"));
    QList<FontFamily> mine, system;
    for (const FontFamily &f : m_families) {
        if (!passesSource(f)) continue;
        if (!passesFilter(f)) continue;
        // Your own fonts are never folded away, whatever they are called.
        if (fold && isCoverage(f.name) && !f.user) continue;
        (f.user ? mine : system).append(f);
    }
    // Starred first; then the chosen order.
    auto less = [this](const FontFamily &a, const FontFamily &b) {
        if (a.starred != b.starred) return a.starred;
        if (m_sortOrder == QLatin1String("added")) {
            if (a.added.isValid() != b.added.isValid()) return a.added.isValid();
            if (a.added != b.added) return a.added > b.added;
        } else if (m_sortOrder == QLatin1String("styles")) {
            if (a.styles.size() != b.styles.size()) return a.styles.size() > b.styles.size();
        } else if (m_sortOrder == QLatin1String("foundry")) {
            const int c = a.foundry.compare(b.foundry, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
        }
        return caseInsensitiveLess(a.name, b.name);
    };
    std::stable_sort(mine.begin(), mine.end(), less);
    std::stable_sort(system.begin(), system.end(), less);
    m_model.setRows(mine + system);
    emit countsChanged();
}

// ── Scanning ────────────────────────────────────────────────────────────────

void Backend::startBounded(QProcess *process, const QString &program, const QStringList &args, int timeoutMs) {
    process->setProcessEnvironment(safeEnvironment());
    process->setProgram(program);
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(timeoutMs);
    connect(timer, &QTimer::timeout, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
    connect(process, &QProcess::finished, timer, &QTimer::stop);
    // A child that never stops talking must not grow inside this process.
    connect(process, &QProcess::readyReadStandardOutput, process, [process] {
        if (process->bytesAvailable() > kOutputCap + 1) process->closeReadChannel(QProcess::StandardOutput);
    });
    timer->start();
    process->start(QIODevice::ReadOnly);
}

QString Backend::readBounded(QProcess *process, bool *truncated) const {
    QByteArray bytes = process->readAllStandardOutput();
    if (truncated) *truncated = bytes.size() > kOutputCap;
    if (bytes.size() > kOutputCap) bytes.truncate(kOutputCap);
    return QString::fromUtf8(bytes);
}

void Backend::refresh() {
    // A change made while fc-list runs is not in its answer: scan again after.
    if (m_scanning) { m_rescanPending = true; return; }
    m_rescanPending = false;
    setScanning(true);
    m_scanDone = m_latinDone = false;

    if (m_scanProcess) m_scanProcess->deleteLater();
    m_scanProcess = new QProcess(this);
    connect(m_scanProcess, &QProcess::finished, this, &Backend::onScanFinished);
    connect(m_scanProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) onScanFinished();
    });
    startBounded(m_scanProcess, QStringLiteral("fc-list"), {QStringLiteral("--format=") + fcFormat()}, 30000);

    // Families that can set Latin text, per fontconfig's own language coverage.
    // `%{lang}` tells us which languages; `:lang=en` is cheaper than checking
    // every family's list for "en", and it is what the rail's self-set names
    // are gated on.
    if (m_latinProcess) m_latinProcess->deleteLater();
    m_latinProcess = new QProcess(this);
    connect(m_latinProcess, &QProcess::finished, this, &Backend::onLatinFinished);
    connect(m_latinProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) onLatinFinished();
    });
    startBounded(m_latinProcess, QStringLiteral("fc-list"), {QStringLiteral(":lang=en"), QStringLiteral("--format=%{family[0]}\n")}, 30000);
}

void Backend::onScanFinished() {
    if (m_scanDone) return;
    m_scanDone = true;
    m_scanOutput = readBounded(m_scanProcess, &m_scanTruncated);
    if ((m_scanProcess->exitStatus() != QProcess::NormalExit || m_scanProcess->exitCode() != 0) && m_scanOutput.isEmpty())
        setStatus(tr("fc-list failed. Is fontconfig installed?"), true);
    finishScanIfDone();
}

void Backend::onLatinFinished() {
    if (m_latinDone) return;
    m_latinDone = true;
    m_latinOutput = readBounded(m_latinProcess, nullptr);
    finishScanIfDone();
}

void Backend::finishScanIfDone() {
    if (!m_scanDone || !m_latinDone) return;

    m_latin.clear();
    for (const QString &line : m_latinOutput.split(QLatin1Char('\n'))) {
        const QString n = line.trimmed();
        if (!n.isEmpty()) m_latin.insert(n);
    }
    m_latinOutput.clear();

    m_families = parseFcList(m_scanOutput, m_fontRoot, &m_scanTruncated);
    m_scanOutput.clear();
    injectDisabled(m_families);
    for (FontFamily &f : m_families) f.latin = m_latin.contains(f.name) || f.langs.contains(QStringLiteral("en"));
    if (m_groupWeights) groupSuperfamilies(m_families);
    for (FontFamily &f : m_families) f.scripts = scriptsFor(f.langs);
    applyLibrary(m_families);
    markDuplicates(m_families);
    markSymbolic();

    if (m_scanTruncated) setStatus(tr("Font list truncated: more fonts here than OmaFont will show"), true);

    rebuildRows();

    if (!m_pendingSelect.isEmpty()) {
        if (family(m_pendingSelect)) setSelectedName(m_pendingSelect);
        m_pendingSelect.clear();
    } else if (!m_selectedName.isEmpty() && !family(m_selectedName)) {
        setSelectedName(QString());
    }
    if (m_selectedName.isEmpty() && !staged() && !staging()) selectFirst();

    emit selectionChanged();
    emit previewChanged();
    setScanning(false);
    emit scanFinished();
    startMetadata();
    if (m_rescanPending) refresh();
}

// fontconfig says a dingbat font "covers" English because its letters are
// mapped, to pictures. The file's own class settles it once it has been read.
bool Backend::markSymbolic() {
    bool changed = false;
    for (FontFamily &f : m_families) {
        if (!f.latin) continue;
        const FontFileInfo *i = representativeInfo(f);
        if (i && i->classification() == QLatin1String("Symbolic")) { f.latin = false; changed = true; }
    }
    return changed;
}

// Search by character: fontconfig answers ":charset=<hex>,<hex>" itself.
void Backend::startCharsetSearch() {
    const QString needle = m_filter.trimmed();
    if (m_charsetProcess) { m_charsetProcess->disconnect(this); if (m_charsetProcess->state() != QProcess::NotRunning) m_charsetProcess->kill(); m_charsetProcess->deleteLater(); m_charsetProcess = nullptr; }
    if (needle.isEmpty()) { m_charsetMatches.clear(); m_charsetPending = false; rebuildRows(); return; }
    QStringList hex;
    // Code points, not UTF-16 units: 😀 is 1f600, not d83d,de00.
    for (const char32_t cp : needle.toUcs4()) {
        if (QChar::isSpace(cp)) continue;
        hex.append(QString::number(quint32(cp), 16));
    }
    if (hex.isEmpty()) { m_charsetMatches.clear(); rebuildRows(); return; }
    m_charsetPending = true;
    m_charsetProcess = new QProcess(this);
    connect(m_charsetProcess, &QProcess::finished, this, [this] {
        m_charsetMatches.clear();
        for (const QString &line : readBounded(m_charsetProcess, nullptr).split(QLatin1Char('\n'))) {
            const QString n = line.trimmed();
            if (!n.isEmpty()) m_charsetMatches.insert(n);
        }
        m_charsetPending = false;
        rebuildRows();
        emit charsetSearchFinished();
    });
    connect(m_charsetProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_charsetMatches.clear();
        m_charsetPending = false;
        rebuildRows();
        emit charsetSearchFinished();
    });
    startBounded(m_charsetProcess, QStringLiteral("fc-list"),
                 {QStringLiteral(":charset=") + hex.join(QLatin1Char(',')), QStringLiteral("--format=%{family[0]}\n")}, 30000);
}

// ── Metadata from the files ─────────────────────────────────────────────────

// Every file's name/OS/2/maxp tables, read on a worker after each scan. A few
// thousand small reads; results land in batches so the Info pane fills in as
// they arrive, and a rescan cancels the run in flight.
void Backend::startMetadata() {
    stopMetadata();
    QStringList files;
    for (const FontFamily &f : m_families)
        for (const QString &file : f.files)
            if (!m_info.contains(file)) files.append(file);
    if (files.isEmpty()) { if (m_readingMetadata) { m_readingMetadata = false; emit metadataChanged(); } emit metadataFinished(); return; }
    // Faces of a collection file share the file; read face 0.
    m_readingMetadata = true;
    emit metadataChanged();
    const int generation = ++m_metaGeneration;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_metaCancel = cancel;
    m_metaThread = QThread::create([this, files, cancel, generation] {
        QHash<QString, FontFileInfo> batch;
        int done = 0;
        for (const QString &file : files) {
            if (cancel->load()) return;
            batch.insert(file, FontFile::read(file));
            if (++done % 150 == 0 || done == files.size()) {
                QHash<QString, FontFileInfo> deliver;
                deliver.swap(batch);
                const bool last = done == files.size();
                QMetaObject::invokeMethod(this, [this, deliver, generation, last] {
                    if (generation != m_metaGeneration) return;
                    for (auto it = deliver.cbegin(); it != deliver.cend(); ++it) m_info.insert(it.key(), it.value());
                    emit selectionChanged();
                    if (last) {
                        m_readingMetadata = false;
                        emit metadataChanged();
                        const bool symbolic = markSymbolic();
                        if (symbolic || m_source.startsWith(QLatin1String("smart:")) || m_source.startsWith(QLatin1String("class:"))
                            || m_source.startsWith(QLatin1String("trait:")) || m_searchScope == QLatin1String("designer")) rebuildRows();
                        emit countsChanged();
                        emit libraryChanged();
                        emit compareChanged();
                        emit metadataFinished();
                    }
                }, Qt::QueuedConnection);
            }
        }
    });
    // The thread deletes itself once it has finished, never sooner. A stopped
    // worker is not waited for: it notices the cancel flag after the file in
    // hand, and the generation check drops anything it still delivers. Only
    // the destructor waits, because the worker posts back to this object.
    QThread *thread = m_metaThread;
    m_metaThreads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_metaThread == thread) m_metaThread = nullptr;
        m_metaThreads.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

void Backend::stopMetadata() {
    if (m_metaCancel) m_metaCancel->store(true);
    m_metaThread = nullptr;
    ++m_metaGeneration;
}
