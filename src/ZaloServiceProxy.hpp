#ifndef ZALOSERVICEPROXY_HPP
#define ZALOSERVICEPROXY_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QTimer>
#include <QTcpSocket>
#include <bb/system/InvokeManager>
#include "ZaloService.hpp"
#include "EventBridgeServer.hpp" // chỉ để lấy hằng số PORT dùng chung 2 phía

// ZaloServiceProxy — thay thế ZaloService làm context property "zService" cho
// QML trong UI process (ApplicationUI). Giữ NGUYÊN chữ ký Q_INVOKABLE của
// ZaloService để KHÔNG phải sửa 59 lời gọi zService.* rải trong assets/*.qml.
//
// Bên trong, mỗi hàm rơi vào 1 trong 2 nhóm:
//
//   Nhóm A (ghi / network — sendMessage, fetchFriends, startQRLogin...):
//     KHÔNG gọi thẳng ZaloService. Chỉ ghi 1 dòng JSON vào bảng command_queue
//     (SQLite). HeadlessService (process riêng, xem HeadlessService.cpp) đọc
//     bảng này định kỳ và mới là nơi thật sự gọi ZaloService/nối mạng. Đây là
//     điểm mấu chốt của toàn bộ kiến trúc: UI process không bao giờ tự mở kết
//     nối WebSocket/HTTP nữa, tránh tình trạng "2 kết nối WS song song" phá
//     session khi UI đang mở cùng lúc với service.
//
//   Nhóm B (đọc dữ liệu local — dbLoadMessages, getQuickMessages,
//   getImageDimensions...): những hàm này chỉ đụng SQLite (đã bật WAL, đọc
//   đồng thời với service an toàn) hoặc filesystem cục bộ, không đụng mạng.
//   Forward THẲNG tới 1 instance ZaloService nội bộ (m_localService) — instance
//   này KHÔNG BAO GIỜ được gọi loadSession()/startQRLogin()/bất kỳ hàm mạng
//   nào, chỉ tồn tại để tái dùng code DB/local đã có sẵn.
//
// Trạng thái (loggedIn, uid, qrImagePath...) không đọc từ m_localService (nó
// luôn m_loggedIn=false vì không bao giờ login) mà đọc từ bảng service_state
// do HeadlessService ghi — xem onStatePollTimer()/refreshStateFromDb().
class ZaloServiceProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)

