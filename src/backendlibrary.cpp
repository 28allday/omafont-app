#include "backend.h"
#include "filepicker.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSaveFile>
#include <algorithm>

// The library: collections, smart collections, deactivated faces and install
// dates. Everything OmaFont remembers that is not a font file.

// A library that would not read is never written over: what is in memory is
// then empty, and saving it would throw away every collection, tag and note.
void Backend::saveLibrary() {
    if (m_libraryUnreadable) { setStatus(tr("Not saving: the library file could not be read. Fix or move %1, then restart.").arg(m_libraryPath), true); return; }
    QString error;
    if (!m_library.save(m_libraryPath, &error)) setStatus(tr("Could not save the library: %1").arg(error), true);
}

// The fontconfig fragment that hides deactivated faces from every application.
// Written whenever the set changes; removed when it is empty so a clean system
// has no OmaFont file in it.
QString Backend::writeFontconfig() {
    // Without the library, the faces already hidden are unknown; rewriting
    // the file would bring them all back.
    if (m_libraryUnreadable) return tr("the library file could not be read");
    const QString path = Library::fontconfigPath();
    if (m_library.disabled.isEmpty()) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) return tr("Could not remove %1").arg(path);
        return {};
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return f.errorString();
    f.write(m_library.fontconfigXml().toUtf8());
    if (!f.commit()) return f.errorString();
    return {};
}

