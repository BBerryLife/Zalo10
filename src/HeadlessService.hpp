#ifndef HEADLESSSERVICE_HPP
#define HEADLESSSERVICE_HPP

// ─── Zalo10 — Headless Service ────────────────────────────────────────────
//
// Chạy khi binary được invoke với target "com.BerryLife.Zalo10.headless"
// (BB10 set INVOKE_TARGET_KEY, main.cpp detect và rẽ vào nhánh này).
//
// Duy trì ZaloService + WebSocket sống liên tục, kể cả khi UI app đóng.
// UI app giao tiếp qua QLocalSocket tại SOCKET_PATH (JSON, length-prefixed).
//
// UI → Service: {"type":"call","id":"N","method":"X","args":[...]}
// Service → UI: {"type":"signal","name":"X","args":[...]}
//              {"type":"prop","name":"loggedIn","value":bool}
//              {"type":"result","id":"N","value":...}

#include <QObject>
#include <QList>
#include <QByteArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMap>

class ZaloService;

class HeadlessService : public QObject
{
    Q_OBJECT

public:
    static const char *SOCKET_PATH; // "/tmp/zalo10_ipc"

    explicit HeadlessService(QObject *parent = 0);
    virtual ~HeadlessService() {}

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

    // ZaloService signals → IPC broadcast
    void onLoggedInChanged();
    void onLoginFailed(const QString &message);
    void onSessionExpired();
    void onLoginSuccess(const QString &uid, const QString &displayName);
    void onSessionRefreshed();
    void onQrCodeReady(const QString &imagePath, const QString &qrCode);
    void onQrScanned(const QString &displayName);
    void onQrExpired();
    void onConversationsReady(const QVariantList &threads);
    void onFriendsReady(const QVariantList &friends);
    void onInvitesReady(const QVariantList &invites);
    void onFriendRequestResponded(const QString &friendId, bool accepted, bool success);
    void onMessagesReady(const QString &threadId, const QVariantList &messages);
    void onMessageSent(bool success, const QString &threadId);
    void onNewMessage(const QString &threadId, const QVariantMap &message);
    void onMessageRecalled(const QString &threadId, const QString &msgId);
    void onThreadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    void onAvatarReady(const QString &threadId, const QString &localPath);
    void onImageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void onBlockUserDone(const QString &userId, bool success);
    void onUnblockUserDone(const QString &userId, bool success);
    void onMuteDone(const QString &threadId, bool muted, bool success);
    void onClearHistoryDone(const QString &threadId, bool success);
    void onLeaveGroupDone(const QString &groupId, bool success);
    void onServerQuickMessagesReady(int imported, int skipped, const QString &error);

private:
    void handleMessage(QLocalSocket *client, const QVariantMap &msg);
    void broadcast(const QVariantMap &payload);
    void send(QLocalSocket *client, const QVariantMap &payload);
    void sendWelcomeState(QLocalSocket *client);
    static QByteArray encode(const QVariantMap &payload);
    static bool decodeNext(QByteArray &buf, QVariantMap &out);

    QLocalServer          *m_server;
    QList<QLocalSocket*>   m_clients;
    QMap<QLocalSocket*, QByteArray> m_readBufs;
    ZaloService           *m_zService;
};

#endif // HEADLESSSERVICE_HPP
