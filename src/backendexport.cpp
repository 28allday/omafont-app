#include "backend.h"
#include "filepicker.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPdfWriter>
#include <QRawFont>
#include <QStandardPaths>
#include <algorithm>

// Repertoire, validation reports, export, reveal, and the specimen PDF.

QVariantMap infoToMap(const FontFileInfo &i);
QProcessEnvironment safeEnvironment();

// ── Repertoire ──────────────────────────────────────────────────────────────

// Every character a file covers, from fontconfig's charset, capped so a CJK
// face does not become 30 000 delegates.
void Backend::loadRepertoire(const QString &file) {
    if (file.isEmpty()) {
        m_repertoire.clear(); m_repertoireTotal = 0; m_repertoireFile.clear();
        emit repertoireChanged();
        emit repertoireFinished();
        return;
    }
    if (file == m_repertoireFile && !m_repertoire.isEmpty()) { emit repertoireFinished(); return; }
    if (m_repertoireProcess) { m_repertoireProcess->disconnect(this); if (m_repertoireProcess->state() != QProcess::NotRunning) m_repertoireProcess->kill(); m_repertoireProcess->deleteLater(); }
    m_repertoireProcess = new QProcess(this);
    m_repertoireFile = file;
    connect(m_repertoireProcess, &QProcess::finished, this, [this, file] {
        const QList<quint32> all = parseCharset(readBounded(m_repertoireProcess, nullptr));
        m_repertoire.clear();
        m_repertoireTotal = all.size();
        for (int i = 0; i < all.size() && i < kRepertoireCap; ++i) m_repertoire.append(all.at(i));
        if (m_repertoireFile == file) emit repertoireChanged();
        emit repertoireFinished();
    });
    connect(m_repertoireProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_repertoire.clear(); m_repertoireTotal = 0;
        emit repertoireChanged();
        emit repertoireFinished();
    });
    startBounded(m_repertoireProcess, QStringLiteral("fc-scan"), {QStringLiteral("--format=%{charset}"), file}, 30000);
    emit repertoireChanged();
}

// ── Validation ──────────────────────────────────────────────────────────────

QVariantList Backend::validate(const QStringList &files) const {
    QVariantList out;
    for (const QString &file : files) {
        const FontFileInfo *cached = info(file);
        const FontFileInfo i = cached ? *cached : FontFile::read(file);
        QVariantMap m = infoToMap(i);
        m.insert(QStringLiteral("file"), file);
        m.insert(QStringLiteral("name"), QFileInfo(file).fileName());
        QStringList problems = i.ok ? i.problems : QStringList{i.error};
        // Can Qt's font engine actually load it? fontconfig listing a file and
        // FreeType rendering it are two different things.
        if (i.ok) {
            QRawFont raw(file, 12.0);
            if (!raw.isValid()) problems.append(QStringLiteral("The font engine could not load it"));
        }
        m.insert(QStringLiteral("problems"), problems);
        m.insert(QStringLiteral("status"), !problems.isEmpty() ? QStringLiteral("error") : !i.warnings.isEmpty() ? QStringLiteral("warning") : QStringLiteral("ok"));
        out.append(m);
    }
    return out;
}

void Backend::validateSelected() {
    const FontFamily *f = selectedFamily();
    if (!f) return;
    m_validationReport = validate(f->files);
    m_validationTitle = tr("Validation: %1").arg(f->name);
    emit validationChanged();
}

void Backend::validateStaged() {
    if (m_stagedFaces.isEmpty()) return;
    QStringList files;
    for (const FontFace &face : m_stagedFaces) if (!files.contains(face.file)) files.append(face.file);
    m_validationReport = validate(files);
    m_validationTitle = tr("Validation: %1").arg(stagedGroup());
    emit validationChanged();
}

void Backend::clearValidation() {
    m_validationReport.clear();
    m_validationTitle.clear();
    emit validationChanged();
}

// ── Export and reveal ───────────────────────────────────────────────────────

QString Backend::exportFiles(const QStringList &files, const QString &directory, int *copied) {
    const QFileInfo dirInfo(directory);
    if (!dirInfo.isDir()) return tr("%1 is not a folder").arg(directory);
    if (!dirInfo.isWritable()) return tr("%1 is not writable").arg(directory);
    int n = 0;
    for (const QString &file : files) {
        const QFileInfo src(file);
        if (!src.isFile()) continue;
        QString target = directory + QLatin1Char('/') + src.fileName();
        if (QFileInfo::exists(target)) target = directory + QLatin1Char('/') + keepBothName(directory, src.fileName());
        if (!QFile::copy(file, target)) return tr("Could not copy %1").arg(src.fileName());
        ++n;
    }
    if (copied) *copied = n;
    return n == 0 ? tr("Nothing to export") : QString();
}

void Backend::exportSelected() {
    if (!selectedFamily() || m_picking) return;
    m_picking = true;
    m_pendingPdfKind.clear();
    m_picker->pickFolder(tr("Choose a folder to export %1 into").arg(m_selectedName));
}

void Backend::revealSelected() {
    const FontFamily *f = selectedFamily();
    if (!f || f->files.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(f->files.first()).absolutePath()));
}

