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
#include <QSize>
#include <sqlite3.h>

class ZaloService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)

public:
    explicit ZaloService(QObject *parent = 0);
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }

    Q_INVOKABLE void startQRLogin();
    Q_INVOKABLE void retryQRLogin();
    Q_INVOKABLE void cancelQRLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei = "", const QString &ua = "", const QString &token = "");
    Q_INVOKABLE bool loadSession();
    Q_INVOKABLE void saveSession();
    Q_INVOKABLE void closeWebSocketGracefully(); // gửi WS Close frame "sạch" trước khi process bị kill

    Q_INVOKABLE void fetchConversations();
    Q_INVOKABLE void fetchFriends();
    Q_INVOKABLE void fetchInvites();
    Q_INVOKABLE void acceptFriendRequest(const QString &friendId);
    Q_INVOKABLE void rejectFriendRequest(const QString &friendId);
    Q_INVOKABLE void fetchGroupDetails(const QStringList &groupIds);
    Q_INVOKABLE void fetchMessages(const QString &threadId, bool isGroup);
    Q_INVOKABLE void sendMessage(const QString &threadId, const QString &content, bool isGroup);
    // Delete a message. Ported from zca-js's deleteMessage.ts:
    //   - onlyMe=true:  "delete for me" — always allowed, any thread.
    //   - onlyMe=false: "delete for everyone" — only allowed in groups; for a
    //     1-1 chat, recallMessage() (undo) is the only way to remove a message
    //     for both sides. Zalo enforces the same split server-side.
    // msgId/cliMsgId/senderId identify the target message (from the DB row);
    // senderId lets us mirror zca-js's isSelf-vs-onlyMe guard client-side too,
    // so a bad tap surfaces a clear error instead of a silent server rejection.
    Q_INVOKABLE void deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId, const QString &senderId, bool onlyMe);
    // Recall ("undo" in zca-js) a message you sent — removes it for everyone,
    // in both 1-1 chats and groups. Ported from zca-js's undo.ts.
    Q_INVOKABLE void recallMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId);
    Q_INVOKABLE void sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption = QString());
    // Copies a picker-provided (potentially transient) image path into the persistent
    // "/tmp/zalo_img_local_<ts>.<ext>" cache immediately, before upload starts, so the
    // original picked photo is never lost even if the WS echo / CDN round-trip fails.
    // Returns the new path (no "file://" prefix), or the original path if the copy fails.
    Q_INVOKABLE QString cacheLocalImage(const QString &sourcePath);
    Q_INVOKABLE void sendFile(const QString &threadId, const QString &localFilePath, bool isGroup);
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId = QString());
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);
    // Update downloader — called from AboutSheet when user confirms update.
    // Saves to /accounts/1000/shared/downloads/<filename>.
    Q_INVOKABLE void downloadUpdate(const QString &url, const QString &filename);
    // Long-press "Download" on a photo bubble: copies the already-cached local
    // file (localImage — same source as the bubble's own rendering and the
    // copy/share fixes above) into the user-visible shared downloads folder,
    // so it shows up in the device's Pictures/gallery app. No re-download from
    // the CDN — the bytes are already on disk. Returns the saved path on
    // success, or an empty string on failure (caller shows an error toast).
    Q_INVOKABLE QString downloadPhotoToGallery(const QString &localImagePath, const QString &msgId);
    // Aborts an in-flight downloadUpdate() (e.g. user tapped Cancel on the
    // SystemProgressDialog). Safe to call when nothing is downloading.
    Q_INVOKABLE void cancelUpdateDownload();


    Q_INVOKABLE void setActiveThread(const QString &threadId, bool isGroup);
    Q_INVOKABLE void clearActiveThread();
    Q_INVOKABLE void blockUser(const QString &userId);
    Q_INVOKABLE void unblockUser(const QString &userId);
    Q_INVOKABLE bool isBlocked(const QString &userId) const { return m_blockedUsers.contains(userId); }
    Q_INVOKABLE void setMute(const QString &threadId, bool isGroup, bool mute);
    Q_INVOKABLE bool isMutedThread(const QString &threadId) const { return m_mutedThreads.contains(threadId); }
    Q_INVOKABLE void clearHistory(const QString &threadId, bool isGroup);
    Q_INVOKABLE void leaveGroup(const QString &groupId);
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    Q_INVOKABLE void     dbSaveMessage(const QVariantMap &msg, const QString &threadId);
    Q_INVOKABLE QVariantList dbLoadMessages(const QString &threadId);
    // Returns, for every thread that has at least one locally-stored message,
    // the most recent one: {threadId: {content, dName, isMine, msgType,
    // recalledOriginalContent, ts}}. Used by ChatsTab.qml/GroupsTab.qml right
    // after onFriendsReady/onConversationsReady to restore the chat list's
    // "last message" preview + time from local history immediately on app
    // launch, instead of showing "No messages yet" until a new message
    // happens to arrive over the network during that session.
    Q_INVOKABLE QVariantMap getThreadLastMessages() const;

    // Quick Messages ("/command" canned replies) — stored locally in SQLite,
    // shared across every conversation. Replaces the old "Timed Messages"
    // placeholder feature.
    Q_INVOKABLE QVariantList getQuickMessages() const;                                        // [{id,name,content}], sorted A-Z by name
    Q_INVOKABLE int          addQuickMessage(const QString &name, const QString &content);     // returns new id, or -1 on failure/duplicate name
    Q_INVOKABLE bool         updateQuickMessage(int id, const QString &name, const QString &content); // false on failure/duplicate name
    Q_INVOKABLE bool         deleteQuickMessage(int id);
    // Pulls the user's own "Tin nhắn nhanh" (quick message) list straight from their
    // real Zalo account via api/quickmessage/list, then merges it into the local
    // quick_messages table — same "match by name, existing wins" rule as importData(),
    // so it's safe to call repeatedly without creating duplicates.
    Q_INVOKABLE void         fetchServerQuickMessages();
    // Returns {"width": w, "height": h} for a local image file (accepts "file://" prefix).
    // Used by ChatView.qml right after the user picks a photo to send, so the outgoing
    // bubble can be sized to the real aspect ratio immediately (before upload finishes).
    Q_INVOKABLE QVariantMap getImageDimensions(const QString &localFilePath) const;
    Q_INVOKABLE qint64 getFileSize(const QString &localFilePath) const;

    // ---- Data export / import / cache management (Settings) -----------------
    // Runs on the UI thread but is kept fast: SQLite reads/writes and one small
    // file write, so a single SystemProgressToast with indefinite progress (no
    // QThread) is enough to keep the UI from looking frozen.
    //
    // exportData: dumps every locally-known message + quick message into a single
    // JSON file under <destDir>/zalo10/zalo10_data_<timestamp>.json. Text only —
    // image files are never copied (they live in a tmp cache that doesn't survive
    // an app update/reinstall anyway). A message that had a photo keeps its text
    // content (if any) with a "[Photo]" marker appended so the conversation still
    // reads naturally; no image data or dimensions are included.
    // Returns a map: {"success": bool, "path": exported json path,
    // "messageCount": n, "error": msg}
    Q_INVOKABLE QVariantMap exportData(const QString &destDir);

    // importData: reads back a JSON file produced by exportData(). Messages whose
    // msgId already exists locally are skipped (existing data always wins) so importing
    // is always safe to re-run. Quick messages are matched by name (case-insensitive)
    // and skipped the same way.
    // Returns: {"success": bool, "importedMessages": n, "skippedMessages": n,
    //           "importedQuickMessages": n, "skippedQuickMessages": n, "error": msg}
    Q_INVOKABLE QVariantMap importData(const QString &jsonFilePath);

    // clearCache: deletes every cached image file Zalo10 writes to the temp folder
    // (avatars, message photo thumbnails/full images, QR codes) AND wipes the local
    // message history table (cleared_threads + quick_messages are left intact —
    // those aren't "cache", they're user data/settings). Conversations themselves
    // are unaffected server-side and will simply be re-fetched next time they're
    // opened. Returns the number of files deleted.
    Q_INVOKABLE int clearCache();

