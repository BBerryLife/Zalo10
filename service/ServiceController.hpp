#ifndef SERVICECONTROLLER_HPP
#define SERVICECONTROLLER_HPP

#include <QObject>

class QTimer;
class ZaloService;
namespace bb { namespace system { class InvokeManager; class InvokeRequest; } }

// Điều phối service headless: giữ WebSocket khi UI KHÔNG chạy, nhường khi UI chạy.
class ServiceController : public QObject
{
    Q_OBJECT
public:
    explicit ServiceController(QObject *parent = 0);
    ZaloService *service() const { return m_svc; }

private slots:
    void onInvoked(const bb::system::InvokeRequest &request);
    void onPollTimer();
    void onSessionExpired();

private:
    void evaluate();

    ZaloService *m_svc;
    QTimer      *m_poll;
    bb::system::InvokeManager *m_invoke;
    bool m_active;       // service đang giữ realtime
    bool m_sessionDead;  // phiên hết hạn / chưa đăng nhập: không retry cho tới khi UI chạy rồi đóng
    bool m_lastUiAlive;
};

#endif
