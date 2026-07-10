#ifndef HEADLESSSERVICE_HPP
#define HEADLESSSERVICE_HPP

#include <QObject>
#include <QTimer>
#include "ZaloService.hpp"
#include "EventBridgeServer.hpp"

// HeadlessService: process KHÔNG UI, chạy độc lập với ApplicationUI (Zalo10
// target chính). Đây là nơi thật sự sở hữu ZaloService — giữ WebSocket sống,
// giữ session, tiếp tục nhận/gửi tin nhắn dù người dùng đã đóng active frame.
//
// Vòng đời: khởi động bởi Navigator khi nhận invoke bb.action.system.STARTED
// (xem bar-descriptor.xml, invoke-target type="service") — tức là chạy ngay
// từ lúc device boot xong, không phụ thuộc UI app có được mở hay không.
// Permission _sys_headless_nostop (đã có sẵn trong bar-descriptor.xml) ngăn
// OS tự kill process này khi rảnh — đây là điều kiện bắt buộc để nó thực sự
// "chạy nền vĩnh viễn" thay vì bị dọn sau vài phút như 1 headless thường.
//
// KHÔNG kế thừa bb::cascades::Application — cascades kéo theo toàn bộ hạ tầng
// UI/scene graph không cần thiết ở đây và (theo tài liệu headlesserviceui)
// không phải mục đích thiết kế của invoke-target kiểu service.
class HeadlessService : public QObject
{
    Q_OBJECT

public:
    explicit HeadlessService(QObject *parent = 0);
    ~HeadlessService();

private slots:
    // Poll định kỳ command_queue (do UI ghi vào) và dispatch cho ZaloService
    // thật. Poll thay vì QFileSystemWatcher vì watcher trên BB10 filesystem có
    // độ trễ/độ tin cậy không ổn định khi file bị sqlite WAL rewrite liên tục;
    // 500ms poll đủ nhanh cho UX chat, nhẹ CPU, và đơn giản để debug qua log.
    void onCommandPollTimer();

private:
    ZaloService *m_zService;
    QTimer      *m_commandPollTimer;
    EventBridgeServer *m_eventBridge;
};

#endif // HEADLESSSERVICE_HPP