// fontconfig stops listing a rejected file, so the deactivated faces come
// back into the list from what the library remembers about them.
void Backend::injectDisabled(QList<FontFamily> &families) const {
    const QString userPrefix = m_fontRoot + QLatin1Char('/');
    for (const DisabledFace &d : m_library.disabled) {
        if (!QFileInfo::exists(d.file)) continue;
        FontFace face;
        face.family = d.family;
        face.style = d.style;
        face.file = d.file;
        face.mono = d.mono;
        face.disabled = true;
        face.user = d.file.startsWith(userPrefix);
        face.format = extensionOf(d.file) == QLatin1String("otf") ? QStringLiteral("CFF") : QStringLiteral("TrueType");
        FontFamily *target = nullptr;
        for (FontFamily &f : families) if (f.name == d.family) { target = &f; break; }
        if (!target) {
            FontFamily fresh;
            fresh.name = d.family;
            families.append(fresh);
            target = &families.last();
        }
        bool already = false;
        for (FontFace &existing : target->faces)
            if (existing.file == d.file && existing.style == d.style) { existing.disabled = true; already = true; }
        if (!already) {
            target->faces.append(face);
            if (!target->files.contains(d.file)) target->files.append(d.file);
            if (!d.style.isEmpty() && !target->styles.contains(d.style)) target->styles.append(d.style);
        }
        if (face.user) target->user = true;
        if (d.mono) target->mono = true;
    }
    for (FontFamily &f : families) {
        f.disabledCount = std::count_if(f.faces.cbegin(), f.faces.cend(), [](const FontFace &x) { return x.disabled; });
    }
    std::sort(families.begin(), families.end(), [](const FontFamily &a, const FontFamily &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

void Backend::applyLibrary(QList<FontFamily> &families) const {
    for (FontFamily &f : families) {
        // A superfamily inherits what any of its members was given.
        QStringList names{f.name};
        names += f.members;
        for (const QString &n : names) {
            if (!f.added.isValid() && m_library.added.contains(n)) f.added = m_library.added.value(n);
            if (f.source.isEmpty()) f.source = m_library.sources.value(n);
            for (const QString &tag : m_library.tags.value(n)) if (!f.tags.contains(tag)) f.tags.append(tag);
            if (m_library.starred.contains(n)) f.starred = true;
            if (f.note.isEmpty()) f.note = m_library.notes.value(n);
        }
        std::sort(f.tags.begin(), f.tags.end());
    }
}

// ── Tags, stars, notes ──────────────────────────────────────────────────────

void Backend::addTag(const QString &familyName, const QString &tag) {
    const QString t = Library::normaliseTag(tag);
    if (t.isEmpty() || familyName.isEmpty()) return;
    QStringList &list = m_library.tags[familyName];
    if (list.contains(t)) return;
    list.append(t);
    std::sort(list.begin(), list.end());
    for (FontFamily &f : m_families) if (f.name == familyName) { f.tags = list; }
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    rebuildRows();
}

void Backend::removeTag(const QString &familyName, const QString &tag) {
    const QString t = Library::normaliseTag(tag);
    if (!m_library.tags.contains(familyName)) return;
    QStringList &list = m_library.tags[familyName];
    if (list.removeAll(t) == 0) return;
    if (list.isEmpty()) m_library.tags.remove(familyName);
    for (FontFamily &f : m_families) if (f.name == familyName) { f.tags.removeAll(t); }
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    rebuildRows();
}

void Backend::renameTag(const QString &from, const QString &to) {
    const QString a = Library::normaliseTag(from), b = Library::normaliseTag(to);
    if (a.isEmpty() || b.isEmpty() || a == b) return;
    for (auto it = m_library.tags.begin(); it != m_library.tags.end(); ++it) {
        if (it.value().removeAll(a) > 0 && !it.value().contains(b)) it.value().append(b);
        std::sort(it.value().begin(), it.value().end());
    }
    for (FontFamily &f : m_families) {
        if (f.tags.removeAll(a) > 0 && !f.tags.contains(b)) f.tags.append(b);
        std::sort(f.tags.begin(), f.tags.end());
    }
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    if (m_source == QStringLiteral("tag:") + a) setSource(QStringLiteral("tag:") + b); else rebuildRows();
}

void Backend::deleteTag(const QString &tag) {
    const QString t = Library::normaliseTag(tag);
    for (auto it = m_library.tags.begin(); it != m_library.tags.end();) {
        it.value().removeAll(t);
        if (it.value().isEmpty()) it = m_library.tags.erase(it); else ++it;
    }
    for (FontFamily &f : m_families) f.tags.removeAll(t);
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    if (m_source == QStringLiteral("tag:") + t) setSource(QStringLiteral("all")); else rebuildRows();
}

void Backend::toggleStar(const QString &familyName) {
    if (familyName.isEmpty()) return;
    const bool on = !m_library.starred.contains(familyName);
    if (on) m_library.starred.append(familyName); else m_library.starred.removeAll(familyName);
    for (FontFamily &f : m_families) if (f.name == familyName) f.starred = on;
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    rebuildRows();
}

void Backend::setNote(const QString &familyName, const QString &note) {
    if (familyName.isEmpty()) return;
    if (m_library.notes.value(familyName) == note) return;
    if (note.trimmed().isEmpty()) m_library.notes.remove(familyName); else m_library.notes.insert(familyName, note);
    for (FontFamily &f : m_families) if (f.name == familyName) f.note = note;
    saveLibrary();
    emit selectionChanged();
}

// ── Collections ─────────────────────────────────────────────────────────────

QVariantList Backend::collections() const {
    QVariantList out;
    for (const Collection &c : m_library.collections) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), c.id);
        m.insert(QStringLiteral("name"), c.name);
        int present = 0;
        for (const QString &n : c.families) if (family(n)) ++present;
        m.insert(QStringLiteral("count"), present);
        out.append(m);
    }
    return out;
}

QVariantList Backend::smartCollections() const {
    QVariantList out;
    for (const SmartCollection &s : m_library.smart) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("name"), s.name);
        int count = 0;
        for (const FontFamily &f : m_families) if (Library::matches(s, f, representativeInfo(f))) ++count;
        m.insert(QStringLiteral("count"), count);
        out.append(m);
    }
    return out;
}

QVariantMap Backend::smartCollection(const QString &id) const {
    for (const SmartCollection &s : m_library.smart) {
        if (s.id != id) continue;
        QVariantMap m;
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("matchAll"), s.matchAll);
        QVariantList rules;
        for (const SmartRule &r : s.rules) {
            QVariantMap rm;
            rm.insert(QStringLiteral("field"), r.field);
            rm.insert(QStringLiteral("op"), r.op);
            rm.insert(QStringLiteral("value"), r.value);
            rules.append(rm);
        }
        m.insert(QStringLiteral("rules"), rules);
        return m;
    }
    return {};
}

