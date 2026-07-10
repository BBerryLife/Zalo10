APP_NAME = Zalo10Headless

CONFIG += qt warn_on
# LƯU Ý: KHÔNG có "cascades10" ở đây — target này không dùng Cascades/QML,
# chỉ dùng bb::Application (core) + Qt4 thuần. Đây là điểm khác biệt quan
# trọng nhất so với Zalo10.pro.

PKGNAME = com.berrylife.zalo10.headless
# Cùng package id gốc với Zalo10 (xem bar-descriptor.xml) — 2 target đóng
# chung 1 .bar, chia sẻ chung sandbox filesystem (QDir::homePath()), đây là
# điều kiện bắt buộc để cả 2 process đọc/ghi chung 1 file zalo_messages.db.

QT += network script
# Không cần "gui" — service không vẽ gì cả.

LIBS += -lbb -lbbsystem
LIBS += -lQtNetwork -lQtScript
LIBS += -lssl -lcrypto
LIBS += -lsqlite3

INCLUDEPATH += src

HEADERS += \
    src/HeadlessService.hpp \
    src/EventBridgeServer.hpp \
    src/ZaloService.hpp \
    src/ZaloServiceUtils.hpp

SOURCES += \
    src/main_headless.cpp \
    src/HeadlessService.cpp \
    src/EventBridgeServer.cpp \
    src/ZaloService.cpp \
    src/ZaloService_Auth.cpp \
    src/ZaloService_WebSocket.cpp \
    src/ZaloService_Contacts.cpp \
    src/ZaloService_Messages.cpp \
    src/ZaloService_Crypto.cpp \
    src/ZaloService_Network.cpp \
    src/ZaloService_Db.cpp \
    src/ZaloService_Ipc.cpp

OTHER_FILES += \
    bar-descriptor.xml
