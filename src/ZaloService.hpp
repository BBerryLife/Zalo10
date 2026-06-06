#ifndef ZALOSERVICE_HPP
#define ZALOSERVICE_HPP

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <QSettings>
#include <QFile>
#include <QSslSocket>
#include <QStringList>
#include <QQueue>
#include <sqlite3.h>

class ZaloService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)

public:
    explicit ZaloService(QObject *parent = 0);
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }

    // Các hàm tương tác công khai từ QML Cascades
    Q_INVOKABLE void startQRLogin();
    Q_INVOKABLE void retryQRLogin();
    Q_INVOKABLE void cancelQRLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei = "", const QString &ua = "", const QString &token = "");
    Q_INVOKABLE bool loadSession();
    Q_INVOKABLE void saveSession();

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
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url);
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);
    // Gọi khi mở / đóng ChatView để biết thread đang xem
    Q_INVOKABLE void setActiveThread(const QString &threadId, bool isGroup);
    Q_INVOKABLE void clearActiveThread();
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body, const QString &threadId);
    Q_INVOKABLE void     dbSaveMessage(const QVariantMap &msg, const QString &threadId);
    Q_INVOKABLE QVariantList dbLoadMessages(const QString &threadId);

signals:
    void loggedInChanged();
    void loginFailed(const QString &message);
    void sessionExpired();          // cookies/secretKey no longer valid → must re-login
    void loginSuccess(const QString &uid, const QString &displayName);
    void qrCodeReady(const QString &imagePath, const QString &qrCode);
    void qrScanned(const QString &displayName);
    void qrExpired();
    void conversationsReady(const QVariantList &threads); // groups
    void friendsReady(const QVariantList &friends);       // 1-1 friends
    void invitesReady(const QVariantList &invites);       // friend requests
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    // Phát khi poll nhận được tin nhắn mới trong thread đang mở
    void newMessage(const QString &threadId, const QVariantMap &message);
    // Phát khi lastMessage của một thread thay đổi (để cập nhật danh sách)
    void threadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    // threadId, localFilePath (file:///tmp/...)
    void avatarReady(const QString &threadId, const QString &localPath);
    // msgId, localFilePath — for image messages downloaded for display
    void imageMsgReady(const QString &msgId, const QString &localPath);

private slots:
    // Slot xử lý luồng đăng nhập bằng QR Code
    void onStep1Done();
    void onStep2Done();
    void onStep3Done();
    void onStep4Done();
    void onStep5Done();
    void onQRImageFetched();
    void onStep6Done();
    void onStep7Done();
    void onStep8Done();
    void onStep9Done();

    // Slot xử lý luồng đăng nhập trực tiếp qua Cookie cũ
    void onCookieStep1Done();
    void onCookieStep2Done();

    // Slot xử lý dữ liệu tin nhắn và hội thoại
    void onFetchConvoDone();
    void onFetchFriendsDone();
    void onFetchInvitesDone();
    void onAcceptFriendDone();
    void onRejectFriendDone();
    void onGroupDetailsDone();
    void onFetchMsgDone();
    void onSendMsgDone();
    void onSendPhotoDone();
    void onSendFileDone();
    void onRefreshSessionKeyDone();
    void onImageMsgDownloaded();

    void onQRExpired();
    void onListenTimer();
    void onListenDone();
    void onPollMsgDone();
    void onAvatarDownloaded();

    // WebSocket (RFC 6455 over QSslSocket) — real-time messages
    void onWsConnected();
    void onWsEncrypted();
    void onWsReadyRead();
    void onWsDisconnected();
    void onWsSslErrors(const QList<QSslError> &errors);
    void onWsReconnectTimer();