bool Backend::inCollection(const QString &id, const QString &familyName) const {
    for (const Collection &c : m_library.collections) if (c.id == id) return c.families.contains(familyName);
    return false;
}

void Backend::addCollection(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    Collection c;
    c.id = Library::newId();
    c.name = n;
    m_library.collections.append(c);
    saveLibrary();
    emit libraryChanged();
    setSource(QStringLiteral("coll:") + c.id);
}

void Backend::renameCollection(const QString &id, const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return;
    if (Collection *c = m_library.collection(id)) { c->name = n; saveLibrary(); emit libraryChanged(); emit sourceChanged(); return; }
    if (SmartCollection *s = m_library.smartCollection(id)) { s->name = n; saveLibrary(); emit libraryChanged(); emit sourceChanged(); }
}

void Backend::deleteCollection(const QString &id) {
    for (int i = 0; i < m_library.collections.size(); ++i) {
        if (m_library.collections.at(i).id != id) continue;
        m_library.collections.removeAt(i);
        saveLibrary();
        emit libraryChanged();
        if (m_source == QStringLiteral("coll:") + id) setSource(QStringLiteral("all"));
        return;
    }
}

void Backend::addToCollection(const QString &id, const QString &familyName) {
    Collection *c = m_library.collection(id);
    if (!c || familyName.isEmpty() || c->families.contains(familyName)) return;
    c->families.append(familyName);
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    if (m_source == QStringLiteral("coll:") + id) rebuildRows();
}

void Backend::removeFromCollection(const QString &id, const QString &familyName) {
    Collection *c = m_library.collection(id);
    if (!c || c->families.removeAll(familyName) == 0) return;
    saveLibrary();
    emit libraryChanged();
    emit selectionChanged();
    if (m_source == QStringLiteral("coll:") + id) rebuildRows();
}

void Backend::saveSmartCollection(const QString &id, const QString &name, bool matchAll, const QVariantList &rules) {
    SmartCollection fresh;
    fresh.name = name.trimmed().isEmpty() ? tr("Smart collection") : name.trimmed();
    fresh.matchAll = matchAll;
    for (const QVariant &v : rules) {
        const QVariantMap m = v.toMap();
        SmartRule r;
        r.field = m.value(QStringLiteral("field")).toString();
        r.op = m.value(QStringLiteral("op")).toString();
        r.value = m.value(QStringLiteral("value")).toString();
        if (!Library::ruleFields().contains(r.field) || !Library::ruleOps().contains(r.op)) continue;
        fresh.rules.append(r);
    }
    if (SmartCollection *existing = m_library.smartCollection(id)) {
        fresh.id = id;
        *existing = fresh;
    } else {
        fresh.id = Library::newId();
        m_library.smart.append(fresh);
    }
    saveLibrary();
    emit libraryChanged();
    const QString source = QStringLiteral("smart:") + fresh.id;
    if (m_source == source) { rebuildRows(); emit sourceChanged(); }
    else setSource(source);
}

void Backend::deleteSmartCollection(const QString &id) {
    for (int i = 0; i < m_library.smart.size(); ++i) {
        if (m_library.smart.at(i).id != id) continue;
        m_library.smart.removeAt(i);
        saveLibrary();
        emit libraryChanged();
        if (m_source == QStringLiteral("smart:") + id) setSource(QStringLiteral("all"));
        return;
    }
}

// ── Activate / deactivate ───────────────────────────────────────────────────

