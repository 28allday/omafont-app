QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = omafont
TEMPLATE = app

INCLUDEPATH += src

HEADERS += \
    src/backend.h \
    src/fontfile.h \
    src/library.h \
    src/cli.h \
    src/filepicker.h \
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/backendstage.cpp \
    src/backendlibrary.cpp \
    src/backendexport.cpp \
    src/fontfile.cpp \
    src/library.cpp \
    src/cli.cpp \
    src/filepicker.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc

