#include "cli.h"
#include "backend.h"
#include "fontfile.h"

#include <QDir>
#include <QFileInfo>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#ifndef OMAFONT_VERSION
#define OMAFONT_VERSION "dev"
#endif

namespace {

const QStringList kVerbs = {QStringLiteral("list"), QStringLiteral("info"), QStringLiteral("install"),
                            QStringLiteral("remove"), QStringLiteral("validate"), QStringLiteral("activate"),
                            QStringLiteral("deactivate"), QStringLiteral("export"), QStringLiteral("help")};

QTextStream &out() { static QTextStream s(stdout); return s; }
QTextStream &err() { static QTextStream s(stderr); return s; }

void usage() {
    out() << "OmaFont — fonts for Omarchy.\n\n"
             "  omafont                         open the window\n"
             "  omafont <file|folder|archive>…  open the window with those staged\n"
             "  omafont --version               which version this is\n"
             "  omafont list [--all] [--json]   installed families (yours first)\n"
             "  omafont info <family> [--json]  styles and files of one family\n"
             "  omafont install <path>… [--json]  install fonts, folders or archives\n"
             "  omafont remove <family> [--json]  remove one of your families\n"
             "  omafont validate <file|family>… [--json]  check font files for problems\n"
             "  omafont deactivate <family> [--json]  hide a family from every app, keeping its files\n"
             "  omafont activate <family> [--json]    bring it back\n"
             "  omafont export <family> <folder> [--json]  copy a family's files somewhere\n\n"
             "--all includes the language-coverage families that the window folds away.\n";
    out().flush();
}

// Wait for one of the backend's finishing signals, with a ceiling so a
// wedged child cannot hang a script.
template <typename Signal>
bool waitFor(Backend &backend, Signal signal, int timeoutMs) {
    QEventLoop loop;
    bool done = false;
    QObject::connect(&backend, signal, &loop, [&] { done = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return done;
}

bool scan(Backend &backend) {
    backend.refresh();
    return waitFor(backend, &Backend::scanFinished, 60000);
}

QJsonObject familyJson(const FontFamily &f, const FontFileInfo *meta = nullptr) {
    QJsonObject o;
    o.insert(QStringLiteral("name"), f.name);
    o.insert(QStringLiteral("styles"), QJsonArray::fromStringList(f.styles));
    o.insert(QStringLiteral("files"), QJsonArray::fromStringList(f.files));
    o.insert(QStringLiteral("yours"), f.user);
    o.insert(QStringLiteral("monospace"), f.mono);
    o.insert(QStringLiteral("variable"), f.variable);
    o.insert(QStringLiteral("coverage"), Backend::isCoverage(f.name));
    o.insert(QStringLiteral("deactivated"), f.inactive());
    o.insert(QStringLiteral("duplicates"), f.duplicate);
    o.insert(QStringLiteral("formats"), QJsonArray::fromStringList(f.formats));
    o.insert(QStringLiteral("foundry"), f.foundry);
    o.insert(QStringLiteral("languages"), QJsonArray::fromStringList(f.langs));
    if (f.added.isValid()) o.insert(QStringLiteral("added"), f.added.toString(Qt::ISODate));
    if (meta && meta->ok) {
        QJsonObject m;
        m.insert(QStringLiteral("kind"), meta->kind);
        m.insert(QStringLiteral("version"), meta->version);
        m.insert(QStringLiteral("postscriptName"), meta->postscriptName);
        m.insert(QStringLiteral("glyphCount"), meta->glyphCount);
        m.insert(QStringLiteral("designer"), meta->designer);
        m.insert(QStringLiteral("manufacturer"), meta->manufacturer);
        m.insert(QStringLiteral("copyright"), meta->copyright);
        m.insert(QStringLiteral("license"), meta->license);
        m.insert(QStringLiteral("licenseUrl"), meta->licenseUrl);
        m.insert(QStringLiteral("embedding"), meta->embedding());
        m.insert(QStringLiteral("classification"), meta->classification());
        m.insert(QStringLiteral("traits"), QJsonArray::fromStringList(meta->traits()));
        o.insert(QStringLiteral("details"), m);
    }
    return o;
}

void emitJson(const QJsonValue &value) {
    out() << QJsonDocument(value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject())).toJson(QJsonDocument::Indented);
    out().flush();
}

int fail(const QString &message, bool json) {
    if (json) {
        QJsonObject o;
        o.insert(QStringLiteral("ok"), false);
        o.insert(QStringLiteral("error"), message);
        emitJson(o);
    } else {
        err() << message << '\n';
        err().flush();
    }
    return 1;
}

}  // namespace

bool Cli::isVerb(const QString &word) {
    return kVerbs.contains(word) || word == QLatin1String("--help") || word == QLatin1String("-h")
        || word == QLatin1String("--version");
}

int Cli::run(Backend &backend, const QStringList &words) {
    QStringList args = words;
    const QString verb = args.isEmpty() ? QStringLiteral("help") : args.takeFirst();
    const bool json = args.removeAll(QStringLiteral("--json")) > 0;
    const bool all = args.removeAll(QStringLiteral("--all")) > 0;

    if (verb == QLatin1String("help") || verb == QLatin1String("--help") || verb == QLatin1String("-h")) {
        usage();
        return 0;
    }
    if (verb == QLatin1String("--version")) {
        out() << "omafont " << OMAFONT_VERSION << '\n';
        out().flush();
        return 0;
    }

    if (verb == QLatin1String("list")) {
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        QList<FontFamily> mine, system;
        for (const FontFamily &f : backend.allFamilies()) {
            if (!all && Backend::isCoverage(f.name)) continue;
            (f.user ? mine : system).append(f);
        }
        if (json) {
            QJsonArray a;
            for (const FontFamily &f : mine + system) a.append(familyJson(f));
            emitJson(a);
            return 0;
        }
        auto print = [](const QString &heading, const QList<FontFamily> &list) {
            if (list.isEmpty()) return;
            out() << heading << " (" << list.size() << ")\n";
            for (const FontFamily &f : list)
                out() << "  " << (f.mono ? "M " : "  ") << f.name << "  — " << f.styles.size() << " style" << (f.styles.size() == 1 ? "" : "s") << '\n';
        };
        print(QStringLiteral("Yours"), mine);
        print(QStringLiteral("System"), system);
        out().flush();
        return 0;
    }

    if (verb == QLatin1String("info")) {
        if (args.isEmpty()) return fail(QStringLiteral("info needs a family name"), json);
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        const FontFamily *f = backend.family(args.join(QLatin1Char(' ')));
        if (!f) return fail(QStringLiteral("No family called \"%1\"").arg(args.join(QLatin1Char(' '))), json);
        const FontFileInfo meta = FontFile::read(f->representative());
        if (json) { emitJson(familyJson(*f, &meta)); return 0; }
        out() << f->name << '\n'
              << "  " << (f->user ? "yours, removable" : "system") << (f->mono ? ", monospace" : "") << (f->variable ? ", variable" : "")
              << (f->inactive() ? ", DEACTIVATED" : "") << (f->duplicate ? ", has duplicates" : "") << '\n'
              << "  styles: " << f->styles.join(QStringLiteral(", ")) << '\n';
        if (meta.ok) {
            out() << "  kind: " << meta.kind << "   version: " << meta.version << "   glyphs: " << meta.glyphCount << '\n';
            if (!meta.designer.isEmpty()) out() << "  designer: " << meta.designer << '\n';
            if (!meta.manufacturer.isEmpty()) out() << "  manufacturer: " << meta.manufacturer << '\n';
            if (!meta.copyright.isEmpty()) out() << "  copyright: " << meta.copyright << '\n';
            if (!meta.license.isEmpty()) out() << "  licence: " << meta.license.left(120) << (meta.license.size() > 120 ? "…" : "") << '\n';
            out() << "  embedding: " << meta.embedding() << "   class: " << meta.classification() << '\n';
        }
        out() << "  languages: " << f->langs.size() << '\n';
        for (const QString &file : f->files) out() << "  " << file << '\n';
        out().flush();
        return 0;
    }

    if (verb == QLatin1String("install")) {
        if (args.isEmpty()) return fail(QStringLiteral("install needs at least one file, folder or archive"), json);
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        backend.stage(args);
        if (!backend.staged() && backend.staging()) {
            if (!waitFor(backend, &Backend::stageFinished, 120000) && !backend.staged())
                return fail(backend.status().isEmpty() ? QStringLiteral("Nothing there to install") : backend.status(), json);
        }
        if (!backend.staged()) return fail(backend.status().isEmpty() ? QStringLiteral("Nothing there to install") : backend.status(), json);
        const QString group = backend.stagedGroup();
        const QStringList families = backend.stagedFamilies();
        backend.installStaged();
        if (backend.statusIsError()) return fail(backend.status(), json);
        // The files are in place either way; only the font cache may lag.
        const bool refreshed = waitFor(backend, &Backend::scanFinished, 120000);
        if (!refreshed) { err() << "warning: the font cache did not finish refreshing; run fc-cache -f\n"; err().flush(); }
        if (json) {
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("cacheRefreshed"), refreshed);
            o.insert(QStringLiteral("group"), group);
            o.insert(QStringLiteral("families"), QJsonArray::fromStringList(families));
            o.insert(QStringLiteral("directory"), backend.fontRoot() + QLatin1Char('/') + Backend::slug(group));
            emitJson(o);
        } else {
            out() << "Installed " << group << " → " << backend.fontRoot() << '/' << Backend::slug(group) << '\n';
            out().flush();
        }
        return 0;
    }

