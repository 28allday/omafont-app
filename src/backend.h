#pragma once

#include "fontfile.h"
#include "library.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

class PortalFileChooser;
class QThread;

// One face as fontconfig reports it: a file on disk carrying one style of one
// family. A family is the unit people pick; a face is the unit on disk.
struct FontFace {
    QString family;
    QString style;
    QString file;
    bool mono = false;
    QString psName;
    QString fullName;
    QString version;      // "4.001"
    QString format;       // "TrueType", "CFF"
    QString foundry;
    int weight = 80;      // fontconfig weight (80 regular, 200 bold)
    int slant = 0;        // 0 roman, 100 italic, 110 oblique
    int width = 100;      // fontconfig width (100 normal)
    bool variable = false;
    int index = 0;        // face in a collection
    QStringList langs;
    bool disabled = false;
    bool duplicate = false;
    bool user = false;
    // fontconfig weight → OS/2 usWeightClass, roughly.
    int weightClass() const;
};

// Every face of one family, merged from an fc-list pass.
struct FontFamily {
    QString name;
    QStringList styles;
    QStringList files;
    QList<FontFace> faces;
    bool user = false;     // some file lives under the user font root
    bool mono = false;     // fontconfig spacing 100: eligible for the terminal
    bool latin = false;    // sets English, so its own name can be drawn in it
    bool variable = false;
    bool duplicate = false;
    int disabledCount = 0; // faces deactivated; == faces.size() means inactive
    QStringList formats;
    QString foundry;
    QStringList langs;     // union of the faces' language codes
    QStringList scripts;   // writing systems covered, from the languages: Latin, Greek, Cyrillic…
    QDateTime added;       // when OmaFont installed it, if it did
    QString source;        // what it was installed from
    QStringList members;   // fontconfig families folded into this one (Acme Sans Black, Acme Sans Light…)
    QStringList tags;
    bool starred = false;
    QString note;
    bool inactive() const { return !faces.isEmpty() && disabledCount == faces.size(); }
    // The face the specimen renders from: the one nearest Regular — weight 80,
    // upright, normal width — rather than whichever file sorts first.
    QString representative() const;
    // The fontconfig family name of the representative face: what omarchy-font-set and fc-list understand.
    QString fcName() const;
};

// The rows the rail shows: the families that pass the source and the filter,
// user fonts first. A model so delegates survive a rescan, and sectioned.
class FamilyModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1, GroupRole, StylesRole, FilesRole, FacesRole, UserRole, MonoRole, LatinRole,
        PreviewFileRole, CoverageRole, InactiveRole, DuplicateRole, VariableRole, FormatRole, StyleCountRole,
        StarredRole, TagsRole, MembersRole,
    };
    explicit FamilyModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRows(const QList<FontFamily> &rows);
    const QList<FontFamily> &rows() const { return m_rows; }
    Q_INVOKABLE int indexOf(const QString &name) const;
    Q_INVOKABLE QString nameAt(int row) const;
private:
    QList<FontFamily> m_rows;
};

