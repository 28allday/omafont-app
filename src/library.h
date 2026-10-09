#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct FontFamily;
struct FontFileInfo;

// One rule of a smart collection: <field> <op> <value>.
struct SmartRule {
    QString field;   // family, style, postscript, fullname, designer, manufacturer, kind, language, class, trait, location, weight
    QString op;      // contains, begins, ends, is, isnot
    QString value;
    QJsonObject toJson() const;
    static SmartRule fromJson(const QJsonObject &o);
};

struct SmartCollection {
    QString id;
    QString name;
    bool matchAll = true;    // all rules, or any
    QList<SmartRule> rules;
    QJsonObject toJson() const;
    static SmartCollection fromJson(const QJsonObject &o);
};

struct Collection {
    QString id;
    QString name;
    QStringList families;
    QJsonObject toJson() const;
    static Collection fromJson(const QJsonObject &o);
};

// A face that has been deactivated. fontconfig stops listing it, so what the
// list needs to keep showing it is remembered here.
struct DisabledFace {
    QString file;
    QString family;
    QString style;
    bool mono = false;
    QJsonObject toJson() const;
    static DisabledFace fromJson(const QJsonObject &o);
};

// Everything OmaFont remembers between runs that is not a font file:
// collections, smart collections, which faces are deactivated, and when each
// family was installed. One JSON file; pure data, no Qt GUI.
class Library {
public:
    QList<Collection> collections;
    QList<SmartCollection> smart;
    QList<DisabledFace> disabled;
    QHash<QString, QDateTime> added;   // family → when OmaFont installed it
    QHash<QString, QString> sources;   // family → what it was installed from
    QHash<QString, QStringList> tags;  // family → tags, lower-case, unique
    QStringList starred;               // families with a star
    QHash<QString, QString> notes;     // family → free text

    static QString defaultPath();      // $XDG_CONFIG_HOME/omafont/library.json
    static QString fontconfigPath();   // $XDG_CONFIG_HOME/fontconfig/conf.d/60-omafont-deactivated.conf

    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &o);

    // The fontconfig fragment that rejects every deactivated file.
    QString fontconfigXml() const;
    bool isDisabled(const QString &file) const;

    Collection *collection(const QString &id);
    SmartCollection *smartCollection(const QString &id);
    static QString newId();
    static QString normaliseTag(const QString &tag);
    QStringList allTags() const;       // every tag in use, sorted

    // Does a family satisfy a smart collection? `meta` is the family's
    // representative face info (designer, manufacturer, kind…), may be null.
    static bool matches(const SmartCollection &sc, const FontFamily &family, const FontFileInfo *meta);
    static bool ruleMatches(const SmartRule &rule, const FontFamily &family, const FontFileInfo *meta);
    static QStringList ruleFields();
    static QStringList ruleOps();
};
