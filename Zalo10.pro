APP_NAME = Zalo10

CONFIG += qt warn_on cascades10

PKGNAME = com.berrylife.zalo10
# Version is read at runtime from bar-descriptor.xml (bb::ApplicationInfo().version());
# to release, only update <versionNumber> there.

QT += network script gui

LIBS += -lbbcascades -lbbsystem -lbb -lbbplatform -lbbdevice -lbbpim
# -lunifieddatasourcec is the real UDS library name ("uds" fails with "cannot find -luds").
LIBS += -lunifieddatasourcec
# -lbbcascadespickers: ContactPicker needs the real header and an explicit link.
LIBS += -lbbcascadespickers
LIBS += -lQtNetwork -lQtScript -lQtGui
LIBS += -lssl -lcrypto
LIBS += -lsocket   # POSIX sockets used directly by PhoneSyncCore::httpsGet
LIBS += -lsqlite3

# libwebpdecoder (vendored decode-only build for QNX armv7le/gcc 4.6.3, see
# src/third_party/webp/README.md): decodes sticker .webp since Cascades has no WebP codec.
LIBS += -L$$PWD/src/third_party/webp/lib -lwebpdecoder
INCLUDEPATH += $$PWD/src/third_party/webp/include

INCLUDEPATH += src

# src/third_party/xz: XZ Embedded (vendored, 0BSD) for phone sync. Built in qcc's default C mode;
# do NOT add -std=gnu99 (qcc: unknown option).
INCLUDEPATH += $$(QNX_TARGET)/usr/include/qt4/QtGui

HEADERS += \
    src/utils/ZaloCookieJar.hpp \
    src/core/ServiceHandoff.hpp \
    src/utils/LogSanitizer.hpp \
    src/utils/ZaloNetworkManager.hpp \
    src/core/PhoneSyncCore.hpp \
    src/applicationui.hpp \
    src/core/ZaloService.hpp \
    src/utils/ZaloServiceUtils.hpp \
    src/ui/ActiveFrameCover.hpp \
    src/core/HubIntegration.hpp

SOURCES += \
    src/main.cpp \
    src/applicationui.cpp \
    src/core/ZaloService.cpp \
    src/core/ZaloService_Auth.cpp \
    src/core/ZaloService_WebSocket.cpp \
    src/core/ZaloService_Contacts.cpp \
    src/core/ZaloService_Messages.cpp \
    src/core/ZaloService_ContactPicker.cpp \
    src/core/ZaloService_Crypto.cpp \
    src/core/ZaloService_Network.cpp \
    src/core/ZaloService_Db.cpp \
    src/core/ZaloService_Sync.cpp \
    src/core/ZaloService_PhoneSync.cpp \
    src/core/PhoneSyncCore.cpp \
    src/third_party/xz/xz_crc32.c \
    src/third_party/xz/xz_crc64.c \
    src/third_party/xz/xz_dec_stream.c \
    src/third_party/xz/xz_dec_lzma2.c \
    src/ui/ActiveFrameCover.cpp \
    src/core/HubIntegration.cpp

OTHER_FILES += \
    bar-descriptor.xml \
    assets/main.qml \
    assets/ActiveFrameCover.qml \
    assets/images/ActiveFrame/activeframe_zl10_big.png \
    assets/images/ActiveFrame/activeframe_zl10_medium.png \
    assets/images/ActiveFrame/Activeframe_zl10_Small.png \
    assets/ChatList.qml \
    assets/ChatView.qml \
    assets/InfoDialog.qml \
    assets/ConfirmDialog.qml \
    assets/EmojiPanel.qml \
    assets/EmojiButton.qml \
    assets/ContactsTab.qml \
    assets/LoginView.qml \
    assets/ProfileView.qml \
    assets/SettingsSheet.qml \
    assets/AboutSheet.qml \
    assets/PollVoteSheet.qml \
    assets/QuickMessagesSheet.qml \
    assets/QuickMessageEditSheet.qml \
    assets/AttachPickerSheet.qml \
    assets/VoiceNoteSheet.qml