signals:
    void loggedInChanged();
    void loginFailed(const QString &message);
    void sessionExpired();          // cookies/secretKey no longer valid → must re-login
    void loginSuccess(const QString &uid, const QString &displayName);
    void sessionRefreshed();   // secretKey renewed — không cần re-fetch toàn bộ
    void qrCodeReady(const QString &imagePath, const QString &qrCode);
    void qrScanned(const QString &displayName);
    void qrExpired();
    void conversationsReady(const QVariantList &threads); // groups
    void friendsReady(const QVariantList &friends);       // 1-1 friends
    void invitesReady(const QVariantList &invites);       // friend requests
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    // Fired when a previously-displayed message gets recalled/unsent by its sender
    // (Zalo "chat.undo" event). QML should update the existing bubble in place.
    void messageRecalled(const QString &threadId, const QString &msgId);
    // Fired when OUR OWN "delete for me" is confirmed via the chat.delete WS
    // notification (see extractDeleteInfo() in ZaloServiceUtils.hpp) and the
    // local DB row has been hard-deleted. Unlike messageRecalled, this means
    // "remove this bubble from the model entirely" — no placeholder text.
    // Only ever fires for deletions WE performed; another participant's
    // "delete for me" must never reach this signal or affect our screen.
    void messageDeletedLocally(const QString &threadId, const QString &msgId);
    // Result of OUR OWN deleteMessage()/recallMessage() calls (as opposed to
    // messageRecalled above, which is the incoming notification when someone
    // else's recall reaches us over WS). QML uses these to update the bubble
    // immediately without waiting for a WS echo, and to show an error toast
    // on failure (e.g. tried to delete-for-everyone in a 1-1 chat).
    void messageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void messageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void threadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    // threadId, localFilePath (file:///tmp/...)
    void avatarReady(const QString &threadId, const QString &localPath);
    // msgId, localFilePath — for image messages downloaded for display
    // width/height: actual pixel dimensions of the image (0 if unknown) — used by
    // ChatView.qml to size the bubble to the real aspect ratio instead of a fixed square.
    void imageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void blockUserDone(const QString &userId, bool success);
    void unblockUserDone(const QString &userId, bool success);
    void muteDone(const QString &threadId, bool muted, bool success);
    void clearHistoryDone(const QString &threadId, bool success);
    void leaveGroupDone(const QString &groupId, bool success);
    // imported/skipped quick messages pulled from the server, or error non-empty on failure
    void serverQuickMessagesReady(int imported, int skipped, const QString &error);
    // Update download progress (0-100), finished (localPath), or failed (errorMsg)
    void updateDownloadProgress(int percent);
    void updateDownloadFinished(const QString &localPath);
    void updateDownloadFailed(const QString &errorMsg);