private:
    // Cấu trúc đóng gói tham số mã hóa phục vụ API login
    struct EncryptedParams {
        QString enc_ver;
        QString zcid;
        QString zcid_ext;
        QString encryptKey;
        QString encryptedData;
    };

    // Khởi chạy các bước xử lý nội bộ
    void step1_loadLoginPage();
    void step2_getLoginInfo();
    void step3_verifyClient();
    void step4_generateQR();
    void step5_waitingScan();
    void step6_waitingConfirm();
    void step7_checkSession();
    void step8_getZaloLoginInfo();
    void step9_getServerInfo();

    void cookieStep1_getZaloLoginInfo();
    void cookieStep2_getServerInfo(const QString &encryptKey);

    // Tiện ích xử lý chuỗi và mạng
    EncryptedParams buildEncryptedParams(const QVariantMap &data);
    QString buildSignKey(const QString &type, const QVariantMap &params);
    QString generateIMEI();
    QString generateUUIDv4();
    QString buildRawUrl(const QString &base, const QVariantMap &params);
    QNetworkRequest buildRequest(const QString &urlStr, const QString &referer, bool jsonAccept = false);
    QString buildCookieHeader() const;
    void parseCookiesFromReply(QNetworkReply *reply);
    QByteArray buildFormBody(const QList<QPair<QString, QString> > &fields);

    // Công cụ mã hóa nội bộ (AES CBC 128 & MD5)
    QString aesEncryptHex(const QString &keyHex32, const QString &plainText);
    QString aesEncryptBase64(const QString &keyStr, const QString &plainText);
    QString aesEncryptBase64_256(const QString &keyStr, const QString &plainText); // AES-256 cho login params
    QString aesDecryptBase64_256(const QString &keyStr, const QString &cipherB64); // AES-256 cho decrypt response
    QString aesDecryptBase64(const QString &keyStr, const QString &cipherB64);
    QString md5Hex(const QString &input);
    QString md5Hex(const QByteArray &input);
    QString randomHexString(int len);

    // Quản lý kết nối và định thời
    QNetworkAccessManager *m_manager;
    QTimer *m_qrExpireTimer;
    QTimer *m_listenTimer;
    QTimer *m_wsReconnectTimer;

    // WebSocket over QSslSocket (RFC 6455) — real-time messages
    QSslSocket *m_webSocket;
    QStringList m_wsUrls;       // zpw_ws[] từ login response (dùng m_wsUrls thay m_zpwWsUrls nội bộ)
    int         m_wsUrlIndex;
    QByteArray  m_wsCipherKey;  // raw AES key bytes (từ WS cmd=1 handshake)
    bool        m_wsConnected;
    bool        m_wsHandshakeSent;
    QString     m_wsExpectedAccept; // Sec-WebSocket-Accept expected
    QByteArray  m_wsBuffer;         // buffer cho incomplete frames
    void connectWebSocket();
    void disconnectWebSocket();
    void refreshSessionKey();
    void sendWsHandshake(const QUrl &url);
    bool parseWsHandshakeResponse(const QByteArray &data, int &headerEnd);
    void handleWsFrame(int opcode, const QByteArray &payload);
    void handleWsMessage(int opcode, const QByteArray &payload);
    QByteArray maskWsFrame(int opcode, const QByteArray &data); // client→server cần mask
    void sendWsPing();                                            // cmd=2 subCmd=1 keepalive
    void sendWsRequest(int cmd, int subCmd, const QString &jsonData); // generic WS send

    // Trạng thái phiên làm việc
    QString m_userAgent;
    QString m_language;
    bool m_loggedIn;
    bool m_qrCancelled;
    bool m_isAutoRenew;  // true khi step7/step8 được gọi từ refreshSessionKey (không phải QR flow)

    // Bộ nhớ lưu trữ tạm thời thông tin tài khoản
    QMap<QString, QString> m_cookies;
    QString m_uid;
    QString m_displayName;
    QString m_secretKey; // zpw_enk dùng làm khóa mã hóa tin nhắn chat
    QString m_imei;
    QString m_loginVersion;
    QString m_qrCode;
    QString m_pendingEncryptKey;

    // URL định tuyến các cụm Server phân phối luồng dữ liệu chat của Zalo
    QString m_chatServiceUrl;
    QString m_groupServiceUrl;
    QString m_profileServiceUrl;   // zpwServiceMap.profile[0]
    QString m_groupPollServiceUrl; // zpwServiceMap.group_poll[0]
    QString m_friendServiceUrl;    // zpwServiceMap.friend[0]
    QString m_externalToken;
    QStringList m_zpwWsUrls; // zpw_ws[] — lưu session, copy sang m_wsUrls khi connect

    // Thread đang mở trong ChatView (để poll tin nhắn mới)
    QString m_activeThreadId;
    bool    m_activeThreadIsGroup;
    QString m_lastPollMsgId; // msgId cuối cùng đã biết, tránh emit trùng
    QMap<QString, QString> m_threadLastMsgId; // per-thread last msgId để fetch chính xác
    QMap<QString, QString> m_groupNames;        // groupId -> group name for notifications
    QSet<QString> m_seenMsgIds; // Tất cả msgId đã emit — dedup chắc chắn
    QQueue<QString> m_pendingDmThreadIds; // Queue các DM thread đang chờ WS cmd=510 response

    // Cache avatar: url -> localPath (file:///tmp/avatar_<md5>.jpg)
    QMap<QString, QString> m_avatarCache;
    QSet<QString> m_pendingAvatars; // Ngăn tải trùng lặp
    QMap<QString, QSet<QString> > m_pendingAvatarWaiters; // url -> set of threadIds đang chờ

    // Re-emit friendsReady sau khi avatar load xong
    sqlite3 *m_db;
    QVariantList m_pendingFriends;
    int m_pendingFriendAvatarCount;
    int m_loadedFriendAvatarCount;

    // Định nghĩa hằng số môi trường Zalo Web
    static const int API_VERSION = 671; // zca-js su dung 671 (default)
    static const int API_TYPE = 30;
    static const char *USER_AGENT;
    QString generateRandomUserAgent();
    static const char *AES_FIXED_KEY;
};

#endif // ZALOSERVICE_HPP
// NOTE: m_pendingDmThreadId added below m_seenMsgIds
