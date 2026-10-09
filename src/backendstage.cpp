#include "backend.h"
#include "filepicker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <filesystem>
#include <system_error>

// Staging, installing, removing and the terminal font: everything that
// changes what is on disk.

QVariantMap faceToMap(const FontFace &face);
QVariantList facesToList(const QList<FontFace> &faces);

bool Backend::staging() const {
    return (m_extractProcess && m_extractProcess->state() != QProcess::NotRunning)
        || (m_stageScanProcess && m_stageScanProcess->state() != QProcess::NotRunning)
        || !m_stageArchives.isEmpty();
}

QString Backend::expandHome(const QString &path) const {
    if (path == QLatin1String("~")) return QDir::homePath();
    if (path.startsWith(QLatin1String("~/"))) return QDir::homePath() + path.mid(1);
    return path;
}

void Backend::stageUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
        else if (url.scheme().isEmpty()) paths.append(url.toString());
    }
    stage(paths);
}

// Accepts any mix of files, folders and archives: a dropped selection, the
// picker's answer, or the command line. Archives are unpacked into a private
// temp directory first, then fc-scan walks every target in one pass.
void Backend::stage(const QStringList &paths) {
    QStringList clean;
    for (const QString &raw : paths) {
        QString p = raw.trimmed();
        if (p.startsWith(QLatin1String("file://"))) p = QUrl(p).toLocalFile();
        p = expandHome(p);
        if (p.isEmpty() || clean.size() >= 64) continue;
        clean.append(QDir::cleanPath(p));
    }
    if (clean.isEmpty()) return;

    clearStage();
    setSelectedName(QString());
    m_stagedSource = clean.size() == 1 ? QFileInfo(clean.first()).fileName() : tr("%1 items").arg(clean.size());
    setStatus(tr("Reading %1…").arg(m_stagedSource));

    for (const QString &p : clean) {
        const QFileInfo info(p);
        if (!info.exists()) continue;
        if (info.isDir()) m_stageTargets.append(p);
        else if (isArchive(p)) m_stageArchives.append(p);
        else if (isDesktopFont(p)) m_stageTargets.append(p);
    }
    if (m_stageTargets.isEmpty() && m_stageArchives.isEmpty()) {
        m_stagedSource.clear();
        setStatus(tr("Nothing there to install"), true);
        emit stageChanged();
        return;
    }
    emit stageChanged();
    extractNext();
}

// Archives come out through bsdtar (libarchive, which every Arch system
// carries for pacman): zip, tar in any compression, 7z. libarchive refuses
// absolute paths and `..` members unless told otherwise.
void Backend::extractNext() {
    if (m_stageArchives.isEmpty()) { scanStage(); return; }
    if (!m_stageTemp) {
        m_stageTemp = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/omafont-stage-XXXXXX"));
        if (!m_stageTemp->isValid()) {
            m_stageArchives.clear();
            setStatus(tr("Could not make a temporary folder to unpack into"), true);
            emit stageChanged();
            return;
        }
    }
    const QString archive = m_stageArchives.takeFirst();
    // Two "Fonts.zip" from different folders, or a.tar.gz beside a.tar.xz,
    // must not unpack over each other.
    const QString base = QFileInfo(archive).completeBaseName();
    QString target = m_stageTemp->filePath(base);
    for (int n = 2; QFileInfo::exists(target); ++n) target = m_stageTemp->filePath(QStringLiteral("%1-%2").arg(base).arg(n));
    QDir().mkpath(target);

    // This runs from the previous process's own finished signal, so it may
    // only be let go of later, never deleted under its emitter.
    if (m_extractProcess) m_extractProcess->deleteLater();
    m_extractProcess = new QProcess(this);
    connect(m_extractProcess, &QProcess::finished, this, [this, target, archive](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) setStatus(tr("Could not unpack %1").arg(QFileInfo(archive).fileName()), true);
        m_stageTargets.append(target);
        extractNext();
    });
    // A crash or a timeout kill still ends in finished(); only a program that
    // never started needs handling here, or the next step would run twice.
    connect(m_extractProcess, &QProcess::errorOccurred, this, [this, archive](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        setStatus(tr("Could not unpack %1: bsdtar is missing").arg(QFileInfo(archive).fileName()), true);
        extractNext();
    });
    startBounded(m_extractProcess, QStringLiteral("bsdtar"),
                 {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target,
                  QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")}, 60000);
    emit stageChanged();
}

void Backend::scanStage() {
    if (m_stageTargets.isEmpty()) { setStatus(tr("Nothing there to install"), true); emit stageChanged(); return; }
    if (m_stageScanProcess) m_stageScanProcess->deleteLater();
    m_stageScanProcess = new QProcess(this);
    connect(m_stageScanProcess, &QProcess::finished, this, &Backend::onStageScanFinished);
    connect(m_stageScanProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) onStageScanFinished();
    });
    QStringList args{QStringLiteral("--format=") + fcFormat()};
    args += m_stageTargets;
    startBounded(m_stageScanProcess, QStringLiteral("fc-scan"), args, 30000);
    emit stageChanged();
}

