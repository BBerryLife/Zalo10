# Zalo10Service — headless notifier. Đặt thư mục này cạnh dự án UI:
#   <workspace>/Zalo10/          (app UI, có src/)
#   <workspace>/Zalo10Service/   (thư mục này)
APP_NAME = Zalo10Service

CONFIG += qt warn_on
CONFIG -= cascades10
# QtGui chỉ để dùng QImage trong ZaloService (không tạo QApplication/widget).
QT += network script gui

UI_SRC = ../Zalo10/src

LIBS += -lbbsystem -lbb -lbbplatform -lbbdevice -lbbpim -lunifieddatasourcec
LIBS += -lQtNetwork -lQtScript -lQtGui
LIBS += -lssl -lcrypto -lsqlite3
LIBS += -L$$PWD/$$UI_SRC/third_party/webp/lib -lwebpdecoder

INCLUDEPATH += . $$UI_SRC $$UI_SRC/third_party/webp/include
INCLUDEPATH += $$(QNX_TARGET)/usr/include/qt4/QtGui

HEADERS += \
    ServiceController.hpp \
    $$UI_SRC/ServiceHandoff.hpp \
    $$UI_SRC/ZaloCookieJar.hpp \
    $$UI_SRC/ZaloService.hpp \
    $$UI_SRC/ZaloServiceUtils.hpp \
    $$UI_SRC/HubIntegration.hpp

SOURCES += \
    main.cpp \
    ServiceController.cpp \
    ContactPickerStub.cpp \
    $$UI_SRC/ZaloService.cpp \
    $$UI_SRC/ZaloService_Auth.cpp \
    $$UI_SRC/ZaloService_WebSocket.cpp \
    $$UI_SRC/ZaloService_Contacts.cpp \
    $$UI_SRC/ZaloService_Messages.cpp \
    $$UI_SRC/ZaloService_Crypto.cpp \
    $$UI_SRC/ZaloService_Network.cpp \
    $$UI_SRC/ZaloService_Db.cpp \
    $$UI_SRC/HubIntegration.cpp
