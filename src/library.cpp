#include "library.h"
#include "backend.h"
#include "fontfile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

// ── JSON shapes ─────────────────────────────────────────────────────────────

QJsonObject SmartRule::toJson() const {
    return {{QStringLiteral("field"), field}, {QStringLiteral("op"), op}, {QStringLiteral("value"), value}};
}
SmartRule SmartRule::fromJson(const QJsonObject &o) {
    SmartRule r;
    r.field = o.value(QStringLiteral("field")).toString();
    r.op = o.value(QStringLiteral("op")).toString();
    r.value = o.value(QStringLiteral("value")).toString();
    return r;
}

QJsonObject SmartCollection::toJson() const {
    QJsonArray a;
    for (const SmartRule &r : rules) a.append(r.toJson());
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("matchAll"), matchAll}, {QStringLiteral("rules"), a}};
}
SmartCollection SmartCollection::fromJson(const QJsonObject &o) {
    SmartCollection s;
    s.id = o.value(QStringLiteral("id")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.matchAll = o.value(QStringLiteral("matchAll")).toBool(true);
    for (const QJsonValue &v : o.value(QStringLiteral("rules")).toArray()) s.rules.append(SmartRule::fromJson(v.toObject()));
    return s;
}

QJsonObject Collection::toJson() const {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("families"), QJsonArray::fromStringList(families)}};
}
Collection Collection::fromJson(const QJsonObject &o) {
    Collection c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("families")).toArray()) c.families.append(v.toString());
    return c;
}

QJsonObject DisabledFace::toJson() const {
    return {{QStringLiteral("file"), file}, {QStringLiteral("family"), family}, {QStringLiteral("style"), style}, {QStringLiteral("mono"), mono}};
}
DisabledFace DisabledFace::fromJson(const QJsonObject &o) {
    DisabledFace d;
    d.file = o.value(QStringLiteral("file")).toString();
    d.family = o.value(QStringLiteral("family")).toString();
    d.style = o.value(QStringLiteral("style")).toString();
    d.mono = o.value(QStringLiteral("mono")).toBool();
    return d;
}

// ── Paths ───────────────────────────────────────────────────────────────────

static QString configHome() {
    QString c = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (c.isEmpty() || !c.startsWith(QLatin1Char('/'))) c = QDir::homePath() + QStringLiteral("/.config");
    return c;
}

QString Library::defaultPath() { return configHome() + QStringLiteral("/omafont/library.json"); }
QString Library::fontconfigPath() { return configHome() + QStringLiteral("/fontconfig/conf.d/60-omafont-deactivated.conf"); }

// ── Load / save ─────────────────────────────────────────────────────────────

bool Library::load(const QString &path, QString *error) {
    QFile f(path);
    if (!f.exists()) return true;  // a fresh library is an empty one
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) { if (error) *error = pe.errorString(); return false; }
    fromJson(doc.object());
    return true;
}

bool Library::save(const QString &path, QString *error) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

QJsonObject Library::toJson() const {
    QJsonArray c, s, d;
    for (const Collection &x : collections) c.append(x.toJson());
    for (const SmartCollection &x : smart) s.append(x.toJson());
    for (const DisabledFace &x : disabled) d.append(x.toJson());
    QJsonObject a, src, tg, nt;
    for (auto it = added.cbegin(); it != added.cend(); ++it) a.insert(it.key(), it.value().toString(Qt::ISODate));
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) src.insert(it.key(), it.value());
    for (auto it = tags.cbegin(); it != tags.cend(); ++it) if (!it.value().isEmpty()) tg.insert(it.key(), QJsonArray::fromStringList(it.value()));
    for (auto it = notes.cbegin(); it != notes.cend(); ++it) if (!it.value().trimmed().isEmpty()) nt.insert(it.key(), it.value());
    return {{QStringLiteral("version"), 2}, {QStringLiteral("collections"), c}, {QStringLiteral("smart"), s},
            {QStringLiteral("disabled"), d}, {QStringLiteral("added"), a}, {QStringLiteral("sources"), src},
            {QStringLiteral("tags"), tg}, {QStringLiteral("starred"), QJsonArray::fromStringList(starred)},
            {QStringLiteral("notes"), nt}};
}