void Backend::onStageScanFinished() {
    bool truncated = false;
    QString text = readBounded(m_stageScanProcess, &truncated);
    if (truncated) text.truncate(text.lastIndexOf(QLatin1Char('\n')) + 1);  // drop the half line
    QList<FontFace> faces = parseFcScan(text);
    if (faces.isEmpty()) {
        const bool failed = m_stageScanProcess->exitStatus() != QProcess::NormalExit || m_stageScanProcess->exitCode() != 0;
        clearStage();
        setStatus(failed ? tr("fc-scan failed. Is fontconfig installed?") : tr("No installable fonts found in that"), true);
        return;
    }
    if (faces.size() > kMaxFaces) { truncated = true; faces = faces.mid(0, kMaxFaces); }
    m_stagedFaces = faces;
    m_stagedFormat = QStringLiteral("all");
    m_stagedTruncated = truncated;
    m_conflictPolicy = QStringLiteral("replace");
    m_installInvalid = false;
    validateStagedFaces();
    const int problems = stagedProblems();
    setStatus(truncated ? tr("Showing the first %1 fonts; that source holds more").arg(faces.size())
            : problems > 0 ? tr("%1 of %2 fonts have problems; see the report").arg(problems).arg(faces.size())
            : faces.size() == 1 ? tr("1 font ready to install")
            : tr("%1 fonts ready to install").arg(faces.size()));
    emit stageChanged();
    emit previewChanged();
    emit stageFinished();
}

// Validate before install. Each staged file is parsed; the
// structural findings decide whether it is installed by default.
void Backend::validateStagedFaces() {
    m_stagedInfo.clear();
    QSet<QString> files;
    for (const FontFace &f : m_stagedFaces) files.insert(f.file);
    if (files.size() > 600) return;  // a giant drop: validated on demand, not up front
    for (const QString &file : files) m_stagedInfo.insert(file, FontFile::read(file));
}

void Backend::releaseStageTemp() { m_stageTemp.reset(); }

void Backend::clearStage() {
    m_picking = false;
    m_stageArchives.clear();
    for (QProcess *p : {m_extractProcess, m_stageScanProcess}) {
        if (!p) continue;
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(500); }
        p->deleteLater();
    }
    m_extractProcess = nullptr;
    m_stageScanProcess = nullptr;
    m_stageTargets.clear();
    m_stagedFaces.clear();
    m_stagedInfo.clear();
    m_stagedSource.clear();
    m_stagedFormat = QStringLiteral("all");
    m_stagedTruncated = false;
    m_conflictPolicy = QStringLiteral("replace");
    m_installInvalid = false;
    releaseStageTemp();
    emit stageChanged();
    emit previewChanged();
}

// A staged face, with what validation and the destination say about it.
static QVariantMap stagedFaceMap(const Backend &b, const FontFace &face, const QHash<QString, FontFileInfo> &infos,
                                 const QString &destDir) {
    QVariantMap m = faceToMap(face);
    const auto it = infos.constFind(face.file);
    const bool checked = it != infos.cend();
    m.insert(QStringLiteral("checked"), checked);
    m.insert(QStringLiteral("valid"), !checked || (it->ok && it->problems.isEmpty()));
    m.insert(QStringLiteral("problems"), checked ? (it->ok ? it->problems : QStringList{it->error}) : QStringList());
    m.insert(QStringLiteral("warnings"), checked ? it->warnings : QStringList());
    m.insert(QStringLiteral("glyphCount"), checked ? it->glyphCount : 0);
    m.insert(QStringLiteral("kind"), checked ? it->kind : face.format);
    m.insert(QStringLiteral("exists"), !destDir.isEmpty() && QFileInfo::exists(destDir + QLatin1Char('/') + QFileInfo(face.file).fileName()));
    Q_UNUSED(b);
    return m;
}

