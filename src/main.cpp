#include "backend.h"
#include "cli.h"
#include "theme.h"

#include <QCommandLineParser>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>

#ifndef OMAFONT_VERSION
#define OMAFONT_VERSION "dev"
#endif

int main(int argc, char *argv[]) {
    // Headless verbs (list, info, install, remove) never open a window, so
    // they must not need a display either.
    const bool headless = argc > 1 && Cli::isVerb(QString::fromLocal8Bit(argv[1]));
    if (headless && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);

    // The identity trio. setDesktopFileName is what becomes the Wayland app_id,
    // which is what Hyprland rules, the taskbar and the icon lookup key off.
    app.setApplicationName(QStringLiteral("omafont"));
    app.setOrganizationName(QStringLiteral("omafont"));
    app.setApplicationDisplayName(QStringLiteral("OmaFont"));
    app.setApplicationVersion(QStringLiteral(OMAFONT_VERSION));
    app.setDesktopFileName(QStringLiteral("omafont"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omafont")));

    // OmaFont's own Controls style (src/style/OmaFontStyle, in qrc under the
    // default qt/qml import path): the Oma suite look, falling back to Basic
    // for anything it does not restyle.
    QQuickStyle::setStyle(QStringLiteral("OmaFontStyle"));

    if (headless) {
        Backend backend;
        QStringList words;
        for (int i = 1; i < argc; ++i) words.append(QString::fromLocal8Bit(argv[i]));
        return Cli::run(backend, words);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("OmaFont — fonts for Omarchy."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Font files, folders or archives to stage for installing."));
    parser.process(app);

    OmarchyTheme theme;
    Backend backend;

    qmlRegisterSingletonInstance("Omafont", 1, 0, "OmarchyTheme", &theme);
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Theme.qml")), "Omafont", 1, 0, "Theme");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/ui/icons/Icons.qml")), "Omafont", 1, 0, "Icons");
    qmlRegisterType(QUrl(QStringLiteral("qrc:/ui/Icon.qml")), "Omafont", 1, 0, "Icon");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/ui/Sheets.qml")), "Omafont", 1, 0, "Sheets");

    // Carry the desktop's text size into the default font so every control
    // grows with `omarchy display text size`, without a restart.
    const qreal basePointSize = app.font().pointSizeF();
    const auto scaleFont = [&app, basePointSize](qreal scale) {
        QFont font = app.font();
        font.setPointSizeF(basePointSize * scale);
        app.setFont(font);
    };
    scaleFont(theme.textScale());
    QObject::connect(&theme, &OmarchyTheme::textScaleChanged, &app, [&theme, scaleFont] {
        scaleFont(theme.textScale());
    });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings) qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the OmaFont interface.";
        return -1;
    }

    backend.refresh();
    // `omafont Foo.ttf` and "Open with" stage what was handed over.
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) backend.stage(files);

    return app.exec();
}