void Library::fromJson(const QJsonObject &o) {
    collections.clear(); smart.clear(); disabled.clear(); added.clear();
    sources.clear(); tags.clear(); starred.clear(); notes.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("collections")).toArray()) collections.append(Collection::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("smart")).toArray()) smart.append(SmartCollection::fromJson(v.toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("disabled")).toArray()) disabled.append(DisabledFace::fromJson(v.toObject()));
    const QJsonObject a = o.value(QStringLiteral("added")).toObject();
    for (auto it = a.constBegin(); it != a.constEnd(); ++it) {
        const QDateTime when = QDateTime::fromString(it.value().toString(), Qt::ISODate);
        if (when.isValid()) added.insert(it.key(), when);
    }
    const QJsonObject src = o.value(QStringLiteral("sources")).toObject();
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) sources.insert(it.key(), it.value().toString());
    const QJsonObject tg = o.value(QStringLiteral("tags")).toObject();
    for (auto it = tg.constBegin(); it != tg.constEnd(); ++it) {
        QStringList list;
        for (const QJsonValue &v : it.value().toArray()) {
            const QString n = normaliseTag(v.toString());
            if (!n.isEmpty() && !list.contains(n)) list.append(n);
        }
        if (!list.isEmpty()) tags.insert(it.key(), list);
    }
    for (const QJsonValue &v : o.value(QStringLiteral("starred")).toArray()) if (!v.toString().isEmpty()) starred.append(v.toString());
    const QJsonObject nt = o.value(QStringLiteral("notes")).toObject();
    for (auto it = nt.constBegin(); it != nt.constEnd(); ++it) notes.insert(it.key(), it.value().toString());
}

// Tags are adjectives: lower-case, single spaces, no leading '#'.
QString Library::normaliseTag(const QString &tag) {
    QString t = tag.trimmed().toLower();
    while (t.startsWith(QLatin1Char('#'))) t.remove(0, 1);
    t = t.simplified();
    return t.left(40);
}

QStringList Library::allTags() const {
    QStringList out;
    for (auto it = tags.cbegin(); it != tags.cend(); ++it)
        for (const QString &tag : it.value()) if (!out.contains(tag)) out.append(tag);
    std::sort(out.begin(), out.end());
    return out;
}

// ── Deactivation ────────────────────────────────────────────────────────────

// fontconfig's <rejectfont> takes globs, and its glob knows only * and ?:
// there is no escape, and [ ] \ are ordinary characters. A path is written
// as it is ("Inter[wght].ttf" must stay that), XML-escaped. A * or ? in a file
// name can only widen the match to near-identical names in the same folder.
static QString globEscape(const QString &path) { return path.toHtmlEscaped(); }

QString Library::fontconfigXml() const {
    QString xml = QStringLiteral("<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
                                 "<!-- Written by OmaFont. Faces deactivated in the Fonts app; edit there, not here. -->\n"
                                 "<fontconfig>\n");
    if (!disabled.isEmpty()) {
        xml += QStringLiteral("  <selectfont>\n    <rejectfont>\n");
        for (const DisabledFace &d : disabled) xml += QStringLiteral("      <glob>%1</glob>\n").arg(globEscape(d.file));
        xml += QStringLiteral("    </rejectfont>\n  </selectfont>\n");
    }
    xml += QStringLiteral("</fontconfig>\n");
    return xml;
}

bool Library::isDisabled(const QString &file) const {
    for (const DisabledFace &d : disabled) if (d.file == file) return true;
    return false;
}

Collection *Library::collection(const QString &id) {
    for (Collection &c : collections) if (c.id == id) return &c;
    return nullptr;
}
SmartCollection *Library::smartCollection(const QString &id) {
    for (SmartCollection &s : smart) if (s.id == id) return &s;
    return nullptr;
}
QString Library::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8); }

// ── Smart collections ───────────────────────────────────────────────────────