public:
    explicit ZaloServiceProxy(QObject *parent = 0);
    virtual ~ZaloServiceProxy();

    bool loggedIn() const { return m_loggedIn; }

    // ---- Nhóm A: ghi vào command_queue, HeadlessService xử lý thật ----------
    Q_INVOKABLE void startQRLogin();
    Q_INVOKABLE void retryQRLogin();
    Q_INVOKABLE void cancelQRLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei = "", const QString &ua = "", const QString &token = "");

    Q_INVOKABLE void fetchConversations();
    Q_INVOKABLE void fetchFriends();
    Q_INVOKABLE void fetchInvites();
    Q_INVOKABLE void acceptFriendRequest(const QString &friendId);
    Q_INVOKABLE void rejectFriendRequest(const QString &friendId);
    Q_INVOKABLE void fetchGroupDetails(const QStringList &groupIds);
    Q_INVOKABLE void fetchMessages(const QString &threadId, bool isGroup);
    Q_INVOKABLE void sendMessage(const QString &threadId, const QString &content, bool isGroup);
    Q_INVOKABLE void deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId, const QString &senderId, bool onlyMe);
    Q_INVOKABLE void recallMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId);
    Q_INVOKABLE void sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption = QString());
    Q_INVOKABLE void sendFile(const QString &threadId, const QString &localFilePath, bool isGroup);
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId = QString());
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);

    Q_INVOKABLE void setActiveThread(const QString &threadId, bool isGroup);
    Q_INVOKABLE void clearActiveThread();
    Q_INVOKABLE void blockUser(const QString &userId);
    Q_INVOKABLE void unblockUser(const QString &userId);
    Q_INVOKABLE void setMute(const QString &threadId, bool isGroup, bool mute);
    Q_INVOKABLE void clearHistory(const QString &threadId, bool isGroup);
    Q_INVOKABLE void leaveGroup(const QString &groupId);
    Q_INVOKABLE void fetchServerQuickMessages();

    // ---- Nhóm B: đọc/ghi local, forward thẳng tới m_localService -------------
    // (không qua command_queue vì không cần HeadlessService xử lý — đây là dữ
    // liệu/thao tác cục bộ mà UI có thể tự làm ngay, giữ UX phản hồi tức thì
    // như trước, ví dụ hiển thị lịch sử chat cũ ngay khi mở ChatView)
    Q_INVOKABLE bool isBlocked(const QString &userId) const { return m_localService->isBlocked(userId); }
    Q_INVOKABLE bool isMutedThread(const QString &threadId) const { return m_localService->isMutedThread(threadId); }
    Q_INVOKABLE void dbSaveMessage(const QVariantMap &msg, const QString &threadId) { m_localService->dbSaveMessage(msg, threadId); }
    Q_INVOKABLE QVariantList dbLoadMessages(const QString &threadId) { return m_localService->dbLoadMessages(threadId); }
    Q_INVOKABLE QVariantMap getThreadLastMessages() const { return m_localService->getThreadLastMessages(); }
    Q_INVOKABLE QVariantList getQuickMessages() const { return m_localService->getQuickMessages(); }
    Q_INVOKABLE int addQuickMessage(const QString &name, const QString &content) { return m_localService->addQuickMessage(name, content); }
    Q_INVOKABLE bool updateQuickMessage(int id, const QString &name, const QString &content) { return m_localService->updateQuickMessage(id, name, content); }
    Q_INVOKABLE bool deleteQuickMessage(int id) { return m_localService->deleteQuickMessage(id); }
    Q_INVOKABLE QString cacheLocalImage(const QString &sourcePath) { return m_localService->cacheLocalImage(sourcePath); }
    Q_INVOKABLE QVariantMap getImageDimensions(const QString &localFilePath) const { return m_localService->getImageDimensions(localFilePath); }
    Q_INVOKABLE qint64 getFileSize(const QString &localFilePath) const { return m_localService->getFileSize(localFilePath); }
    Q_INVOKABLE QString downloadPhotoToGallery(const QString &localImagePath, const QString &msgId) { return m_localService->downloadPhotoToGallery(localImagePath, msgId); }
    Q_INVOKABLE QVariantMap exportData(const QString &destDir) { return m_localService->exportData(destDir); }
    Q_INVOKABLE QVariantMap importData(const QString &jsonFilePath) { return m_localService->importData(jsonFilePath); }
    Q_INVOKABLE int clearCache() { return m_localService->clearCache(); }
    // downloadUpdate/cancelUpdateDownload: tự tải file .bar cập nhật app, việc
    // này liên quan tới chính UI app (không phải tài khoản Zalo), giữ nguyên
    // như cũ — chạy trong UI process, không cần qua HeadlessService.
    Q_INVOKABLE void downloadUpdate(const QString &url, const QString &filename) { m_localService->downloadUpdate(url, filename); }
    Q_INVOKABLE void cancelUpdateDownload() { m_localService->cancelUpdateDownload(); }
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false) {
        m_localService->sendHubNotification(title, body, threadId, isGroup);
    }