// The whole font manager, headless. Scans with fontconfig, reads what the
// files say about themselves, keeps the library (collections, deactivated
// faces, install dates), stages anything droppable, installs, removes, exports
// and renders. Every subprocess is bounded in bytes and time.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(FamilyModel *families READ families CONSTANT)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString searchScope READ searchScope WRITE setSearchScope NOTIFY filterChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY sourceChanged)
    Q_PROPERTY(bool showCoverage READ showCoverage WRITE setShowCoverage NOTIFY showCoverageChanged)
    Q_PROPERTY(bool groupWeights READ groupWeights WRITE setGroupWeights NOTIFY groupWeightsChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(qreal specimenScale READ specimenScale WRITE setSpecimenScale NOTIFY specimenScaleChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool readingMetadata READ readingMetadata NOTIFY metadataChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int userCount READ userCount NOTIFY countsChanged)
    Q_PROPERTY(int systemCount READ systemCount NOTIFY countsChanged)
    Q_PROPERTY(int coverageCount READ coverageCount NOTIFY countsChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(int monoCount READ monoCount NOTIFY countsChanged)
    Q_PROPERTY(int recentCount READ recentCount NOTIFY countsChanged)
    Q_PROPERTY(int inactiveCount READ inactiveCount NOTIFY countsChanged)
    Q_PROPERTY(int duplicateCount READ duplicateCount NOTIFY countsChanged)
    Q_PROPERTY(QVariantList languages READ languages NOTIFY countsChanged)
    Q_PROPERTY(QVariantList designFacets READ designFacets NOTIFY countsChanged)
    Q_PROPERTY(QVariantList scriptFacets READ scriptFacets NOTIFY countsChanged)
    Q_PROPERTY(QVariantList tagFacets READ tagFacets NOTIFY libraryChanged)
    Q_PROPERTY(int starredCount READ starredCount NOTIFY countsChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList smartCollections READ smartCollections NOTIFY libraryChanged)
    Q_PROPERTY(QString fontRoot READ fontRoot CONSTANT)

    Q_PROPERTY(QString selectedName READ selectedName WRITE setSelectedName NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedInfo READ selectedInfo NOTIFY selectionChanged)

    Q_PROPERTY(bool staging READ staging NOTIFY stageChanged)
    Q_PROPERTY(bool staged READ staged NOTIFY stageChanged)
    Q_PROPERTY(QVariantList stagedFaces READ stagedFaces NOTIFY stageChanged)
    Q_PROPERTY(QVariantList stagedSelected READ stagedSelected NOTIFY stageChanged)
    Q_PROPERTY(QStringList stagedFamilies READ stagedFamilies NOTIFY stageChanged)
    Q_PROPERTY(QString stagedGroup READ stagedGroup NOTIFY stageChanged)
    Q_PROPERTY(QString stagedSource READ stagedSource NOTIFY stageChanged)
    Q_PROPERTY(QString stagedFormat READ stagedFormat WRITE setStagedFormat NOTIFY stageChanged)
    Q_PROPERTY(int stagedOtfCount READ stagedOtfCount NOTIFY stageChanged)
    Q_PROPERTY(int stagedTtfCount READ stagedTtfCount NOTIFY stageChanged)
    Q_PROPERTY(bool stagedInstalled READ stagedInstalled NOTIFY stageChanged)
    Q_PROPERTY(bool stagedTruncated READ stagedTruncated NOTIFY stageChanged)
    Q_PROPERTY(int stagedProblems READ stagedProblems NOTIFY stageChanged)
    Q_PROPERTY(int stagedWarnings READ stagedWarnings NOTIFY stageChanged)
    Q_PROPERTY(int stagedExisting READ stagedExisting NOTIFY stageChanged)
    Q_PROPERTY(QString conflictPolicy READ conflictPolicy WRITE setConflictPolicy NOTIFY stageChanged)
    Q_PROPERTY(bool installInvalid READ installInvalid WRITE setInstallInvalid NOTIFY stageChanged)

    Q_PROPERTY(QString previewFile READ previewFile NOTIFY previewChanged)
    Q_PROPERTY(QString previewTitle READ previewTitle NOTIFY previewChanged)
    Q_PROPERTY(QString sample READ sample WRITE setSample NOTIFY sampleChanged)
    Q_PROPERTY(QString defaultSample READ defaultSample CONSTANT)
    Q_PROPERTY(bool canSetTerminalFont READ canSetTerminalFont CONSTANT)
    Q_PROPERTY(QVariantList repertoire READ repertoire NOTIFY repertoireChanged)
    Q_PROPERTY(int repertoireTotal READ repertoireTotal NOTIFY repertoireChanged)
    Q_PROPERTY(QString repertoireFile READ repertoireFile NOTIFY repertoireChanged)
    Q_PROPERTY(QStringList compareList READ compareList NOTIFY compareChanged)
    Q_PROPERTY(QVariantList compareRows READ compareRows NOTIFY compareChanged)
    Q_PROPERTY(QVariantList tidyReport READ tidyReport NOTIFY tidyChanged)
    Q_PROPERTY(QVariantList validationReport READ validationReport NOTIFY validationChanged)
    Q_PROPERTY(QString validationTitle READ validationTitle NOTIFY validationChanged)

    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY highContrastChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    FamilyModel *families() { return &m_model; }
    const QList<FontFamily> &allFamilies() const { return m_families; }
    const Library &library() const { return m_library; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString searchScope() const { return m_searchScope; }
    void setSearchScope(const QString &scope);
    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString sourceTitle() const;
    bool showCoverage() const { return m_showCoverage; }
    void setShowCoverage(bool show);
    bool groupWeights() const { return m_groupWeights; }
    void setGroupWeights(bool on);
    QString sortOrder() const { return m_sortOrder; }
    void setSortOrder(const QString &order);
    QString view() const { return m_view; }
    void setView(const QString &view);
    qreal specimenScale() const { return m_specimenScale; }
    void setSpecimenScale(qreal scale);
    bool scanning() const { return m_scanning; }
    bool busy() const { return m_busy; }
    bool readingMetadata() const { return m_readingMetadata; }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }
    int totalCount() const { return m_families.size(); }
    int userCount() const;
    int systemCount() const;
    int coverageCount() const;
    int visibleCount() const { return m_model.rowCount(); }
    int monoCount() const;
    int recentCount() const;
    int inactiveCount() const;
    int duplicateCount() const;
    QVariantList languages() const;
    QVariantList designFacets() const;
    QVariantList scriptFacets() const;
    QVariantList tagFacets() const;
    int starredCount() const;
    QVariantList collections() const;
    QVariantList smartCollections() const;
    QString fontRoot() const { return m_fontRoot; }

    QString selectedName() const { return m_selectedName; }
    void setSelectedName(const QString &name);
    QVariantMap selected() const;
    QVariantMap selectedInfo() const;
    bool hasSelection() const;
    const FontFamily *selectedFamily() const;
    const FontFamily *family(const QString &name) const;
    const FontFileInfo *info(const QString &file) const;

    bool staging() const;
    bool staged() const { return !m_stagedFaces.isEmpty(); }
    QVariantList stagedFaces() const;
    QVariantList stagedSelected() const;
    QStringList stagedFamilies() const;
    QString stagedGroup() const;
    QString stagedSource() const { return m_stagedSource; }
    QString stagedFormat() const { return m_stagedFormat; }
    void setStagedFormat(const QString &format);
    int stagedOtfCount() const;
    int stagedTtfCount() const;
    bool stagedInstalled() const;
    bool stagedTruncated() const { return m_stagedTruncated; }
    int stagedProblems() const;
    int stagedWarnings() const;
    int stagedExisting() const;
    QString conflictPolicy() const { return m_conflictPolicy; }
    void setConflictPolicy(const QString &policy);
    bool installInvalid() const { return m_installInvalid; }
    void setInstallInvalid(bool on);

    QString previewFile() const;
    QString previewTitle() const;
    QString sample() const { return m_sample; }
    void setSample(const QString &sample);
    QString defaultSample() const;
    bool canSetTerminalFont() const { return !m_fontSetTool.isEmpty(); }
    QVariantList repertoire() const { return m_repertoire; }
    int repertoireTotal() const { return m_repertoireTotal; }
    QString repertoireFile() const { return m_repertoireFile; }
    QStringList compareList() const { return m_compare; }
    QVariantList compareRows() const;
    QVariantList tidyReport() const { return m_tidy; }
    QVariantList validationReport() const { return m_validationReport; }
    QString validationTitle() const { return m_validationTitle; }

    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool on);
    bool highContrast() const { return m_highContrast; }
    void setHighContrast(bool on);

    // Pure helpers, exercised directly by the tests.
    // `truncated` in: the text was cut at the byte cap; out: also set when the
    // line cap cut it.
    static QList<FontFamily> parseFcList(const QString &text, const QString &userRoot, bool *truncated = nullptr);
    static QList<FontFace> parseFcScan(const QString &text);
    static FontFace parseFace(const QStringList &parts);
    static QString fcFormat();
    static QString slug(const QString &family);
    static bool isCoverage(const QString &family);
    static bool isDesktopFont(const QString &path);
    static bool isArchive(const QString &path);
    static QString commonGroup(const QStringList &families);
    static QString regularFace(const QStringList &files);
    static QString userFontRoot();
    static QString extensionOf(const QString &path);
    static QList<quint32> parseCharset(const QString &text);
    static void markDuplicates(QList<FontFamily> &families);
    static void groupSuperfamilies(QList<FontFamily> &families);
    static QStringList scriptsFor(const QStringList &langs);
    static bool isStyleWord(const QString &word);
    static QString keepBothName(const QString &dir, const QString &fileName);

    // The file operations, synchronous and reusable by the command line.
    // Each returns an empty string on success or a message to show.
    QString installFaces(const QString &group, const QList<FontFace> &faces, QString *installedDir = nullptr, int *written = nullptr, int *skipped = nullptr);
    QString removeFamilyFiles(const QStringList &files);
    QStringList removableFiles(const FontFamily &family) const;
    QString exportFiles(const QStringList &files, const QString &directory, int *copied = nullptr);
    QString writeSpecimenPdf(const QString &path, const QString &kind, const QString &familyName);
    QString setDisabled(const QStringList &files, bool disabled);
    QVariantList validate(const QStringList &files) const;

    // Small conveniences for QML, which has no clean way to do either.
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    // A path under the home folder, written the short way: "~/.local/share/fonts".
    Q_INVOKABLE static QString tildePath(const QString &path);
    Q_INVOKABLE QVariantMap fileInfo(const QString &path) const;
    Q_INVOKABLE QStringList ruleFields() const { return Library::ruleFields(); }
    Q_INVOKABLE QStringList ruleOps() const { return Library::ruleOps(); }
    Q_INVOKABLE QVariantMap smartCollection(const QString &id) const;
    Q_INVOKABLE bool inCollection(const QString &id, const QString &familyName) const;
    Q_INVOKABLE QStringList allTags() const { return m_library.allTags(); }
    Q_INVOKABLE QStringList tagsOf(const QString &familyName) const { return m_library.tags.value(familyName); }
    Q_INVOKABLE QString designClassOf(const QString &familyName) const;

public slots:
    void refresh();
    void stage(const QStringList &paths);
    void stageUrls(const QList<QUrl> &urls);
    void clearStage();
    void installStaged();
    void removeSelected();
    void setAsTerminalFont();
    void openPicker();
    void selectFirst();
    void setStatus(const QString &message, bool error = false);
    // Library
    void addCollection(const QString &name);
    void renameCollection(const QString &id, const QString &name);
    void deleteCollection(const QString &id);
    void addToCollection(const QString &id, const QString &familyName);
    void removeFromCollection(const QString &id, const QString &familyName);
    void saveSmartCollection(const QString &id, const QString &name, bool matchAll, const QVariantList &rules);
    void deleteSmartCollection(const QString &id);
    void deactivateSelected();
    void activateSelected();
    void deactivateFile(const QString &file);
    void activateFile(const QString &file);
    void addTag(const QString &familyName, const QString &tag);
    void removeTag(const QString &familyName, const QString &tag);
    void renameTag(const QString &from, const QString &to);
    void deleteTag(const QString &tag);
    void toggleStar(const QString &familyName);
    void setNote(const QString &familyName, const QString &note);
    void activateCollection(const QString &id);
    void deactivateCollection(const QString &id);
    void exportCollection(const QString &id);
    // Compare
    void toggleCompare(const QString &familyName);
    void clearCompare();
    // Tidy
    void buildTidy();
    void tidyKeepFormat(const QString &familyName, const QString &ext);
    void tidyKeepVariable(const QString &familyName);
    void tidyDeactivateFiles(const QStringList &files);
    void tidyRemoveFiles(const QStringList &files);
    // Views and reports
    void loadRepertoire(const QString &file);
    void validateSelected();
    void validateStaged();
    void clearValidation();
    void exportSelected();
    void revealSelected();
    void saveSpecimenPdf(const QString &kind);

signals:
    void filterChanged();
    void sourceChanged();
    void showCoverageChanged();
    void groupWeightsChanged();
    void sortOrderChanged();
    void viewChanged();
    void specimenScaleChanged();
    void scanningChanged();
    void busyChanged();
    void metadataChanged();
    void statusChanged();
    void countsChanged();
    void libraryChanged();
    void selectionChanged();
    void stageChanged();
    void previewChanged();
    void sampleChanged();
    void repertoireChanged();
    void validationChanged();
    void compareChanged();
    void tidyChanged();
    void reducedMotionChanged();
    void highContrastChanged();
    void installed(const QString &group);
    void removed(const QString &family);
    void exported(const QString &directory, int count);
    void scanFinished();
    void stageFinished();
    void metadataFinished();
    void repertoireFinished();
    void charsetSearchFinished();

private:
    // core
    void rebuildRows();
    bool passesSource(const FontFamily &f) const;
    bool passesFilter(const FontFamily &f) const;
    void setScanning(bool on);
    void setBusy(bool on);
    void startBounded(QProcess *process, const QString &program, const QStringList &args, int timeoutMs);
    QString readBounded(QProcess *process, bool *truncated) const;
    void onScanFinished();
    void onLatinFinished();
    void finishScanIfDone();
    void injectDisabled(QList<FontFamily> &families) const;
    void applyLibrary(QList<FontFamily> &families) const;
    void startMetadata();
    void stopMetadata();
    void startCharsetSearch();
    const FontFileInfo *representativeInfo(const FontFamily &f) const;
    // A symbol font maps letters to pictures, so its name is not set in it.
    bool markSymbolic();
    // stage
    void extractNext();
    void scanStage();
    void onStageScanFinished();
    void validateStagedFaces();
    void runFontCache(const QString &dir);
    void releaseStageTemp();
    QString expandHome(const QString &path) const;
    // library
    void saveLibrary();
    QString writeFontconfig();

    FamilyModel m_model;
    QList<FontFamily> m_families;
    QSet<QString> m_latin;
    QString m_filter;
    QString m_searchScope = QStringLiteral("names");
    QSet<QString> m_charsetMatches;
    bool m_charsetPending = false;
    QString m_source = QStringLiteral("all");
    bool m_showCoverage = false;
    bool m_groupWeights = true;
    QString m_sortOrder = QStringLiteral("name");
    QString m_view = QStringLiteral("specimen");
    qreal m_specimenScale = 1.0;
    bool m_scanning = false;
    bool m_busy = false;
    bool m_readingMetadata = false;
    QString m_status;
    bool m_statusIsError = false;
    QString m_fontRoot;
    QString m_fontSetTool;
    QString m_selectedName;
    QString m_pendingSelect;
    QString m_sample;
    bool m_reducedMotion = false;
    bool m_highContrast = false;
    Library m_library;
    QString m_libraryPath;
    bool m_libraryUnreadable = false;

    // Metadata from the files themselves, keyed by path.
    QHash<QString, FontFileInfo> m_info;
    QThread *m_metaThread = nullptr;
    QList<QThread *> m_metaThreads;  // every worker still running, stopped ones included
    bool m_rescanPending = false;
    std::shared_ptr<std::atomic<bool>> m_metaCancel;
    int m_metaGeneration = 0;

    // Scanning
    QProcess *m_scanProcess = nullptr;
    QProcess *m_latinProcess = nullptr;
    QProcess *m_charsetProcess = nullptr;
    bool m_scanDone = true;
    bool m_latinDone = true;
    QString m_scanOutput;
    QString m_latinOutput;
    bool m_scanTruncated = false;

    // Staging
    QStringList m_stageTargets;
    QStringList m_stageArchives;
    std::unique_ptr<QTemporaryDir> m_stageTemp;
    QProcess *m_extractProcess = nullptr;
    QProcess *m_stageScanProcess = nullptr;
    QList<FontFace> m_stagedFaces;
    QHash<QString, FontFileInfo> m_stagedInfo;
    QString m_stagedSource;
    QString m_stagedFormat = QStringLiteral("all");
    bool m_stagedTruncated = false;
    QString m_conflictPolicy = QStringLiteral("replace");
    bool m_installInvalid = false;

    // Repertoire and reports
    QProcess *m_repertoireProcess = nullptr;
    QVariantList m_repertoire;
    int m_repertoireTotal = 0;
    QString m_repertoireFile;
    QVariantList m_validationReport;
    QString m_validationTitle;
    QStringList m_compare;
    QVariantList m_tidy;
    QString m_pendingExportCollection;

    QProcess *m_cacheProcess = nullptr;
    QProcess *m_fontSetProcess = nullptr;
    PortalFileChooser *m_picker = nullptr;
    bool m_picking = false;
    QString m_pendingPdfKind;

    static constexpr int kOutputCap = 8 * 1024 * 1024;
    static constexpr int kMaxFaces = 2000;
    static constexpr int kMaxListLines = 40000;
    static constexpr int kRepertoireCap = 6000;
};
