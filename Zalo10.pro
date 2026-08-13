APP_NAME = Zalo10

CONFIG += qt warn_on cascades10

PKGNAME = com.berrylife.zalo10
# Version is read at runtime from bar-descriptor.xml via bb::ApplicationInfo().version()
# To release a new version, only update <versionNumber> in bar-descriptor.xml

# Qt modules — tất cả có sẵn trong BB10 NDK 10.3
QT += network script gui

# BB10 Cascades libs + OpenSSL (có sẵn trong BB10 NDK)
LIBS += -lbbcascades -lbbsystem -lbb -lbbplatform -lbbdevice -lbbpim
# bbcascadespickers = bb::cascades::pickers (ContactPicker) — FilePicker
# trước đây dùng đã kéo namespace pickers vào runtime rồi nhưng chưa từng
# link tường minh; ContactPicker cần include header thật (không chỉ QML
# plugin), nên phải thêm -lbbcascadespickers ở đây để linker resolve được.
LIBS += -lbbcascadespickers
LIBS += -lQtNetwork -lQtScript -lQtGui
LIBS += -lssl -lcrypto
LIBS += -lsqlite3

INCLUDEPATH += src
INCLUDEPATH += $$(QNX_TARGET)/usr/include/qt4/QtGui

HEADERS += \
    src/applicationui.hpp \
    src/ZaloService.hpp \
    src/ZaloServiceUtils.hpp \
    src/ActiveFrameCover.hpp

SOURCES += \
    src/main.cpp \
    src/applicationui.cpp \
    src/ZaloService.cpp \
    src/ZaloService_Auth.cpp \
    src/ZaloService_WebSocket.cpp \
    src/ZaloService_Contacts.cpp \
    src/ZaloService_Messages.cpp \
    src/ZaloService_ContactPicker.cpp \
    src/ZaloService_Crypto.cpp \
    src/ZaloService_Network.cpp \
    src/ZaloService_Db.cpp \
    src/ActiveFrameCover.cpp

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
    assets/QuickMessagesSheet.qml \
    assets/QuickMessageEditSheet.qml \
    assets/AttachPickerSheet.qml \
    assets/VoiceNoteSheet.qml