signals:
    void loggedInChanged();
    void loginFailed(const QString &message);
    void sessionExpired();
    void loginSuccess(const QString &uid, const QString &displayName);
    void sessionRefreshed();
    void qrCodeReady(const QString &imagePath, const QString &qrCode);
    void qrScanned(const QString &displayName);
    void qrExpired();

    // ---- Forward từ EventBridgeServer (xem onEventBridgeReadyRead()) --------
    // Chữ ký giống hệt các signal cùng tên trong ZaloService.hpp — QML dùng
    // Connections { target: zService; onNewMessage: ... } y hệt như trước,
    // không cần sửa gì ở phía .qml.
    void conversationsReady(const QVariantList &threads);
    void friendsReady(const QVariantList &friends);
    void invitesReady(const QVariantList &invites);
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    void messageRecalled(const QString &threadId, const QString &msgId);
    void messageDeletedLocally(const QString &threadId, const QString &msgId);
    void messageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void messageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void avatarReady(const QString &threadId, const QString &localPath);
    void imageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void blockUserDone(const QString &userId, bool success);
    void unblockUserDone(const QString &userId, bool success);
    void muteDone(const QString &threadId, bool muted, bool success);
    void clearHistoryDone(const QString &threadId, bool success);
    void leaveGroupDone(const QString &groupId, bool success);
    void serverQuickMessagesReady(int imported, int skipped, const QString &error);

    // Bắn ra mỗi khi kết nối TCP tới EventBridgeServer được (re)thiết lập —
    // bao gồm cả lần đầu VÀ mọi lần reconnect sau khi rớt. Lý do cần: mọi
    // event tạm thời (avatarReady, imageMsgReady...) broadcast trong lúc UI
    // đang mất kết nối (ví dụ: HeadlessService vừa được tự invoke lại sau
    // khi UI 3 lần connect thất bại — xem onEventBridgeReconnectTimer()) bị
    // MẤT VĨNH VIỄN, vì EventBridgeServer không lưu lại gì cả (đúng thiết kế
    // — xem comment trong EventBridgeServer.hpp: "gửi() không có ai nhận, dữ
    // liệu bị bỏ qua"). QML nghe signal này để tự phát lại các yêu cầu còn
    // thiếu kết quả (vd downloadAvatar cho item chưa có localAvatar) — với
    // avatar cụ thể, việc phát lại gần như miễn phí vì file đã nằm sẵn trên
    // đĩa (avatar_meta cache), nên downloadAvatar() trả về ngay lập tức
    // không cần tải mạng lại.
    void eventBridgeReconnected();

private slots:
    // Poll bảng service_state (ghi bởi HeadlessService) để cập nhật loggedIn/
    // qrCodeReady/... cho UI — đây là "chiều ngược lại" của command_queue.
    void onStatePollTimer();

    // ---- EventBridge client (kết nối tới HeadlessService::EventBridgeServer) --
    void onEventBridgeConnected();
    void onEventBridgeDisconnected();
    void onEventBridgeReadyRead();
    void onEventBridgeReconnectTimer(); // tự kết nối lại nếu HeadlessService chưa kịp khởi động hoặc rớt kết nối

private:
    void writeCommand(const QString &command, const QVariantMap &args);
    void refreshStateFromDb();
    void dispatchEventLine(const QString &jsonLine);

    ZaloService *m_localService; // chỉ dùng cho DB/local — KHÔNG BAO GIỜ gọi loadSession/network
    QTimer      *m_statePollTimer;
    bool         m_loggedIn;
    QString      m_lastQrImagePath;
    QString      m_lastQrCodeRaw;
    QString      m_lastSessionExpiredFlag;

    QTcpSocket  *m_eventSocket;
    QTimer      *m_reconnectTimer;
    QByteArray   m_recvBuffer; // tích luỹ bytes tới khi đủ 1 dòng ('\n')

    // Lưới an toàn: bar-descriptor.xml chỉ trigger invoke-target headless qua
    // bb.action.system.STARTED (lúc BOOT máy) — nếu user chỉ đóng app bình
    // thường (không reboot) và vì lý do gì đó process headless đã chết/chưa
    // từng chạy, không có cơ chế OS nào tự khởi động lại nó. Nếu sau vài lần
    // thử kết nối EventBridge liên tiếp đều thất bại, tự invoke() thẳng
    // headless target 1 lần — xem onEventBridgeReconnectTimer().
    bb::system::InvokeManager *m_invokeManager;
    int          m_connectFailCount;
    bool         m_headlessInvokeAttempted;
};

#endif // ZALOSERVICEPROXY_HPP
