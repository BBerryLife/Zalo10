#ifndef ZALOSERVICEPROXY_HPP
#define ZALOSERVICEPROXY_HPP

// ─── ZaloServiceProxy ─────────────────────────────────────────────────────
// Drop-in replacement cho ZaloService trong UI app.
// Cùng Q_PROPERTY / Q_INVOKABLE / signals → QML không đổi gì.
//
// PRIMARY mode (production, _sys_run_headless granted):
//   Mọi lệnh được chuyển sang HeadlessService qua QLocalSocket IPC.
//
// FALLBACK mode (debug/test builds, _sys_run_headless not granted):
//   Nếu sau FALLBACK_MS mili giây headless vẫn không connect,
//   tự tạo ZaloService trực tiếp trong UI process.

#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QLocalSocket>
#include <QTimer>
#include <QMap>
#include <QByteArray>

class ZaloService; // forward-declared to avoid circular headers

class ZaloServiceProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)

public:
    explicit ZaloServiceProxy(QObject *parent = 0);
    virtual ~ZaloServiceProxy() {}

    bool loggedIn() const { return m_loggedIn; }

    Q_INVOKABLE void startQRLogin();
    Q_INVOKABLE void retryQRLogin();
    Q_INVOKABLE void cancelQRLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void loginWithCookie(const QString &zpsid, const QString &zpwSek,
                                     const QString &imei="", const QString &ua="", const QString &token="");
    Q_INVOKABLE bool         loadSession();
    Q_INVOKABLE void         saveSession();
    Q_INVOKABLE void         closeWebSocketGracefully();

    Q_INVOKABLE void fetchConversations();
    Q_INVOKABLE void fetchFriends();
    Q_INVOKABLE void fetchInvites();
    Q_INVOKABLE void acceptFriendRequest(const QString &friendId);
    Q_INVOKABLE void rejectFriendRequest(const QString &friendId);
    Q_INVOKABLE void fetchGroupDetails(const QStringList &groupIds);
    Q_INVOKABLE void fetchMessages(const QString &threadId, bool isGroup);
    Q_INVOKABLE void sendMessage(const QString &threadId, const QString &content, bool isGroup);
    Q_INVOKABLE void sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup);
    Q_INVOKABLE void sendFile(const QString &threadId, const QString &localFilePath, bool isGroup);
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId=QString());
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);
    Q_INVOKABLE void setActiveThread(const QString &threadId, bool isGroup);
    Q_INVOKABLE void clearActiveThread();
    Q_INVOKABLE void blockUser(const QString &userId);
    Q_INVOKABLE void unblockUser(const QString &userId);
    Q_INVOKABLE bool isBlocked(const QString &userId) const;
    Q_INVOKABLE void setMute(const QString &threadId, bool isGroup, bool mute);
    Q_INVOKABLE bool isMutedThread(const QString &threadId) const;
    Q_INVOKABLE void clearHistory(const QString &threadId, bool isGroup);
    Q_INVOKABLE void leaveGroup(const QString &groupId);
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body,
                                         const QString &threadId, bool isGroup=false);
    Q_INVOKABLE void         dbSaveMessage(const QVariantMap &msg, const QString &threadId);
    Q_INVOKABLE QVariantList dbLoadMessages(const QString &threadId);

    Q_INVOKABLE QVariantList getQuickMessages() const;
    Q_INVOKABLE int          addQuickMessage(const QString &name, const QString &content);
    Q_INVOKABLE bool         updateQuickMessage(int id, const QString &name, const QString &content);
    Q_INVOKABLE bool         deleteQuickMessage(int id);
    Q_INVOKABLE void         fetchServerQuickMessages();
    Q_INVOKABLE QVariantMap  getImageDimensions(const QString &localFilePath) const;
    Q_INVOKABLE QVariantMap  exportData(const QString &destDir);
    Q_INVOKABLE QVariantMap  importData(const QString &jsonFilePath);
    Q_INVOKABLE int          clearCache();

signals:
    void loggedInChanged();
    void loginFailed(const QString &message);
    void sessionExpired();
    void loginSuccess(const QString &uid, const QString &displayName);
    void sessionRefreshed();
    void qrCodeReady(const QString &imagePath, const QString &qrCode);
    void qrScanned(const QString &displayName);
    void qrExpired();
    void conversationsReady(const QVariantList &threads);
    void friendsReady(const QVariantList &friends);
    void invitesReady(const QVariantList &invites);
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    void messageRecalled(const QString &threadId, const QString &msgId);
    void threadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    void avatarReady(const QString &threadId, const QString &localPath);
    void imageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void blockUserDone(const QString &userId, bool success);
    void unblockUserDone(const QString &userId, bool success);
    void muteDone(const QString &threadId, bool muted, bool success);
    void clearHistoryDone(const QString &threadId, bool success);
    void leaveGroupDone(const QString &groupId, bool success);
    void serverQuickMessagesReady(int imported, int skipped, const QString &error);
    void headlessReadyNotLoggedIn();

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError);
    void onSocketReadyRead();
    void onReconnectTimer();
    void onFallbackTimer();          // fires if headless never connects
    void onDirectLoggedInChanged();  // syncs m_loggedIn from direct ZaloService

private:
    void     connectToHeadless();
    void     sendCall(const QString &method, const QVariantList &args);
    QVariant sendCallSync(const QString &method, const QVariantList &args, int timeoutMs=3000);
    void     handleMessage(const QVariantMap &msg);
    void     dispatchDirect(const QString &method, const QVariantList &args);
    QVariant dispatchDirectSync(const QString &method, const QVariantList &args);
    static QByteArray encode(const QVariantMap &payload);
    static bool       decodeNext(QByteArray &buf, QVariantMap &out);

    struct SyncPending { bool done; QVariant value; };

    QLocalSocket *m_socket;
    QByteArray    m_readBuf;
    QTimer       *m_reconnectTimer;
    QTimer       *m_fallbackTimer;   // cancels when headless connects; fires → direct mode
    ZaloService  *m_directService;   // non-null in fallback direct mode
    bool          m_loggedIn;
    int           m_callIdCounter;
    QMap<QString, SyncPending*> m_syncPending;
    QList<QPair<QString,QVariantList> > m_pendingCalls;
    bool m_welcomeReceived;

    static const int RECONNECT_MS = 2000;
    static const int FALLBACK_MS  = 3000; // time before giving up on headless
};

#endif // ZALOSERVICEPROXY_HPP
