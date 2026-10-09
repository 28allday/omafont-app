#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

// The XDG desktop portal file chooser. Under Wayland this is the only picker
// that looks and behaves like the rest of the desktop — never QFileDialog.
//
// Every call is asynchronous: it returns immediately and the answer arrives as
// one of `selected`, `selectedMany` or `folderSelected` (whichever fits the
// call), or `canceled` / `failed`.
class PortalFileChooser : public QObject {
    Q_OBJECT

public:
    explicit PortalFileChooser(QObject *parent = nullptr);

    // `patterns` are globs, e.g. {"*.md", "*.markdown"}; empty means any file.
    Q_INVOKABLE void openFile(const QString &title, const QString &filterName = QString(),
                              const QStringList &patterns = {});
    // Several files at once; the answer arrives as `selectedMany`.
    Q_INVOKABLE void openFiles(const QString &title, const QString &filterName = QString(),
                               const QStringList &patterns = {});
    // A folder; the answer arrives as `folderSelected`.
    Q_INVOKABLE void pickFolder(const QString &title);
    Q_INVOKABLE void saveFile(const QString &title, const QString &suggestedName,
                              const QString &filterName = QString(),
                              const QStringList &patterns = {});

signals:
    void selected(const QUrl &url);
    void selectedMany(const QList<QUrl> &urls);
    void folderSelected(const QUrl &url);
    void canceled();
    void failed(const QString &message);

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    bool request(const QString &method, const QString &title, QVariantMap options);
    void clearPending();

    QString m_pendingPath;
    // What the open request was for, so its answer goes out as exactly one
    // signal: `selected`, `selectedMany` or `folderSelected`.
    enum class Pending { Single, Many, Folder };
    Pending m_pendingKind = Pending::Single;
};