// ── Specimen PDF ────────────────────────────────────────────────────────────

void Backend::saveSpecimenPdf(const QString &kind) {
    if (m_picking) return;
    const QString k = (kind == QLatin1String("waterfall") || kind == QLatin1String("repertoire")) ? kind : QStringLiteral("specimen");
    if (!selectedFamily()) return;
    m_picking = true;
    m_pendingPdfKind = k;
    const QString base = m_selectedName + QLatin1Char(' ') + k;
    m_picker->saveFile(tr("Save the %1 as PDF").arg(k), slug(base) + QStringLiteral(".pdf"), tr("PDF"), {QStringLiteral("*.pdf")});
}

namespace {

struct LoadedFamily {
    QString qtFamily;
    QList<int> ids;
};

// The file's own face, registered with Qt for the length of the render.
LoadedFamily loadFamily(const QStringList &files) {
    LoadedFamily out;
    for (const QString &file : files) {
        const int id = QFontDatabase::addApplicationFont(file);
        if (id < 0) continue;
        out.ids.append(id);
        if (out.qtFamily.isEmpty()) {
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) out.qtFamily = fams.first();
        }
    }
    return out;
}

void unloadFamily(const LoadedFamily &loaded) {
    for (int id : loaded.ids) QFontDatabase::removeApplicationFont(id);
}

}  // namespace