private slots:
    void onStep1Done();
    void onUpdateDownloadProgress(qint64 received, qint64 total);
    void onUpdateDownloadFinished();
    void onUpdateSslErrors(const QList<QSslError> &errors);

    void onStep2Done();
    void onStep3Done();
    void onStep4Done();
    void onStep5Done();
    void onQRImageFetched();
    void onStep6Done();
    void onStep7Done();
    void onStep8Done();
    void onStep9Done();

    void onCookieStep1Done();
    void onCookieStep2Done();

    void onFetchConvoDone();
    void onFetchFriendsDone();
    void onFetchServerQuickMessagesDone();
    void onFetchInvitesDone();
    void onAcceptFriendDone();
    void onRejectFriendDone();
    void onGroupDetailsDone();
    void onFetchMsgDone();
    void onFetchPhotoDetailDone();  // HTTP fallback khi cmd=510 không trả HTTP URL
    void onSendMsgDone();
    void onDeleteMsgDone();
    void onRecallMsgDone();
    void onSendPhotoDone();
    void onSendPhotoMsgDone();
    void onSendFileDone();
    void onRefreshSessionKeyDone();
    void onImageMsgDownloaded();
    void onBlockUserDone();
    void onUnblockUserDone();
    void onSetMuteDone();
    void onClearHistoryDone();
    void onLeaveGroupDone();

    void onQRExpired();
    void onListenTimer();
    void onListenDone();
    void onPollMsgDone();
    void onAvatarDownloaded();
    void onKeepAliveTimer();
    void onKeepAliveDone();

    // WebSocket (RFC 6455 over QSslSocket) — real-time messages
    void onWsConnected();
    void onWsEncrypted();
    void onWsReadyRead();
    void onWsDisconnected();
    void onWsSslErrors(const QList<QSslError> &errors);
    void onWsSocketError(QAbstractSocket::SocketError err);
    void onWsReconnectTimer();