// Deactivating hides faces from every application without touching the files.
// System fonts included: fontconfig honours the user's rejectfont over the
// package's directory. Applications already running keep what they loaded.
QString Backend::setDisabled(const QStringList &files, bool disabled) {
    // Nothing changes in memory unless the fontconfig file says the same.
    const QList<DisabledFace> before = m_library.disabled;
    bool changed = false;
    if (disabled) {
        for (const QString &file : files) {
            if (m_library.isDisabled(file)) continue;
            // Remember enough to keep listing it once fontconfig stops.
            bool found = false;
            for (const FontFamily &f : m_families) {
                for (const FontFace &face : f.faces) {
                    if (face.file != file) continue;
                    DisabledFace d;
                    d.file = file; d.family = face.family; d.style = face.style; d.mono = face.mono;
                    m_library.disabled.append(d);
                    found = true;
                    changed = true;
                }
            }
            if (!found) {
                const FontFileInfo i = FontFile::read(file);
                if (!i.ok) { m_library.disabled = before; return tr("%1 is not a font that can be deactivated").arg(QFileInfo(file).fileName()); }
                DisabledFace d;
                d.file = file; d.family = i.family; d.style = i.subfamily; d.mono = i.fixedPitch;
                m_library.disabled.append(d);
                changed = true;
            }
        }
    } else {
        for (int i = m_library.disabled.size() - 1; i >= 0; --i)
            if (files.contains(m_library.disabled.at(i).file)) { m_library.disabled.removeAt(i); changed = true; }
    }
    if (!changed) return {};
    const QString error = writeFontconfig();
    if (!error.isEmpty()) { m_library.disabled = before; return error; }
    saveLibrary();
    emit libraryChanged();
    return {};
}

void Backend::deactivateSelected() {
    const FontFamily *f = selectedFamily();
    if (!f || m_busy) return;
    const QString name = f->name;
    const QString error = setDisabled(f->files, true);
    if (!error.isEmpty()) { setStatus(tr("Could not deactivate: %1").arg(error), true); return; }
    m_pendingSelect = name;
    setStatus(tr("Deactivated %1. Apps already running keep it until they restart.").arg(name));
    refresh();
}

void Backend::activateSelected() {
    const FontFamily *f = selectedFamily();
    if (!f || m_busy) return;
    const QString name = f->name;
    const QString error = setDisabled(f->files, false);
    if (!error.isEmpty()) { setStatus(tr("Could not activate: %1").arg(error), true); return; }
    m_pendingSelect = name;
    setStatus(tr("Activated %1").arg(name));
    refresh();
}

