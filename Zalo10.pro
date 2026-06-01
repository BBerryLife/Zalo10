APP_NAME = Zalo10

CONFIG += qt warn_on cascades10

PKGNAME = com.example.zalo10
VERSION = 1.0.0

# Qt modules — tất cả có sẵn trong BB10 NDK 10.3
QT += network script

# BB10 Cascades libs + OpenSSL (có sẵn trong BB10 NDK)
LIBS += -lbbcascades -lbbsystem -lbb
LIBS += -lQtNetwork -lQtScript
LIBS += -lssl -lcrypto
LIBS += -lsqlite3

INCLUDEPATH += src

HEADERS += \
    src/applicationui.hpp \
    src/ZaloService.hpp

SOURCES += \
    src/main.cpp \
    src/applicationui.cpp \
    src/ZaloService.cpp

OTHER_FILES += \
    bar-descriptor.xml \
    assets/main.qml \
    assets/ChatList.qml \
    assets/ChatView.qml \
    assets/ContactsTab.qml \
    assets/LoginView.qml \
    assets/ProfileView.qml \
    assets/SettingsSheet.qml \
    assets/AboutSheet.qml