    if (verb == QLatin1String("remove")) {
        if (args.isEmpty()) return fail(QStringLiteral("remove needs a family name"), json);
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        const QString name = args.join(QLatin1Char(' '));
        const FontFamily *f = backend.family(name);
        if (!f) return fail(QStringLiteral("No family called \"%1\"").arg(name), json);
        if (!f->user) return fail(QStringLiteral("%1 belongs to a package; OmaFont only removes fonts under %2").arg(name, backend.fontRoot()), json);
        backend.setSelectedName(name);
        backend.removeSelected();
        if (backend.statusIsError()) return fail(backend.status(), json);
        const bool refreshed = waitFor(backend, &Backend::scanFinished, 120000);
        if (!refreshed) { err() << "warning: the font cache did not finish refreshing; run fc-cache -f\n"; err().flush(); }
        if (json) {
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("cacheRefreshed"), refreshed);
            o.insert(QStringLiteral("removed"), name);
            emitJson(o);
        } else {
            out() << "Removed " << name << '\n';
            out().flush();
        }
        return 0;
    }

    if (verb == QLatin1String("validate")) {
        if (args.isEmpty()) return fail(QStringLiteral("validate needs files or a family name"), json);
        QStringList files, missing;
        for (const QString &a : args) (QFileInfo(a).isFile() ? files : missing).append(a);
        // Once any argument is a file they are all paths, and a typo must not
        // pass as "nothing wrong with the rest".
        if (!files.isEmpty() && !missing.isEmpty())
            return fail(QStringLiteral("No such file: %1").arg(missing.join(QStringLiteral(", "))), json);
        if (files.isEmpty()) {
            if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
            const FontFamily *f = backend.family(args.join(QLatin1Char(' ')));
            if (!f) return fail(QStringLiteral("No such file or family: %1").arg(args.join(QLatin1Char(' '))), json);
            files = f->files;
        }
        const QVariantList report = backend.validate(files);
        int errors = 0;
        if (json) {
            QJsonArray a;
            for (const QVariant &v : report) {
                const QVariantMap m = v.toMap();
                QJsonObject o;
                o.insert(QStringLiteral("file"), m.value(QStringLiteral("file")).toString());
                o.insert(QStringLiteral("status"), m.value(QStringLiteral("status")).toString());
                o.insert(QStringLiteral("problems"), QJsonArray::fromStringList(m.value(QStringLiteral("problems")).toStringList()));
                o.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(m.value(QStringLiteral("warnings")).toStringList()));
                o.insert(QStringLiteral("kind"), m.value(QStringLiteral("kind")).toString());
                o.insert(QStringLiteral("glyphCount"), m.value(QStringLiteral("glyphCount")).toInt());
                a.append(o);
                if (o.value(QStringLiteral("status")).toString() == QLatin1String("error")) ++errors;
            }
            emitJson(a);
            return errors > 0 ? 1 : 0;
        }
        for (const QVariant &v : report) {
            const QVariantMap m = v.toMap();
            const QString status = m.value(QStringLiteral("status")).toString();
            if (status == QLatin1String("error")) ++errors;
            out() << (status == QLatin1String("error") ? "FAIL  " : status == QLatin1String("warning") ? "WARN  " : "ok    ")
                  << m.value(QStringLiteral("file")).toString() << '\n';
            for (const QString &p : m.value(QStringLiteral("problems")).toStringList()) out() << "        ✕ " << p << '\n';
            for (const QString &w : m.value(QStringLiteral("warnings")).toStringList()) out() << "        ! " << w << '\n';
        }
        out().flush();
        return errors > 0 ? 1 : 0;
    }

    if (verb == QLatin1String("activate") || verb == QLatin1String("deactivate")) {
        if (args.isEmpty()) return fail(QStringLiteral("%1 needs a family name").arg(verb), json);
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        const QString name = args.join(QLatin1Char(' '));
        const FontFamily *f = backend.family(name);
        if (!f) return fail(QStringLiteral("No family called \"%1\"").arg(name), json);
        const QString error = backend.setDisabled(f->files, verb == QLatin1String("deactivate"));
        if (!error.isEmpty()) return fail(error, json);
        if (json) {
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("family"), name);
            o.insert(QStringLiteral("deactivated"), verb == QLatin1String("deactivate"));
            emitJson(o);
        } else {
            out() << (verb == QLatin1String("deactivate") ? "Deactivated " : "Activated ") << name << '\n';
            out().flush();
        }
        return 0;
    }

    if (verb == QLatin1String("export")) {
        if (args.size() < 2) return fail(QStringLiteral("export needs a family name and a folder"), json);
        const QString folder = args.takeLast();
        if (!scan(backend)) return fail(QStringLiteral("fc-list did not answer"), json);
        const QString name = args.join(QLatin1Char(' '));
        const FontFamily *f = backend.family(name);
        if (!f) return fail(QStringLiteral("No family called \"%1\"").arg(name), json);
        QDir().mkpath(folder);
        int copied = 0;
        const QString error = backend.exportFiles(f->files, folder, &copied);
        if (!error.isEmpty()) return fail(error, json);
        if (json) {
            QJsonObject o;
            o.insert(QStringLiteral("ok"), true);
            o.insert(QStringLiteral("family"), name);
            o.insert(QStringLiteral("directory"), folder);
            o.insert(QStringLiteral("copied"), copied);
            emitJson(o);
        } else {
            out() << "Exported " << copied << " files of " << name << " to " << folder << '\n';
            out().flush();
        }
        return 0;
    }

    usage();
    return 2;
}