QVariantList Backend::stagedFaces() const {
    const QString destDir = m_fontRoot + QLatin1Char('/') + slug(stagedGroup());
    QVariantList out;
    for (const FontFace &f : m_stagedFaces) out.append(stagedFaceMap(*this, f, m_stagedInfo, destDir));
    return out;
}

// Packs often ship the same faces as both OTF and TTF; the two formats
// frequently disagree about family names and cannot be merged reliably, so the
// person picks one or takes both.
QVariantList Backend::stagedSelected() const {
    const QString destDir = m_fontRoot + QLatin1Char('/') + slug(stagedGroup());
    QVariantList out;
    for (const FontFace &f : m_stagedFaces) {
        const QString ext = extensionOf(f.file);
        if (m_stagedFormat == QLatin1String("otf") && ext != QLatin1String("otf")) continue;
        if (m_stagedFormat == QLatin1String("ttf") && ext == QLatin1String("otf")) continue;
        out.append(stagedFaceMap(*this, f, m_stagedInfo, destDir));
    }
    return out;
}

QStringList Backend::stagedFamilies() const {
    QStringList out;
    for (const FontFace &f : m_stagedFaces) {
        const QString ext = extensionOf(f.file);
        if (m_stagedFormat == QLatin1String("otf") && ext != QLatin1String("otf")) continue;
        if (m_stagedFormat == QLatin1String("ttf") && ext == QLatin1String("otf")) continue;
        if (!out.contains(f.family)) out.append(f.family);
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return out;
}

QString Backend::stagedGroup() const { return commonGroup(stagedFamilies()); }

void Backend::setStagedFormat(const QString &format) {
    const QString f = (format == QLatin1String("otf") || format == QLatin1String("ttf")) ? format : QStringLiteral("all");
    if (m_stagedFormat == f) return;
    m_stagedFormat = f;
    emit stageChanged();
    emit previewChanged();
}

void Backend::setConflictPolicy(const QString &policy) {
    const QString p = (policy == QLatin1String("skip") || policy == QLatin1String("keep")) ? policy : QStringLiteral("replace");
    if (m_conflictPolicy == p) return;
    m_conflictPolicy = p;
    emit stageChanged();
}

void Backend::setInstallInvalid(bool on) {
    if (m_installInvalid == on) return;
    m_installInvalid = on;
    emit stageChanged();
}

int Backend::stagedOtfCount() const {
    return std::count_if(m_stagedFaces.cbegin(), m_stagedFaces.cend(), [](const FontFace &f) { return extensionOf(f.file) == QLatin1String("otf"); });
}
int Backend::stagedTtfCount() const { return m_stagedFaces.size() - stagedOtfCount(); }

int Backend::stagedProblems() const {
    int n = 0;
    for (const QVariant &v : stagedSelected()) if (!v.toMap().value(QStringLiteral("valid")).toBool()) ++n;
    return n;
}
int Backend::stagedWarnings() const {
    int n = 0;
    for (const QVariant &v : stagedSelected()) if (!v.toMap().value(QStringLiteral("warnings")).toStringList().isEmpty()) ++n;
    return n;
}
int Backend::stagedExisting() const {
    int n = 0;
    for (const QVariant &v : stagedSelected()) if (v.toMap().value(QStringLiteral("exists")).toBool()) ++n;
    return n;
}

bool Backend::stagedInstalled() const {
    const QStringList fams = stagedFamilies();
    if (fams.isEmpty()) return false;
    for (const QString &n : fams) if (!family(n)) return false;
    return true;
}

// ── Preview ─────────────────────────────────────────────────────────────────

QString Backend::previewFile() const {
    if (!m_stagedFaces.isEmpty()) {
        QStringList files;
        for (const QVariant &v : stagedSelected()) files.append(v.toMap().value(QStringLiteral("file")).toString());
        return regularFace(files);
    }
    const FontFamily *f = selectedFamily();
    return f ? f->representative() : QString();
}

QString Backend::previewTitle() const {
    if (!m_stagedFaces.isEmpty()) return stagedGroup();
    const FontFamily *f = selectedFamily();
    return f ? f->name : QString();
}

// ── Install ─────────────────────────────────────────────────────────────────

// "Inter-Regular.ttf" already there → "Inter-Regular-2.ttf", and so on.
QString Backend::keepBothName(const QString &dir, const QString &fileName) {
    const QFileInfo fi(fileName);
    const QString base = fi.completeBaseName();
    const QString ext = fi.suffix();
    for (int n = 2; n < 1000; ++n) {
        const QString candidate = QStringLiteral("%1-%2.%3").arg(base).arg(n).arg(ext);
        if (!QFileInfo::exists(dir + QLatin1Char('/') + candidate)) return candidate;
    }
    return fileName;
}

// Copies the faces into <font root>/<group>/. Nothing is trusted from the
// caller: the extension check and the single-segment destination are repeated
// here, the destination may not be a symlink, and what happens to a file that
// is already there follows the conflict policy: replace, skip, or keep both.
QString Backend::installFaces(const QString &group, const QList<FontFace> &faces, QString *installedDir, int *written, int *skipped) {
    const QString dirName = slug(group);
    if (dirName.contains(QLatin1Char('/')) || dirName == QLatin1String(".") || dirName == QLatin1String("..")) return tr("Refusing an unsafe folder name");
    if (faces.isEmpty()) return tr("Nothing to install");

    QDir root(m_fontRoot);
    if (!root.mkpath(QStringLiteral("."))) return tr("Could not create %1").arg(m_fontRoot);
    if (QFileInfo(m_fontRoot).isSymLink()) return tr("%1 is a link; refusing to write through it").arg(m_fontRoot);

    const QString dest = m_fontRoot + QLatin1Char('/') + dirName;
    const QFileInfo destInfo(dest);
    if (destInfo.exists() && (destInfo.isSymLink() || !destInfo.isDir())) return tr("%1 exists and is not a folder").arg(dest);
    if (!QDir().mkpath(dest)) return tr("Could not create %1").arg(dest);

    int copied = 0, passed = 0;
    QSet<QString> done;
    for (const FontFace &face : faces) {
        // fc-scan gives a line per face: a variable font or a .ttc is one
        // file listed many times, and is copied once.
        if (done.contains(face.file)) continue;
        done.insert(face.file);
        if (!isDesktopFont(face.file)) continue;
        const QFileInfo src(face.file);
        if (!src.isFile()) continue;
        QString name = src.fileName();
        QString target = dest + QLatin1Char('/') + name;
        const QFileInfo targetInfo(target);
        if (targetInfo.isSymLink()) return tr("%1 is a link; refusing to replace it").arg(target);
        if (targetInfo.exists()) {
            // A font dropped back in from where it is installed is already
            // there; "replacing" it would delete the only copy.
            if (targetInfo.canonicalFilePath() == src.canonicalFilePath()) { ++passed; continue; }
            if (m_conflictPolicy == QLatin1String("skip")) { ++passed; continue; }
            if (m_conflictPolicy == QLatin1String("keep")) {
                name = keepBothName(dest, name);
                target = dest + QLatin1Char('/') + name;
            }
        }
        // Copy beside the target and rename over it, so a failed copy leaves
        // the old file where it was.
        const QString part = dest + QStringLiteral("/.omafont-") + name + QStringLiteral(".part");
        QFile::remove(part);
        if (!QFile::copy(face.file, part)) { QFile::remove(part); return tr("Could not copy %1").arg(src.fileName()); }
        QFile::setPermissions(part, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        std::error_code ec;
        std::filesystem::rename(QFile::encodeName(part).toStdString(), QFile::encodeName(target).toStdString(), ec);
        if (ec) { QFile::remove(part); return tr("Could not replace %1").arg(name); }
        m_info.remove(target);
        ++copied;
    }
    if (written) *written = copied;
    if (skipped) *skipped = passed;
    if (copied == 0 && passed == 0) return tr("None of those files could be installed");
    if (installedDir) *installedDir = dest;
    return {};
}

void Backend::installStaged() {
    if (m_stagedFaces.isEmpty() || m_busy) return;
    QList<FontFace> faces;
    int invalid = 0;
    for (const QVariant &v : stagedSelected()) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("valid")).toBool() && !m_installInvalid) { ++invalid; continue; }
        FontFace f;
        f.family = m.value(QStringLiteral("family")).toString();
        f.style = m.value(QStringLiteral("style")).toString();
        f.file = m.value(QStringLiteral("file")).toString();
        faces.append(f);
    }
    if (faces.isEmpty()) {
        setStatus(invalid > 0 ? tr("Nothing installed: every chosen font has problems. Tick “install anyway” to force it.") : tr("Nothing to install"), true);
        return;
    }
    const QString group = stagedGroup().isEmpty() ? QStringLiteral("Custom") : stagedGroup();
    const QStringList families = stagedFamilies();

    setBusy(true);
    setStatus(tr("Installing %1…").arg(group));
    QString dir;
    int written = 0, skipped = 0;
    const QString error = installFaces(group, faces, &dir, &written, &skipped);
    if (!error.isEmpty()) {
        setBusy(false);
        setStatus(tr("Install failed: %1").arg(error), true);
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &fam : families) {
        m_library.added.insert(fam, now);
        if (!m_stagedSource.isEmpty()) m_library.sources.insert(fam, m_stagedSource);
    }
    // The group name is what the rail will show once weights are folded.
    m_library.added.insert(group, now);
    if (!m_stagedSource.isEmpty()) m_library.sources.insert(group, m_stagedSource);
    saveLibrary();

    // Select the family the specimen was showing, so the list lands where the
    // eye already is rather than on whatever sorts first.
    m_pendingSelect = faces.first().family;
    clearStage();
    QString note = written == 1 ? tr("Installed %1.").arg(group) : tr("Installed %1 (%2 fonts).").arg(group).arg(written);
    if (skipped > 0) note += tr(" %1 already there, left alone.").arg(skipped);
    if (invalid > 0) note += tr(" %1 with problems not installed.").arg(invalid);
    note += tr(" Apps already running see it after a restart.");
    setStatus(note);
    emit installed(group);
    runFontCache(dir);
}