private:
    struct EncryptedParams {
        QString enc_ver;
        QString zcid;
        QString zcid_ext;
        QString encryptKey;
        QString encryptedData;
    };

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

    void fetchPhotoViaWs510(const QString &msgId, const QString &threadId);
    void fetchPhotoViaHttp(const QString &msgId, const QString &threadId);
    void fetchPhotoViaHttpAtIndex(const QString &msgId, const QString &threadId, int idx);
    QSize imageDimensions(const QString &localFileUrlOrPath) const; // strips "file://", reads pixel size
    void markMessageRecalled(const QString &threadId, const QString &msgId); // chat.undo handling
    void markMessageDeletedForMe(const QString &threadId, const QString &msgId); // chat.delete handling — local-only, hard delete

    // Data export/import/cache helpers
    QVariantList dbLoadAllMessages() const;     // every row, every thread — used by exportData
    QStringList  cacheFilePatterns() const;     // filename prefixes this app writes under tempPath()

    // Persistent avatar cache (avatar_meta table) — see ZaloService_Db.cpp.
    // Lets downloadAvatar() recognise "we already have this exact avatar on
    // disk" across app restarts and logout/login, and only re-fetch when the
    // person's avatar URL actually changed (or the cached file is gone).
    void    loadAvatarCacheFromDb();                              // startup diagnostic: logs avatar_meta vs files on disk
    bool    avatarMetaLookup(const QString &threadId, QString &urlHashOut, QString &localPathOut) const;
    void    avatarMetaUpsert(const QString &threadId, const QString &urlHash, const QString &localPath);

    EncryptedParams buildEncryptedParams(const QVariantMap &data);
    QString buildSignKey(const QString &type, const QVariantMap &params);
    QString generateIMEI();
    QString generateUUIDv4();
    QString buildRawUrl(const QString &base, const QVariantMap &params);
    QNetworkRequest buildRequest(const QString &urlStr, const QString &referer, bool jsonAccept = false);
    QString buildCookieHeader() const;
    void parseCookiesFromReply(QNetworkReply *reply);
    QByteArray buildFormBody(const QList<QPair<QString, QString> > &fields);

    QString aesEncryptHex(const QString &keyHex32, const QString &plainText);
    QString aesEncryptBase64(const QString &keyStr, const QString &plainText);
    QString aesEncryptBase64_256(const QString &keyStr, const QString &plainText); // AES-256 cho login params
    QString aesDecryptBase64_256(const QString &keyStr, const QString &cipherB64); // AES-256 cho decrypt response
    QString aesDecryptBase64(const QString &keyStr, const QString &cipherB64);
    QString md5Hex(const QString &input);
    QString md5Hex(const QByteArray &input);
    QString randomHexString(int len);

    QNetworkAccessManager *m_manager;
    QTimer *m_qrExpireTimer;
    QTimer *m_listenTimer;
    QTimer *m_wsReconnectTimer;
    QTimer *m_keepAliveTimer; // gọi /keepalive định kỳ để gia hạn session (issue zca-js #keepalive)

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
    void sendKeepAlive();   // GET {chat}/keepalive — gia hạn session, port từ zca-js keepAliveFactory
    void sendWsHandshake(const QUrl &url);
    bool parseWsHandshakeResponse(const QByteArray &data, int &headerEnd);
    void handleWsFrame(int opcode, const QByteArray &payload);
    void handleWsMessage(int opcode, const QByteArray &payload);
    QByteArray maskWsFrame(int opcode, const QByteArray &data); // client→server cần mask
    void sendWsPing();                                            // cmd=2 subCmd=1 keepalive
    void sendWsRequest(int cmd, int subCmd, const QString &jsonData); // generic WS send

    QString m_userAgent;
    QString m_language;
    bool m_loggedIn;
    bool m_qrCancelled;
    bool m_isAutoRenew;
    bool m_isFetchingFriends;
    bool m_isFetchingConversations;  // true khi step7/step8 được gọi từ refreshSessionKey (không phải QR flow)
    bool m_loginEmitted;             // true sau khi loginSuccess đã emit lần đầu — ngăn emit lại từ refreshSessionKey
    qint64 m_lastFetchFriendsTime;      // epoch-ms của lần fetch thành công gần nhất
    qint64 m_lastFetchConvoTime;        // epoch-ms của lần fetch thành công gần nhất
    static const int FETCH_COOLDOWN_MS = 10000; // 10 giây cooldown giữa 2 lần fetch

    QMap<QString, QString> m_cookies;
    QString m_uid;
    QString m_displayName;
    QString m_secretKey; // zpw_enk dùng làm khóa mã hóa tin nhắn chat
    QString m_imei;
    QString m_loginVersion;
    QString m_qrCode;
    QString m_pendingEncryptKey;

    QString m_chatServiceUrl;
    QString m_groupServiceUrl;
    QString m_profileServiceUrl;   // zpwServiceMap.profile[0]
    QString m_groupPollServiceUrl; // zpwServiceMap.group_poll[0]
    QString m_friendServiceUrl;    // zpwServiceMap.friend[0]
    QString m_fileServiceUrl;      // zpwServiceMap.file[0]
    QString m_quickMessageServiceUrl; // zpwServiceMap.quick_message[0]

    QSet<QString> m_mutedThreads;  // threadIds currently muted
    QSet<QString> m_blockedUsers;  // userIds currently blocked
    QString m_externalToken;
    QStringList m_zpwWsUrls; // zpw_ws[] — lưu session, copy sang m_wsUrls khi connect

    QString m_activeThreadId;
    bool    m_activeThreadIsGroup;
    QString m_lastPollMsgId; // msgId cuối cùng đã biết, tránh emit trùng
    QMap<QString, QString> m_threadLastMsgId; // per-thread last msgId để fetch chính xác
    QMap<QString, QString> m_groupNames;        // groupId -> group name for notifications
    QSet<QString> m_seenMsgIds; // Tất cả msgId đã emit — dedup chắc chắn
    QString m_pending510Toid; // Thread đang chờ WS cmd=510 response (chỉ 1 tại 1 thời điểm)
    QMap<QString, QString> m_pendingPhotoMsgIds; // msgId -> threadId, waiting for photo URL via WS cmd=510
    // clientId (cliMsgId) -> {"localPath","fileSize","fileName"} for a photo we just sent.
    // Populated in sendPhoto() before the upload even starts, so that when the WS cmd=501
    // echo lands (often BEFORE the HTTP send-msg response, per device logs) we can attach
    // the already-cached local file directly instead of racing a CDN re-download that can
    // return empty/fail moments after upload (this is what caused "my sent photo" to turn
    // into a gray box after logout/login). Entry is removed once consumed by either the
    // WS echo path or the HTTP send-msg confirm path, whichever resolves the real msgId first.
    QMap<QString, QVariantMap> m_pendingSentPhotoInfo;

    // Cache avatar: url -> localPath (file:///tmp/avatar_<md5>.jpg)
    QMap<QString, QString> m_avatarCache;
    QSet<QString> m_pendingAvatars; // Ngăn tải trùng lặp
    QMap<QString, QSet<QString> > m_pendingAvatarWaiters; // url -> set of threadIds đang chờ

    // Re-emit friendsReady sau khi avatar load xong
    sqlite3 *m_db;
    QVariantList m_pendingFriends;
    int m_pendingFriendAvatarCount;
    int m_loadedFriendAvatarCount;

    QNetworkReply *m_updateReply;
    QString        m_updateDestPath;

    static const int API_VERSION = 671; // zca-js su dung 671 (default)
    static const int API_TYPE = 30;
    static const int KEEPALIVE_INTERVAL_MS = 120000; // 2 phut, theo goi y trong issue zca-js
    static const char *USER_AGENT;
    QString generateRandomUserAgent();
    static const char *AES_FIXED_KEY;
};

#endif // ZALOSERVICE_HPP
