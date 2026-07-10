#ifndef EVENTBRIDGESERVER_HPP
#define EVENTBRIDGESERVER_HPP

#include <QObject>
#include <QTcpServer>
#include <QList>
#include <QString>
#include <QVariant>
#include "ZaloService.hpp"

class QTcpSocket;

// EventBridgeServer — chạy trong HeadlessService, lắng nghe trên
// 127.0.0.1:EVENTBRIDGE_PORT. Mỗi khi ZaloService (thật, sống trong
// HeadlessService) emit 1 trong các signal "sự kiện tạm thời" (không có bảng
// SQLite tự nhiên để lưu — ảnh vừa tải xong, kết quả gửi tin, danh sách bạn
// bè mới...), EventBridgeServer serialize thành 1 dòng JSON và gửi cho MỌI
// client đang kết nối (thường chỉ có 0 hoặc 1: ApplicationUI, khi đang mở).
//
// Đây là kênh RIÊNG so với SQLite command_queue/service_state — dùng theo
// đúng khuyến nghị chính thức của BlackBerry cho giao tiếp UI<->headless
// "trực tiếp theo thời gian thực" (xem tài liệu Headless Apps, mục local
// socket/QTcpSocket), thay vì cố nhồi mọi loại sự kiện qua SQLite polling.
//
// Nếu KHÔNG có client nào kết nối (UI đang đóng), server vẫn hoạt động bình
// thường — chỉ đơn giản là gửi() không có ai nhận, dữ liệu bị bỏ qua. Điều
// này AN TOÀN vì mọi dữ liệu quan trọng thật sự (tin nhắn) đã được ghi vào
// SQLite trước đó rồi (dbSaveMessage) — EventBridge chỉ là "thông báo tức
// thời cho UI đang mở", không phải nguồn sự thật duy nhất.
class EventBridgeServer : public QObject
{
    Q_OBJECT

public:
    static const quint16 PORT = 47811; // cổng cục bộ tuỳ ý, chỉ cần cố định 2 bên

    explicit EventBridgeServer(ZaloService *service, QObject *parent = 0);

private slots:
    void onNewConnection();
    void onClientDisconnected();

    // Forward 1-1 từ mọi signal "tạm thời" của ZaloService thật sang broadcastEvent()
    void onNewMessage(const QString &threadId, const QVariantMap &message);
    void onMessagesReady(const QString &threadId, const QVariantList &messages);
    void onFriendsReady(const QVariantList &friends);
    void onConversationsReady(const QVariantList &threads);
    void onInvitesReady(const QVariantList &invites);
    void onFriendRequestResponded(const QString &friendId, bool accepted, bool success);
    void onAvatarReady(const QString &threadId, const QString &localPath);
    void onImageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void onMessageSent(bool success, const QString &threadId);
    void onMessageRecalled(const QString &threadId, const QString &msgId);
    void onMessageDeletedLocally(const QString &threadId, const QString &msgId);
    void onMessageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void onMessageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void onMuteDone(const QString &threadId, bool muted, bool success);
    void onBlockUserDone(const QString &userId, bool success);
    void onUnblockUserDone(const QString &userId, bool success);
    void onClearHistoryDone(const QString &threadId, bool success);
    void onLeaveGroupDone(const QString &groupId, bool success);
    void onServerQuickMessagesReady(int imported, int skipped, const QString &error);

private:
    QTcpServer      *m_server;
    QList<QTcpSocket*> m_clients;

    // eventName + payload map -> 1 dòng JSON (kết thúc bằng '\n', vì
    // QTcpSocket là stream, cần delimiter để bên nhận biết ranh giới message)
    void broadcastEvent(const QString &eventName, const QVariantMap &payload);
};

#endif // EVENTBRIDGESERVER_HPP