// fc-cache only builds an index, but a stale one is why a just-installed font
// is "not there" in the next app to open. The whole user font root is
// re-indexed, not just the folder that changed: fontconfig trusts a directory
// cache written in the same second as the directory last changed, so caching
// only the new subfolder can leave the parent's listing stale.
void Backend::runFontCache(const QString &dir) {
    Q_UNUSED(dir);
    if (m_cacheProcess) m_cacheProcess->deleteLater();
    m_cacheProcess = new QProcess(this);
    auto done = [this] { setBusy(false); refresh(); };
    connect(m_cacheProcess, &QProcess::finished, this, done);
    connect(m_cacheProcess, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) done();
    });
    QDir().mkpath(m_fontRoot);
    startBounded(m_cacheProcess, QStringLiteral("fc-cache"), {QStringLiteral("-f"), QStringLiteral("--"), m_fontRoot}, 120000);
}

// ── Remove ──────────────────────────────────────────────────────────────────

// A family's files that are yours and that no other family also lists: a .ttc
// shared by two families stays until both are removed.
QStringList Backend::removableFiles(const FontFamily &family) const {
    QSet<QString> elsewhere;
    for (const FontFamily &other : m_families)
        if (other.name != family.name)
            for (const QString &file : other.files) elsewhere.insert(file);
    QStringList out;
    for (const QString &file : family.files)
        if (file.startsWith(m_fontRoot + QLatin1Char('/')) && !elsewhere.contains(file)) out.append(file);
    return out;
}

