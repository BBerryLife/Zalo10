APP_NAME = Zalo10

CONFIG += qt warn_on cascades10

PKGNAME = com.berrylife.zalo10
VERSION = 1.1.0

# Inject version as integer components — avoids all string/dot escaping issues on Windows qmake
# Keep in sync with bar-descriptor.xml: <versionNumber>1.1.0</versionNumber> <buildId>1</buildId>
DEFINES += APP_VER_MAJOR=1 APP_VER_MINOR=1 APP_VER_PATCH=0 APP_VER_BUILD=1

# Qt modules — tất cả có sẵn trong BB10 NDK 10.3
QT += network script

# BB10 Cascades libs + OpenSSL (có sẵn trong BB10 NDK)
LIBS += -lbbcascades -lbbsystem -lbb -lbbplatform
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
    assets/EmojiPanel.qml \
    assets/EmojiButton.qml \
    assets/ContactsTab.qml \
    assets/LoginView.qml \
    assets/ProfileView.qml \
    assets/SettingsSheet.qml \
    assets/AboutSheet.qml