void Backend::deactivateFile(const QString &file) {
    const QString error = setDisabled({file}, true);
    if (!error.isEmpty()) { setStatus(tr("Could not deactivate: %1").arg(error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("Deactivated %1").arg(QFileInfo(file).fileName()));
    refresh();
}

void Backend::activateFile(const QString &file) {
    const QString error = setDisabled({file}, false);
    if (!error.isEmpty()) { setStatus(tr("Could not activate: %1").arg(error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("Activated %1").arg(QFileInfo(file).fileName()));
    refresh();
}


// ── Collections that do something ───────────────────────────────────────────

static QStringList collectionFiles(const Backend &b, const Library &lib, const QString &id, QString *name) {
    QStringList files;
    for (const Collection &c : lib.collections) {
        if (c.id != id) continue;
        if (name) *name = c.name;
        for (const QString &fam : c.families) if (const FontFamily *f = b.family(fam)) files += f->files;
    }
    return files;
}

void Backend::activateCollection(const QString &id) {
    QString name;
    const QStringList files = collectionFiles(*this, m_library, id, &name);
    if (files.isEmpty()) return;
    const QString error = setDisabled(files, false);
    if (!error.isEmpty()) { setStatus(tr("Could not activate %1: %2").arg(name, error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("Activated everything in %1").arg(name));
    refresh();
}

void Backend::deactivateCollection(const QString &id) {
    QString name;
    const QStringList files = collectionFiles(*this, m_library, id, &name);
    if (files.isEmpty()) return;
    const QString error = setDisabled(files, true);
    if (!error.isEmpty()) { setStatus(tr("Could not deactivate %1: %2").arg(name, error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("Deactivated everything in %1. Apps already running keep it until they restart.").arg(name));
    refresh();
}

void Backend::exportCollection(const QString &id) {
    if (m_picking) return;
    QString name;
    if (collectionFiles(*this, m_library, id, &name).isEmpty()) { setStatus(tr("Nothing in that collection is installed"), true); return; }
    m_picking = true;
    m_pendingPdfKind.clear();
    m_pendingExportCollection = id;
    m_picker->pickFolder(tr("Choose a folder to export %1 into").arg(name));
}

// ── Tidy ────────────────────────────────────────────────────────────────────

namespace {
// The static files of a family that also has a variable font: the files none
// of whose faces are variable. fontconfig lists a variable font's named
// instances (Regular, Bold…) as faces of their own that are not variable, so
// judging face by face would take the variable file for a static one.
QStringList staticFilesBesideVariable(const FontFamily &f) {
    QSet<QString> variableFiles;
    for (const FontFace &face : f.faces) if (face.variable) variableFiles.insert(face.file);
    QStringList files;
    if (variableFiles.isEmpty()) return files;
    for (const FontFace &face : f.faces)
        if (!face.disabled && face.duplicate && !variableFiles.contains(face.file) && !files.contains(face.file))
            files.append(face.file);
    return files;
}
} // namespace

// Everything worth cleaning up, with one-click fixes. Built on demand: it
// walks every family and reads the metadata cache.
void Backend::buildTidy() {
    QVariantList out;
    auto item = [](const QString &kind, const QString &family, const QString &title, const QString &detail,
                   const QStringList &files, const QString &severity) {
        QVariantMap m;
        m.insert(QStringLiteral("kind"), kind);
        m.insert(QStringLiteral("family"), family);
        m.insert(QStringLiteral("title"), title);
        m.insert(QStringLiteral("detail"), detail);
        m.insert(QStringLiteral("files"), files);
        m.insert(QStringLiteral("severity"), severity);
        return m;
    };
    qint64 coverageBytes = 0;
    int coverageCount = 0;
    for (const FontFamily &f : m_families) {
        // OTF and TTF twins of the same style.
        QHash<QString, QSet<QString>> extsByStyle;
        for (const FontFace &face : f.faces) if (!face.disabled) extsByStyle[face.style.toLower()].insert(extensionOf(face.file));
        int twins = 0;
        for (auto it = extsByStyle.cbegin(); it != extsByStyle.cend(); ++it) if (it.value().contains(QStringLiteral("otf")) && it.value().contains(QStringLiteral("ttf"))) ++twins;
        if (twins > 0) {
            QStringList files;
            for (const FontFace &face : f.faces) if (face.duplicate) files.append(face.file);
            out.append(item(QStringLiteral("twins"), f.name, tr("%1: the same styles as both OTF and TTF").arg(f.name),
                            tr("%1 styles are installed twice. Keep one format; applications pick one at random otherwise.").arg(twins),
                            files, QStringLiteral("warning")));
        }
        // Static weights beside a variable font that already carries them.
        const QStringList staticFiles = staticFilesBesideVariable(f);
        if (!staticFiles.isEmpty() && twins == 0)
            out.append(item(QStringLiteral("variable"), f.name, tr("%1: static weights beside a variable font").arg(f.name),
                            tr("The variable font already carries these weights. Deactivating the static copies leaves one of each."),
                            staticFiles, QStringLiteral("info")));
        // Files that do not pass validation.
        QStringList broken;
        QStringList why;
        for (const QString &file : f.files) {
            const FontFileInfo *i = info(file);
            if (!i) continue;
            if (!i->ok) { broken.append(file); why.append(i->error); }
            else if (!i->problems.isEmpty()) { broken.append(file); why.append(i->problems.first()); }
        }
        if (!broken.isEmpty())
            out.append(item(QStringLiteral("invalid"), f.name, (broken.size() == 1 ? tr("%1: 1 file with problems").arg(f.name) : tr("%1: %2 files with problems").arg(f.name).arg(broken.size())),
                            why.first(), broken, QStringLiteral("error")));
        // No Latin and not a language-coverage or symbol font: probably stray.
        if (!f.latin && !isCoverage(f.name) && f.user) {
            const FontFileInfo *i = representativeInfo(f);
            if (!i || i->classification() != QLatin1String("Symbolic"))
                out.append(item(QStringLiteral("nolatin"), f.name, tr("%1 sets no Latin text").arg(f.name),
                                tr("It covers %1. If that was not the point of installing it, deactivate or remove it.")
                                    .arg(f.scripts.isEmpty() ? tr("no known writing system") : f.scripts.join(QStringLiteral(", "))),
                                f.files, QStringLiteral("info")));
        }
        if (isCoverage(f.name) && !f.user) {
            ++coverageCount;
            for (const QString &file : f.files) coverageBytes += QFileInfo(file).size();
        }
    }
    if (coverageCount > 0)
        out.append(item(QStringLiteral("coverage"), QString(), tr("%1 language-coverage families, %2").arg(coverageCount).arg(formatBytes(coverageBytes)),
                        tr("The per-script Noto families from your packages. They are what lets the browser show every language, so they stay; "
                           "OmaFont folds them out of the way. Most of the size is the noto-fonts-extra package, which nothing requires."),
                        {}, QStringLiteral("info")));
    std::stable_sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        auto rank = [](const QString &s) { return s == QLatin1String("error") ? 0 : s == QLatin1String("warning") ? 1 : 2; };
        return rank(a.toMap().value(QStringLiteral("severity")).toString()) < rank(b.toMap().value(QStringLiteral("severity")).toString());
    });
    m_tidy = out;
    emit tidyChanged();
}

// Keep one of the OTF/TTF twins: the other format's copies are removed when
// they are yours, deactivated when they belong to a package.
void Backend::tidyKeepFormat(const QString &familyName, const QString &ext) {
    const FontFamily *f = family(familyName);
    if (!f || m_busy) return;
    QStringList removeList, disableList;
    for (const FontFace &face : f->faces) {
        if (!face.duplicate || face.disabled) continue;
        const QString e = extensionOf(face.file);
        const bool keep = (ext == QLatin1String("otf")) ? e == QLatin1String("otf") : e != QLatin1String("otf");
        if (keep) continue;
        (face.user ? removeList : disableList).append(face.file);
    }
    QString error;
    if (!removeList.isEmpty()) error = removeFamilyFiles(removeList);
    if (error.isEmpty() && !disableList.isEmpty()) error = setDisabled(disableList, true);
    if (!error.isEmpty()) { setStatus(tr("Tidy failed: %1").arg(error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("%1: kept the %2 copies").arg(familyName, ext.toUpper()));
    if (!removeList.isEmpty()) { setBusy(true); runFontCache(QString()); } else refresh();
}

void Backend::tidyKeepVariable(const QString &familyName) {
    const FontFamily *f = family(familyName);
    if (!f || m_busy) return;
    const QStringList files = staticFilesBesideVariable(*f);
    if (files.isEmpty()) return;
    const QString error = setDisabled(files, true);
    if (!error.isEmpty()) { setStatus(tr("Tidy failed: %1").arg(error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(tr("%1: deactivated %2 static copies").arg(familyName).arg(files.size()));
    refresh();
}

void Backend::tidyDeactivateFiles(const QStringList &files) {
    const QString error = setDisabled(files, true);
    if (!error.isEmpty()) { setStatus(tr("Could not deactivate: %1").arg(error), true); return; }
    m_pendingSelect = m_selectedName;
    setStatus(files.size() == 1 ? tr("Deactivated 1 file") : tr("Deactivated %1 files").arg(files.size()));
    refresh();
}

void Backend::tidyRemoveFiles(const QStringList &files) {
    if (m_busy) return;
    QStringList mine;
    for (const QString &f : files) if (f.startsWith(m_fontRoot + QLatin1Char('/'))) mine.append(f);
    if (mine.isEmpty()) { setStatus(tr("Only fonts in your own folder can be removed; deactivate the others"), true); return; }
    const QString error = removeFamilyFiles(mine);
    if (!error.isEmpty()) { setStatus(tr("Remove failed: %1").arg(error), true); return; }
    bool wasDisabled = false;
    for (int i = m_library.disabled.size() - 1; i >= 0; --i)
        if (mine.contains(m_library.disabled.at(i).file)) { m_library.disabled.removeAt(i); wasDisabled = true; }
    const QString fcError = wasDisabled ? writeFontconfig() : QString();
    if (wasDisabled) saveLibrary();
    m_pendingSelect = m_selectedName;
    const QString done = mine.size() == 1 ? tr("Removed 1 file") : tr("Removed %1 files").arg(mine.size());
    if (fcError.isEmpty()) setStatus(done);
    else setStatus(tr("%1; the fontconfig file could not be updated: %2").arg(done, fcError), true);
    setBusy(true);
    runFontCache(QString());
}