QString Backend::removeFamilyFiles(const QStringList &files) {
    const QString rootCanonical = QFileInfo(m_fontRoot).canonicalFilePath();
    if (rootCanonical.isEmpty()) return tr("%1 does not exist").arg(m_fontRoot);
    const QString prefix = rootCanonical + QLatin1Char('/');
    // Every file is checked before any is touched, so a refusal never
    // leaves a family half removed.
    for (const QString &file : files) {
        const QFileInfo info(file);
        if (!file.startsWith(m_fontRoot + QLatin1Char('/'))) return tr("%1 is outside your font folder").arg(file);
        if (info.isSymLink()) return tr("%1 is a link; refusing to follow it").arg(file);
        const QString dirCanonical = QFileInfo(info.absolutePath()).canonicalFilePath();
        if (dirCanonical != rootCanonical && !dirCanonical.startsWith(prefix)) return tr("%1 resolves outside your font folder").arg(file);
    }
    int removed = 0;
    QSet<QString> parents;
    for (const QString &file : files) {
        const QFileInfo info(file);
        if (!info.exists()) continue;
        if (!QFile::remove(file)) return tr("Could not remove %1").arg(info.fileName());
        m_info.remove(file);
        ++removed;
        parents.insert(info.absolutePath());
    }
    if (removed == 0) return tr("Nothing was removed");
    for (const QString &dir : parents) {
        if (dir == m_fontRoot || !dir.startsWith(m_fontRoot + QLatin1Char('/'))) continue;
        if (QDir(dir).isEmpty()) QDir().rmdir(dir);
    }
    return {};
}