// A4 pages at 300 dpi, the Oma specimen in ink on paper: name, sample at
// several sizes, the character set; or a waterfall; or a glyph grid.
QString Backend::writeSpecimenPdf(const QString &path, const QString &kind, const QString &familyName) {
    // The family is found and loaded before the writer exists: QPdfWriter
    // creates the file, and a refusal after that would leave a blank PDF.
    const FontFamily *fam = family(familyName);
    if (!fam) return tr("No family called %1").arg(familyName);
    const LoadedFamily loaded = loadFamily(fam->files);
    const struct Unload { const LoadedFamily &l; ~Unload() { unloadFamily(l); } } unload{loaded};
    if (loaded.qtFamily.isEmpty()) return tr("The font engine could not load %1").arg(familyName);

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);
    writer.setTitle(familyName + QStringLiteral(" — ") + kind);
    writer.setCreator(QStringLiteral("OmaFont"));
    QPainter p(&writer);
    if (!p.isActive()) return tr("Could not open %1 for writing").arg(path);
    const qreal dpi = writer.resolution();
    const QRectF page = writer.pageLayout().paintRectPixels(int(dpi));
    const qreal margin = dpi * 0.6;
    const QRectF body = page.adjusted(margin, margin, -margin, -margin);
    auto pt = [dpi](qreal points) { return points * dpi / 72.0; };
    const QString sample = m_sample.isEmpty() ? defaultSample() : m_sample;
    QFont ui(QStringLiteral("Inter"));
    if (!QFontDatabase::families().contains(QStringLiteral("Inter"))) ui = QFont(QStringLiteral("sans-serif"));

    auto footer = [&](int pageNo) {
        QFont f = ui; f.setPixelSize(int(pt(8)));
        p.setFont(f);
        p.setPen(QColor(0x72, 0x7E, 0x90));
        p.drawText(QRectF(body.left(), page.bottom() - margin * 0.7, body.width(), pt(12)), Qt::AlignRight | Qt::AlignVCenter,
                   tr("OmaFont · page %1").arg(pageNo));
        p.setPen(Qt::black);
    };

    QFont face(loaded.qtFamily);

    qreal y = body.top();
    int pageNo = 1;
    auto heading = [&] {
        face.setPixelSize(int(pt(40)));
        p.setFont(face);
        p.drawText(QRectF(body.left(), y, body.width(), pt(50)), Qt::AlignLeft | Qt::AlignVCenter, fam->name);
        y += pt(54);
        QFont small = ui; small.setPixelSize(int(pt(9)));
        p.setFont(small);
        p.setPen(QColor(0x72, 0x7E, 0x90));
        const FontFileInfo *i = representativeInfo(*fam);
        QStringList bits{tr("%1 styles").arg(fam->styles.size())};
        if (i && !i->designer.isEmpty()) bits.append(i->designer);
        if (i && !i->version.isEmpty()) bits.append(i->version);
        if (!fam->formats.isEmpty()) bits.append(fam->formats.join(QStringLiteral(", ")));
        p.drawText(QPointF(body.left(), y + pt(9)), bits.join(QStringLiteral("   ·   ")));
        p.setPen(Qt::black);
        y += pt(22);
        p.setPen(QPen(QColor(0xA7, 0xB1, 0xC1), pt(0.5)));
        p.drawLine(QPointF(body.left(), y), QPointF(body.right(), y));
        p.setPen(Qt::black);
        y += pt(14);
    };
    heading();

    if (kind == QLatin1String("repertoire")) {
        QList<quint32> glyphs;
        if (m_repertoireFile == fam->representative() && !m_repertoire.isEmpty()) {
            for (const QVariant &v : m_repertoire) glyphs.append(v.toUInt());
        } else {
            QProcess scan;
            scan.setProcessEnvironment(safeEnvironment());
            scan.start(QStringLiteral("fc-scan"), {QStringLiteral("--format=%{charset}"), fam->representative()}, QIODevice::ReadOnly);
            if (scan.waitForFinished(10000)) glyphs = parseCharset(readBounded(&scan, nullptr));
            else scan.kill();
        }
        const int columns = 12;
        const qreal cell = body.width() / columns;
        const qreal rowH = cell * 1.25;
        face.setPixelSize(int(cell * 0.5));
        QFont code = ui; code.setPixelSize(int(pt(6)));
        int col = 0;
        int shown = 0;
        for (quint32 cp : glyphs) {
            if (cp < 0x20 || (cp >= 0xD800 && cp <= 0xDFFF)) continue;
            if (shown >= 6000) break;
            if (y + rowH > body.bottom()) { footer(pageNo++); writer.newPage(); y = body.top(); col = 0; }
            const QRectF box(body.left() + col * cell, y, cell, rowH);
            p.setPen(QPen(QColor(0xDD, 0xE2, 0xEA), pt(0.4)));
            p.drawRect(box);
            p.setPen(Qt::black);
            p.setFont(face);
            p.drawText(box.adjusted(0, 0, 0, -rowH * 0.25), Qt::AlignCenter, QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1));
            p.setFont(code);
            p.setPen(QColor(0x72, 0x7E, 0x90));
            p.drawText(box.adjusted(0, rowH * 0.72, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("U+%1").arg(cp, 4, 16, QLatin1Char('0')).toUpper());
            p.setPen(Qt::black);
            if (++col == columns) { col = 0; y += rowH; }
            ++shown;
        }
        footer(pageNo);
        p.end();
        return {};
    }

    const QList<int> sizes = kind == QLatin1String("waterfall")
        ? QList<int>{72, 60, 48, 36, 30, 24, 20, 18, 16, 14, 12, 11, 10, 9, 8}
        : QList<int>{36, 24, 18, 14, 11};
    for (int size : sizes) {
        face.setPixelSize(int(pt(size)));
        p.setFont(face);
        QFontMetricsF fm(face);
        const QRectF needed = fm.boundingRect(QRectF(body.left() + pt(28), y, body.width() - pt(28), body.height()), Qt::TextWordWrap, sample);
        if (y + needed.height() > body.bottom()) { footer(pageNo++); writer.newPage(); y = body.top(); }
        QFont label = ui; label.setPixelSize(int(pt(6.5)));
        p.setFont(label);
        p.setPen(QColor(0x72, 0x7E, 0x90));
        p.drawText(QRectF(body.left(), y, pt(24), needed.height()), Qt::AlignRight | Qt::AlignTop, QString::number(size));
        p.setPen(Qt::black);
        p.setFont(face);
        p.drawText(QRectF(body.left() + pt(28), y, body.width() - pt(28), needed.height()), Qt::TextWordWrap, sample);
        y += needed.height() + pt(8);
    }
    if (kind != QLatin1String("waterfall")) {
        // Styles, each in its own face.
        y += pt(10);
        QFont cap = ui; cap.setPixelSize(int(pt(7))); cap.setBold(true); cap.setLetterSpacing(QFont::AbsoluteSpacing, pt(0.6));
        p.setFont(cap);
        p.setPen(QColor(0x72, 0x7E, 0x90));
        p.drawText(QPointF(body.left(), y + pt(7)), tr("STYLES").toUpper());
        p.setPen(Qt::black);
        y += pt(16);
        for (const FontFace &fc : fam->faces) {
            if (y + pt(20) > body.bottom()) { footer(pageNo++); writer.newPage(); y = body.top(); }
            const LoadedFamily one = loadFamily({fc.file});
            QFont styled(one.qtFamily.isEmpty() ? loaded.qtFamily : one.qtFamily);
            styled.setPixelSize(int(pt(14)));
            if (fc.weightClass() >= 600) styled.setBold(true);
            if (fc.slant >= 100) styled.setItalic(true);
            p.setFont(styled);
            p.drawText(QPointF(body.left(), y + pt(14)), fc.style.isEmpty() ? tr("Regular") : fc.style);
            y += pt(20);
            unloadFamily(one);
        }
        y += pt(10);
        p.setFont(cap);
        p.setPen(QColor(0x72, 0x7E, 0x90));
        p.drawText(QPointF(body.left(), y + pt(7)), tr("CHARACTER SET"));
        p.setPen(Qt::black);
        y += pt(16);
        const QString charset = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789\n!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\n£€¥©®™ àáâäåæçèéêëìíîïñòóôöøùúûüß");
        face.setPixelSize(int(pt(14)));
        p.setFont(face);
        QFontMetricsF fm(face);
        const QRectF needed = fm.boundingRect(QRectF(body.left(), y, body.width(), body.height()), Qt::TextWordWrap, charset);
        if (y + needed.height() > body.bottom()) { footer(pageNo++); writer.newPage(); y = body.top(); }
        p.drawText(QRectF(body.left(), y, body.width(), needed.height()), Qt::TextWordWrap, charset);
    }
    footer(pageNo);
    p.end();
    return {};
}
