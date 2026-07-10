#include "HeadlessService.hpp"
#include <QDebug>

// ---------------------------------------------------------------------------
// HeadlessService — process nền giữ session/WebSocket Zalo sống xuyên suốt,
// độc lập với việc ApplicationUI (UI app) có đang mở hay không.
//
// Đây là câu trả lời cho vấn đề gốc: trước đây, đóng active frame (vuốt card)
// = SIGTERM cả process duy nhất đang giữ WS = mất kết nối thật sự, và lần mở
// lại phải tự loadSession() + refreshSessionKey() từ đầu, dễ fail nếu cookie
// đã bị server invalidate trong lúc app tắt (=> phải quét QR lại).
// Giờ WS sống trong process RIÊNG (target thứ 2, xem Zalo10.pro), được
// Navigator khởi động qua invoke bb.action.system.STARTED, và permission
// _sys_headless_nostop (bar-descriptor.xml) ngăn OS tự dọn process này.
// ---------------------------------------------------------------------------

HeadlessService::HeadlessService(QObject *parent)
    : QObject(parent),
      m_zService(new ZaloService(this)),
      m_commandPollTimer(new QTimer(this)),
      m_eventBridge(new EventBridgeServer(m_zService, this))
{
    qDebug() << "[HeadlessService] starting...";

    connect(m_commandPollTimer, SIGNAL(timeout()), this, SLOT(onCommandPollTimer()));
    m_commandPollTimer->start(500); // 500ms — đủ nhanh cho UX chat, nhẹ CPU

    // Tự khôi phục session ngay khi service khởi động — KHÔNG chờ UI mở lên.
    // Đây chính là điểm mấu chốt: trước kia loadSession() chỉ được gọi từ
    // ApplicationUI::ApplicationUI() (constructor UI, tức phải mở app), giờ
    // nó chạy ngay khi Navigator start service lúc device boot xong.
    if (m_zService->loadSession()) {
        qDebug() << "[HeadlessService] session restored, refreshing + reconnecting WS";
    } else {
        qDebug() << "[HeadlessService] no saved session — waiting for UI to trigger QR login";
    }
}

HeadlessService::~HeadlessService()
{
    qDebug() << "[HeadlessService] shutting down — saving session";
    m_zService->saveSession();
    m_zService->closeWebSocketGracefully();
}

void HeadlessService::onCommandPollTimer()
{
    m_zService->processCommandQueue();
}