void Backend::removeSelected() {
    const FontFamily *f = selectedFamily();
    if (!f || !f->user || m_busy) return;
    const QStringList files = removableFiles(*f);
    if (files.isEmpty()) { setStatus(tr("Those files are shared with another family; nothing removed"), true); return; }
    const QString name = f->name;
    setBusy(true);
    setStatus(tr("Removing %1…").arg(name));
    const QString error = removeFamilyFiles(files);
    if (!error.isEmpty()) {
        setBusy(false);
        setStatus(tr("Remove failed: %1").arg(error), true);
        return;
    }
    // A removed file cannot stay deactivated, or remembered as added.
    bool libraryTouched = false;
    for (int i = m_library.disabled.size() - 1; i >= 0; --i)
        if (files.contains(m_library.disabled.at(i).file)) { m_library.disabled.removeAt(i); libraryTouched = true; }
    if (m_library.added.remove(name) > 0) libraryTouched = true;
    if (m_library.sources.remove(name) > 0) libraryTouched = true;
    if (m_library.tags.remove(name) > 0) libraryTouched = true;
    if (m_library.notes.remove(name) > 0) libraryTouched = true;
    if (m_library.starred.removeAll(name) > 0) libraryTouched = true;
    for (Collection &c : m_library.collections) if (c.families.removeAll(name) > 0) libraryTouched = true;
    QString libError;
    if (libraryTouched) {
        if (!writeFontconfig().isEmpty()) libError = tr("; the fontconfig file could not be updated");
        saveLibrary();
        emit libraryChanged();
    }
    setSelectedName(QString());
    setStatus(tr("Removed %1").arg(name) + libError, !libError.isEmpty());
    emit removed(name);
    runFontCache(QString());
}

// ── Terminal font ───────────────────────────────────────────────────────────

void Backend::setAsTerminalFont() {
    const FontFamily *f = selectedFamily();
    if (!f || !f->mono || m_fontSetTool.isEmpty() || m_busy) return;
    const QString name = f->fcName();
    if (m_fontSetProcess) m_fontSetProcess->deleteLater();
    m_fontSetProcess = new QProcess(this);
    setBusy(true);
    setStatus(tr("Setting the terminal font to %1…").arg(name));
    connect(m_fontSetProcess, &QProcess::finished, this, [this, name](int code, QProcess::ExitStatus status) {
        setBusy(false);
        if (status == QProcess::NormalExit && code == 0) setStatus(tr("Terminal font set to %1").arg(name));
        else setStatus(tr("omarchy-font-set failed for %1").arg(name), true);
    });
    connect(m_fontSetProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        setBusy(false);
        setStatus(tr("omarchy-font-set could not be run"), true);
    });
    startBounded(m_fontSetProcess, m_fontSetTool, {name}, 60000);
}

// ── Picker ──────────────────────────────────────────────────────────────────

void Backend::openPicker() {
    if (m_picking) return;
    m_picking = true;
    m_pendingPdfKind.clear();
    m_picker->openFiles(tr("Choose fonts, a folder or an archive"), tr("Fonts and archives"),
                        {QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc"),
                         QStringLiteral("*.TTF"), QStringLiteral("*.OTF"), QStringLiteral("*.TTC"),
                         QStringLiteral("*.zip"), QStringLiteral("*.ZIP"), QStringLiteral("*.tar.gz"),
                         QStringLiteral("*.tar.xz"), QStringLiteral("*.tgz"), QStringLiteral("*.7z")});
}