QStringList Library::ruleFields() {
    return {QStringLiteral("family"), QStringLiteral("style"), QStringLiteral("postscript"), QStringLiteral("fullname"),
            QStringLiteral("designer"), QStringLiteral("manufacturer"), QStringLiteral("kind"), QStringLiteral("language"),
            QStringLiteral("class"), QStringLiteral("trait"), QStringLiteral("location"), QStringLiteral("weight"),
            QStringLiteral("tag"), QStringLiteral("script")};
}
QStringList Library::ruleOps() {
    return {QStringLiteral("contains"), QStringLiteral("begins"), QStringLiteral("ends"), QStringLiteral("is"), QStringLiteral("isnot")};
}

static bool textOp(const QString &op, const QStringList &haystack, const QString &value) {
    const QString v = value.trimmed();
    bool any = false;
    for (const QString &h : haystack) {
        bool hit = false;
        if (op == QLatin1String("contains")) hit = h.contains(v, Qt::CaseInsensitive);
        else if (op == QLatin1String("begins")) hit = h.startsWith(v, Qt::CaseInsensitive);
        else if (op == QLatin1String("ends")) hit = h.endsWith(v, Qt::CaseInsensitive);
        else if (op == QLatin1String("is") || op == QLatin1String("isnot")) hit = h.compare(v, Qt::CaseInsensitive) == 0;
        if (hit) { any = true; break; }
    }
    return op == QLatin1String("isnot") ? !any : any;
}

bool Library::ruleMatches(const SmartRule &rule, const FontFamily &f, const FontFileInfo *meta) {
    const QString field = rule.field;
    if (field == QLatin1String("family")) return textOp(rule.op, QStringList{f.name} + f.members, rule.value);
    if (field == QLatin1String("tag")) return textOp(rule.op, f.tags, rule.value);
    if (field == QLatin1String("script")) return textOp(rule.op, f.scripts, rule.value);
    if (field == QLatin1String("style")) return textOp(rule.op, f.styles, rule.value);
    if (field == QLatin1String("postscript")) {
        QStringList names;
        for (const FontFace &face : f.faces) names.append(face.psName);
        return textOp(rule.op, names, rule.value);
    }
    if (field == QLatin1String("fullname")) {
        QStringList names;
        for (const FontFace &face : f.faces) names.append(face.fullName);
        return textOp(rule.op, names, rule.value);
    }
    if (field == QLatin1String("designer")) return textOp(rule.op, {meta ? meta->designer : QString()}, rule.value);
    if (field == QLatin1String("manufacturer")) return textOp(rule.op, {meta ? meta->manufacturer : QString(), f.foundry}, rule.value);
    if (field == QLatin1String("kind")) return textOp(rule.op, f.formats, rule.value);
    if (field == QLatin1String("language")) {
        QStringList names;
        for (const QString &code : f.langs) { names.append(code); names.append(FontFile::languageName(code)); }
        return textOp(rule.op, names, rule.value);
    }
    if (field == QLatin1String("class")) return textOp(rule.op, {meta ? meta->classification() : QString()}, rule.value);
    if (field == QLatin1String("trait")) {
        QStringList traits = meta ? meta->traits() : QStringList();
        if (f.mono && !traits.contains(QStringLiteral("Monospaced"))) traits.append(QStringLiteral("Monospaced"));
        if (f.variable && !traits.contains(QStringLiteral("Variable"))) traits.append(QStringLiteral("Variable"));
        return textOp(rule.op, traits, rule.value);
    }
    if (field == QLatin1String("location")) return textOp(rule.op, {f.user ? QStringLiteral("Yours") : QStringLiteral("System")}, rule.value);
    if (field == QLatin1String("weight")) {
        QStringList names;
        for (const FontFace &face : f.faces) names.append(FontFile::weightName(face.weightClass()));
        return textOp(rule.op, names, rule.value);
    }
    return false;
}

bool Library::matches(const SmartCollection &sc, const FontFamily &f, const FontFileInfo *meta) {
    if (sc.rules.isEmpty()) return false;
    for (const SmartRule &r : sc.rules) {
        const bool hit = ruleMatches(r, f, meta);
        if (sc.matchAll && !hit) return false;
        if (!sc.matchAll && hit) return true;
    }
    return sc.matchAll;
}
