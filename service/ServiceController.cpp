#include "ServiceController.hpp"
#include "ZaloService.hpp"
#include "ServiceHandoff.hpp"

#include <bb/system/InvokeManager>
#include <bb/system/InvokeRequest>
#include <QTimer>
#include <QDebug>

// Chu kỳ poll file PID của UI. Đủ ngắn để nhận lại WS nhanh sau khi UI đóng,
// đủ thưa để không đáng kể về pin (so với ping WS 25s).
static const int UI_POLL_MS = 4000;

ServiceController::ServiceController(QObject *parent)
    : QObject(parent),
      m_svc(new ZaloService(this)),
      m_poll(new QTimer(this)),
      m_invoke(new bb::system::InvokeManager(this)),
      m_active(false), m_sessionDead(false), m_lastUiAlive(false)
{
    // Lúc service chạy thì app UI không foreground: để sendBannerNotification()
    // bỏ qua dialog, chỉ còn Hub item (đã tự phát âm/banner).
    m_svc->setAppForeground(false);

    connect(m_svc, SIGNAL(sessionExpired()), this, SLOT(onSessionExpired()));
    connect(m_invoke, SIGNAL(invoked(const bb::system::InvokeRequest&)),
            this,     SLOT(onInvoked(const bb::system::InvokeRequest&)));
    connect(m_poll, SIGNAL(timeout()), this, SLOT(onPollTimer()));
    m_poll->start(UI_POLL_MS);

    qDebug() << "[Service] started, pid file:" << ServiceHandoff::pidFilePath();
    evaluate();
}

void ServiceController::onInvoked(const bb::system::InvokeRequest &request)
{
    qDebug() << "[Service] invoked, action =" << request.action();
    evaluate(); // STARTED / UI_OPENED / UI_CLOSED đều chỉ cần đánh giá lại trạng thái
}

void ServiceController::onPollTimer() { evaluate(); }

void ServiceController::onSessionExpired()
{
    // Headless không có UI để hiện màn QR: đứng yên, chờ user mở app đăng nhập lại.
    qDebug() << "[Service] session expired - idle until user logs in via the app";
    m_sessionDead = true;
    if (m_active) { m_svc->suspendRealtime(); m_active = false; }
}

void ServiceController::evaluate()
{
    bool ui = ServiceHandoff::isUiAlive();

    // UI vừa đóng -> có thể user đã đăng nhập lại trong lúc UI chạy: cho phép thử lại.
    if (m_lastUiAlive && !ui) m_sessionDead = false;
    m_lastUiAlive = ui;

    if (ui) {
        if (m_active) {
            qDebug() << "[Service] UI is running -> handing WebSocket over";
            m_svc->suspendRealtime();
            m_active = false;
        }
        return;
    }

    if (m_active || m_sessionDead) return;

    qDebug() << "[Service] UI not running -> taking WebSocket";
    m_active = true;
    if (!m_svc->resumeRealtime()) {
        qDebug() << "[Service] no saved session - idle";
        m_active = false;
        m_sessionDead = true;
    }
}
