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
#include <QTcpSocket>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <QStringList>
#include <QSize>
#include <sqlite3.h>

// Forward-declared: the full ContactPicker header pulls heavy dependencies; it is included
// only in ZaloService_ContactPicker.cpp.
namespace bb { namespace cascades { namespace pickers { class ContactPicker; } } }

class HubIntegration;

struct PhoneSyncRuntime;

class ZaloService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(QString selfUid READ selfUid CONSTANT)

public:
    explicit ZaloService(QObject *parent = 0);
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }
    QString selfUid() const { return m_uid; }
    Q_INVOKABLE QString memberDisplayName(const QString &uid) const { return m_memberNames.value(uid, QString()); }

    Q_INVOKABLE void startQRLogin();
    Q_INVOKABLE void retryQRLogin();
    Q_INVOKABLE void cancelQRLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei = "", const QString &ua = "", const QString &token = "");
    Q_INVOKABLE bool loadSession();
    Q_INVOKABLE void saveSession();
    Q_INVOKABLE void closeWebSocketGracefully();

    Q_INVOKABLE void fetchConversations();
    Q_INVOKABLE void fetchFriends();
    Q_INVOKABLE void fetchInvites();
    Q_INVOKABLE void acceptFriendRequest(const QString &friendId);
    Q_INVOKABLE void rejectFriendRequest(const QString &friendId);
    Q_INVOKABLE void fetchGroupDetails(const QStringList &groupIds);
    Q_INVOKABLE void fetchGroupBoard(const QString &groupId, int page, int count);
    // Group only (Zalo has no pin API for 1-1). Same endpoint as createNote with type:2
    // (PinnedMessage); msgType is Zalo's type code (1=text, 32=photo...).
    Q_INVOKABLE void pinGroupMessage(const QString &groupId, const QString &msgId,
                                      const QString &cliMsgId, const QString &senderId,
                                      const QString &senderName, const QString &content,
                                      int msgType);
    Q_INVOKABLE void createGroupNote(const QString &groupId, const QString &title, bool pinAct);
    // Polls use the plain "group" service URL, not the group board one. Zalo requires 2+ options.
    Q_INVOKABLE void createGroupPoll(const QString &groupId, const QString &question,
                                      const QStringList &optionsList, bool allowMultiChoices,
                                      bool allowAddNewOption, bool hideVotePreview,
                                      bool isAnonymous);
    // optionIds is a QVariantList on purpose: Qt4 QML can't turn a JS array into QList<int> (it
    // arrives empty, which voted for nothing). Empty list clears the vote. groupId only routes
    // the Hub notification.
    Q_INVOKABLE void voteGroupPoll(const QString &groupId, const QString &pollId, const QVariantList &optionIds);
    Q_INVOKABLE void getPollDetail(const QString &pollId);
    Q_INVOKABLE void fetchMessages(const QString &threadId, bool isGroup);
    Q_INVOKABLE void sendMessage(const QString &threadId, const QString &content, bool isGroup);
    Q_INVOKABLE void sendMessageQuote(const QString &threadId, const QString &content, bool isGroup,
                                       const QString &quoteMsgId, const QString &quoteCliMsgId,
                                       const QString &quoteOwnerId, const QString &quoteContent,
                                       int quoteMsgType, const QString &quoteTs,
                                       const QString &quoteSenderName);
    // origMsgId/origTs are the ORIGINAL message's ids; the server uses them to mark it as forwarded.
    // Posts to the file service. Targets must be all groups or all 1-1.
    Q_INVOKABLE void forwardMessage(const QString &content, const QStringList &threadIds, bool isGroup,
                                     const QString &origMsgId, const QString &origTs);
    // onlyMe=true: delete for me (any thread). onlyMe=false: delete for everyone (groups only;
    // 1-1 uses recallMessage()). senderId is for the client-side permission check.
    Q_INVOKABLE void deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId, const QString &senderId, bool onlyMe);
    Q_INVOKABLE void recallMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId);
    Q_INVOKABLE void reactMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                   const QString &cliMsgId, int msgType, int rType, const QString &icon);
    Q_INVOKABLE QVariantMap dbLoadThreadReactions(const QString &threadId);
    Q_INVOKABLE QVariantMap getThreadMessageStates(const QString &threadId);
    Q_INVOKABLE void sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption = QString());
    Q_INVOKABLE QString cacheLocalImage(const QString &sourcePath);
    Q_INVOKABLE void sendFile(const QString &threadId, const QString &localFilePath, bool isGroup);
    Q_INVOKABLE void deleteLocalFile(const QString &path);
    // Opens the ContactPicker and builds a hand-written VCF 3.0 (the BB10 SDK has no vCard export),
    // then emits contactVcfReady(threadId, path) or contactPickError.
    // threadId is required in both signals: every live ChatView page listens to zService, and
    // without it each page would send the file (this caused triple .vcf sends).
    Q_INVOKABLE void pickContact(const QString &threadId);
    Q_INVOKABLE void sendVideo(const QString &threadId, const QString &localFilePath, bool isGroup);
    void handleFileUploadDone(const QString &fileId, const QString &fileUrl);
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId = QString());
    Q_INVOKABLE void downloadVideoMessage(const QString &msgId, const QString &url, const QString &fileName);
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);
    // Sticker images never change per id, so they are cached forever. stickerId is a QString:
    // a qint64 Q_INVOKABLE parameter caused SIGSEGV when QML numbers were marshalled in Qt 4.8.
    Q_INVOKABLE void downloadSticker(const QString &stickerId);
    Q_INVOKABLE void downloadUpdate(const QString &url, const QString &filename);
    Q_INVOKABLE QString downloadPhotoToGallery(const QString &localImagePath, const QString &msgId);
    Q_INVOKABLE void cancelUpdateDownload();

    Q_INVOKABLE void setActiveThread(const QString &threadId, bool isGroup);
    Q_INVOKABLE void clearActiveThread();
    Q_INVOKABLE QStringList unreadThreadIds() const;
    Q_INVOKABLE void setThreadUnread(const QString &threadId, bool unread);
    Q_INVOKABLE bool isActiveThread(const QString &threadId) const
        { return !threadId.isEmpty() && m_activeThreadId == threadId; }
    Q_INVOKABLE void blockUser(const QString &userId);
    Q_INVOKABLE void unblockUser(const QString &userId);
    Q_INVOKABLE bool isBlocked(const QString &userId) const { return m_blockedUsers.contains(userId); }
    Q_INVOKABLE void setMute(const QString &threadId, bool isGroup, bool mute);
    Q_INVOKABLE bool isMutedThread(const QString &threadId) const { return m_mutedThreads.contains(threadId); }
    Q_INVOKABLE void clearHistory(const QString &threadId, bool isGroup);
    Q_INVOKABLE void leaveGroup(const QString &groupId);
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    // Banner shown even while the app is in the foreground (Hub banners only show when inactive).
    Q_INVOKABLE void sendBannerNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    void setAppForeground(bool foreground) { m_appForeground = foreground; }

    // UI <-> headless service handoff: Zalo allows one WebSocket per account, so only one process
    // holds realtime at a time. The UI process never calls these.
    void suspendRealtime();
    bool resumeRealtime();
    bool realtimeSuspended() const { return m_realtimeSuspended; }
    Q_INVOKABLE bool isGroupHubThread(const QString &threadId) const;
    bool handleHubAction(const QString &action, const QString &threadId);
    Q_INVOKABLE void     dbSaveMessage(const QVariantMap &msg, const QString &threadId);
    Q_INVOKABLE QVariantList dbLoadMessages(const QString &threadId);
    Q_INVOKABLE QVariantList dbLoadMessagesPage(const QString &threadId, const QString &beforeTs, int limit);
    Q_INVOKABLE QStringList pinnedThreadIds() const;
    Q_INVOKABLE void hideLocalMessage(const QString &threadId, const QString &msgId);
    Q_INVOKABLE bool isLocallyHidden(const QString &msgId) const;
    Q_INVOKABLE void fetchPinConversations();
    Q_INVOKABLE void startHistorySync();
    Q_INVOKABLE bool isHistorySyncing() const { return m_sync.active || m_sync.groupActive; }
    Q_INVOKABLE void startPhoneSync();
    Q_INVOKABLE void cancelPhoneSync();
    Q_INVOKABLE bool isPhoneSyncing() const;
    Q_INVOKABLE QVariantMap getThreadLastMessages() const;

    Q_INVOKABLE QVariantList getQuickMessages() const;
    Q_INVOKABLE int          addQuickMessage(const QString &name, const QString &content);
    Q_INVOKABLE bool         updateQuickMessage(int id, const QString &name, const QString &content);
    Q_INVOKABLE bool         deleteQuickMessage(int id);
    Q_INVOKABLE void         fetchServerQuickMessages();
    Q_INVOKABLE QVariantMap getImageDimensions(const QString &localFilePath) const;
    Q_INVOKABLE qint64 getFileSize(const QString &localFilePath) const;

    Q_INVOKABLE QVariantMap exportData(const QString &destDir);

    Q_INVOKABLE QVariantMap importData(const QString &jsonFilePath);

    Q_INVOKABLE int clearCache();

signals:
    void loggedInChanged();
    void loginFailed(const QString &message);
    void sessionExpired();  // Session cookies/secretKey are no longer valid: re-login required.
    void loginSuccess(const QString &uid, const QString &displayName);
    void sessionRefreshed();
    void qrCodeReady(const QString &imagePath, const QString &qrCode);
    void qrScanned(const QString &displayName);
    void qrExpired();
    void conversationsReady(const QVariantList &threads);
    void friendsReady(const QVariantList &friends);
    void pinnedChanged();
    void historySynced(int messages);
    void phoneSyncProgress(int percent, const QString &text);
    void phoneSyncFinished(bool ok, const QString &message);
    void invitesReady(const QVariantList &invites);
    void groupBoardReady(const QString &groupId, const QVariantList &items, const QString &error);
    void pinMessageDone(bool success, const QString &error);
    void createNoteDone(bool success, const QString &error);
    void createPollDone(bool success, const QString &error);
    void votePollDone(bool success, const QString &pollId, const QVariantList &updatedOptions, const QString &error);
    void pollDetailReady(const QString &pollId, const QVariantMap &detail, const QString &error);
    void boardEventOccurred(const QString &groupId, const QString &act, const QString &actorName,
                             bool isSelf, int topicType, const QString &topicId, const QString &title);
    void forwardMessageDone(bool success, int successCount, int failCount, const QString &error);
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    void threadRead(const QString &threadId);
    void threadMarkedUnread(const QString &threadId);
    void messageTsCorrected(const QString &threadId, const QString &msgId, const QString &newTs);
    void messageRecalled(const QString &threadId, const QString &msgId);
    void messageDeletedLocally(const QString &threadId, const QString &msgId);
    void reactionUpdated(const QString &threadId, const QString &msgId, const QString &uid, const QString &icon);
    void messageStatesChanged(const QString &threadId);
    void reactMessageDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void messageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void messageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void threadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    void avatarReady(const QString &threadId, const QString &localPath);
    void stickerReady(const QString &stickerId, const QString &localPath);
    void imageMsgReady(const QString &msgId, const QString &localPath, int width, int height);
    void blockUserDone(const QString &userId, bool success);
    void unblockUserDone(const QString &userId, bool success);
    void muteDone(const QString &threadId, bool muted, bool success);
    void clearHistoryDone(const QString &threadId, bool success);
    void leaveGroupDone(const QString &groupId, bool success);
    void serverQuickMessagesReady(int imported, int skipped, const QString &error);
    void updateDownloadProgress(int percent);
    void updateDownloadFinished(const QString &localPath);
    void updateDownloadFailed(const QString &errorMsg);
    void videoDownloadProgress(const QString &msgId, int percent);
    void videoDownloadFinished(const QString &msgId, const QString &localPath);
    void videoDownloadFailed(const QString &msgId, const QString &errorMsg);
    void videoUploadProgress(const QString &threadId, int percent);
    void fileUploadProgress(const QString &threadId, int percent);
    // Exactly one of the two signals is emitted per pickContact(); path has no file:// prefix.
    void contactVcfReady(const QString &threadId, const QString &path);
    void contactPickError(const QString &threadId, const QString &reason);

private slots:
    void onPinConversationsDone();
    void onPhoneSyncRsaDone();
    void onPhoneSyncPullDone();
    void onPhoneSyncDownloadDone();
    void onPhoneSyncRawDone();
    void onPhoneSyncRetryDownload();
    void onPhoneSyncDownloadProgress(qint64 got, qint64 total);
    void onPhoneSyncDecodeDone();
    void onPhoneSyncTick();
    void onPhoneSyncTimeout();
    void onSyncStartDeferred();
    void onSyncWatchdog();
    void onSyncNextPage();
    void onSyncGroupNext();
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
    void retryRefreshSessionKey();
    void onStep9Done();

    void onCookieStep1Done();
    void onCookieStep2Done();

    void onFetchConvoDone();
    void onFetchFriendsDone();
    void onFetchServerQuickMessagesDone();
    void onFetchInvitesDone();
    void onFetchGroupBoardDone();
    void onPinGroupMessageDone();
    void onCreateGroupNoteDone();
    void onCreateGroupPollDone();
    void onVoteGroupPollDone();
    void onGetPollDetailDone();
    void onForwardMessageDone();
    void onAcceptFriendDone();
    void onRejectFriendDone();
    void onGroupDetailsDone();
    void onFetchMsgDone();
    void onFetchPhotoDetailDone();
    void onSendMsgDone();
    void onSendMsgQuoteDone();
    void onDeleteMsgDone();
    void onRecallMsgDone();
    void onReactMsgDone();
    void onSendPhotoDone();
    void onSendPhotoMsgDone();
    void onSendVideoChunkUploadDone();
    void onSendVideoMsgDone();
    // ContactPicker emits exactly one of canceled/error/contactSelected per open() (single-select).
    void onContactPickerCanceled();
    void onContactPickerError();
    void onContactPickerContactSelected(int contactId);
    void onVideoDownloadProgress(qint64 received, qint64 total);
    void onVideoDownloadFinished();
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
    void onStickerDownloaded();
    void onKeepAliveTimer();
    void onKeepAliveDone();

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
    void dbSaveReaction(const QString &threadId, const QString &msgId, const QString &uid, const QString &icon);
    void dbRemoveReaction(const QString &msgId, const QString &uid);
    // Raises outgoing messages up to the ts of msgId/altMsgId to at least `state`; never lowers.
    // Returns true if a row changed.
    bool dbAdvanceMessageStates(const QString &threadId, int state,
                                const QString &msgId, const QString &altMsgId);
    QSize imageDimensions(const QString &localFileUrlOrPath) const;
    void markMessageRecalled(const QString &threadId, const QString &msgId);
    void markMessageDeletedForMe(const QString &threadId, const QString &msgId);
    bool isMessageDeletedForMe(const QString &msgId) const;

    QVariantList dbLoadAllMessages() const;
    QStringList  cacheFilePatterns() const;

    void    loadAvatarCacheFromDb();
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
    QString aesEncryptBase64_256(const QString &keyStr, const QString &plainText);
    QString aesDecryptBase64_256(const QString &keyStr, const QString &cipherB64);
    QString aesDecryptBase64(const QString &keyStr, const QString &cipherB64);
    QString md5Hex(const QString &input);
    QString md5Hex(const QByteArray &input);
    QString randomHexString(int len);

    QNetworkAccessManager *m_manager;
    QTimer *m_qrExpireTimer;
    QTimer *m_listenTimer;
    QTimer *m_wsReconnectTimer;
    QTimer *m_keepAliveTimer;

    // WebSocket TLS is built by hand: QSslSocket on BB10 always fails the handshake (error:1407742E)
    // and Qt 4.8's QSsl has no TlsV1_1/1_2. m_webSocket is a plain QTcpSocket with TLS run through
    // the OpenSSL C API (TLSv1_2_client_method()). See wsTlsHandshakeStep()/wsWriteRaw()/wsReadAvailable().
    QTcpSocket *m_webSocket;
    SSL_CTX    *m_wsSslCtx;
    SSL        *m_wsSsl;
    bool        m_wsUseSsl;
    bool        m_wsTlsEstablished;
    QStringList m_wsUrls;
    int         m_wsUrlIndex;
    // Set when the connection drops before the WS handshake succeeds (e.g. SSL failure): the next
    // reconnect should try another host from m_wsUrls.
    bool        m_wsAdvanceUrlOnReconnect;
    // Consecutive failed reconnects (reset on successful upgrade); drives a growing backoff so cycling
    // the zpw_ws pool doesn't cause bursts of failed TLS handshakes.
    int         m_wsConsecutiveFailCount;
    QByteArray  m_wsCipherKey;
    bool        m_wsConnected;
    bool        m_wsHandshakeSent;
    QString     m_wsExpectedAccept;
    QByteArray  m_wsBuffer;
    void connectWebSocket();
    void disconnectWebSocket();
    void refreshSessionKey();
    void sendKeepAlive();
    void sendWsHandshake(const QUrl &url);
    bool parseWsHandshakeResponse(const QByteArray &data, int &headerEnd);
    void handleWsFrame(int opcode, const QByteArray &payload);
    void handleWsMessage(int opcode, const QByteArray &payload);
    QVariantMap decodeWsEnvelope(const QVariantMap &outer, const QString &debugTag);
    QByteArray maskWsFrame(int opcode, const QByteArray &data);
    bool applyRefreshSessionResponseNative(const QByteArray &raw);
    int  wsNextReconnectDelayMs();
    bool wsTlsHandshakeStep();
    void wsPumpTlsOutput();
    qint64 wsWriteRaw(const QByteArray &data);
    QByteArray wsReadDecrypted();
    void sendWsPing();
    void sendWsRequest(int cmd, int subCmd, const QString &jsonData);

    QString m_userAgent;
    QString m_language;
    bool m_loggedIn;
    bool m_qrCancelled;
    bool m_isAutoRenew;
    int  m_renewRetry;
    bool m_isFetchingFriends;
    bool m_isFetchingConversations;  // true when step7/step8 run from refreshSessionKey (not the QR flow)
    bool m_loginEmitted;
    qint64 m_lastFetchFriendsTime;
    qint64 m_lastFetchConvoTime;
    static const int FETCH_COOLDOWN_MS = 10000;

    QMap<QString, QString> m_cookies;
    QString m_uid;
    QString m_displayName;
    QString m_secretKey;  // zpw_enk, the message encryption key
    QString m_imei;
    QString m_loginVersion;
    QString m_qrCode;
    QString m_pendingEncryptKey;

    QString m_chatServiceUrl;
    QString m_groupServiceUrl;
    QString m_profileServiceUrl;
    QString m_groupPollServiceUrl;
    QString m_groupBoardServiceUrl;
                                     // Group board service (/api/board/list, /api/board/topic/createv2|updatev2).
                                     // Poll actions use zpwServiceMap.group (m_groupServiceUrl) instead.
    QString m_friendServiceUrl;
    QString m_fileServiceUrl;
    QString m_quickMessageServiceUrl;
    QString m_reactionServiceUrl;
                                    // Reactions use zpwServiceMap.reaction[0], not the chat/group hosts (they 404 on
                                    // /api/message/reaction), as in zca-js addReactionFactory().

    QSet<QString> m_mutedThreads;
    QSet<QString> m_blockedUsers;
    QString m_externalToken;
    QStringList m_zpwWsUrls;

    QString m_activeThreadId;
    bool    m_activeThreadIsGroup;
    bool    m_appForeground;
    bool    m_realtimeSuspended;
    HubIntegration *m_hub;
    QString m_lastPollMsgId;
    QMap<QString, QString> m_threadLastMsgId;
    QMap<QString, QString> m_groupNames;
    // uid -> display name from each group's "currentMems"; the wire "dName" is unreliable for
    // incoming group messages (it sometimes carries our own name).
    QMap<QString, QString> m_memberNames;
    QSet<QString> m_seenMsgIds;
    QString m_pending510Toid;

    struct HistorySyncState {
        HistorySyncState()
            : active(false), awaiting(false), pages(0), totalMsgs(0), retries(0),
              groupActive(false), groupRan(false), groupDone(0), groupMsgs(0),
              lastStartMs(0), watchdog(0) {}
        bool    active;
        bool    awaiting;
        int     pages;
        int     totalMsgs;
        int     retries;
        QString cursor;
        QSet<QString> threads;
        bool    groupActive;
        bool    groupRan;
        int     groupDone;
        int     groupMsgs;
        QStringList groupQueue;
        qint64  lastStartMs;
        QTimer *watchdog;
    };
    HistorySyncState m_sync;

    struct PsHandle { PsHandle() : p(0) {} PhoneSyncRuntime *p; };
    PsHandle m_psh;
    void handlePhoneSyncEvent(const QString &act, const QVariantMap &data);
    void phoneSyncBeginDownload(const QVariantMap &d);
    void phoneSyncStartDownload();
    void phoneSyncDownloadSucceeded(const QByteArray &data);
    void phoneSyncDownloadFailed(int http);
    void phoneSyncFail(const QString &msg);
    QSet<QString> m_pinnedIds;
    void processOldDmBatch(const QVariantList &rawMsgs, const QString &emitThread, bool bulk);
    void handleSyncPage(const QVariantList &rawMsgs);
    void sendSyncPage();
    void finishDmSync(const QString &why);
    void startGroupSync();
    void fetchGroupHistorySync(const QString &groupId);
    void finishGroupSync();
    bool dbMessageExists(const QString &msgId) const;
    QString m_conversationServiceUrl;
    QMap<QString, QString> m_pendingPhotoMsgIds;
    // clientId -> {localPath,fileSize,fileName} for photos just sent. The WS echo (cmd=501) usually
    // arrives before the HTTP response and can reuse the local file instead of re-downloading
    // (avoids grey images after relogin). Removed once used.
    QMap<QString, QVariantMap> m_pendingSentPhotoInfo;

    // fileId (from asyncfile/upload) -> pending video/file info. Unlike photos, the URL is not in
    // the HTTP response: wait for WS cmd=601 act_type="file_done" before step 2 (asyncfile/msg).
    // "isFile" selects the progress signal.
    QMap<QString, QVariantMap> m_pendingVideoUpload;

    // clientId -> state of an in-progress chunked sendVideo()/sendFile(): the server rejects chunks
    // over 512K, so remaining bytes and progress live here between async replies.
    // "isFile" selects videoUploadProgress vs fileUploadProgress.
    QMap<QString, QVariantMap> m_pendingVideoChunkUpload;
    void sendVideoChunk(const QString &clientId);

    QNetworkReply *m_videoDownloadReply;
    QString        m_videoDownloadMsgId;
    QString        m_videoDownloadDestPath;

    // Must be a member: open() doesn't block, so the picker lives until one of its signals fires
    // (each slot deleteLater()s it and resets the pointer).
    bb::cascades::pickers::ContactPicker *m_contactPicker;
    QString m_contactPickerThreadId;

    QMap<QString, QString> m_avatarCache;
    QSet<QString> m_pendingAvatars;
    QMap<QString, QSet<QString> > m_pendingAvatarWaiters;
    QMap<qint64, QString> m_stickerCache;
    QSet<qint64> m_pendingStickers;

    sqlite3 *m_db;
    QVariantList m_pendingFriends;
    int m_pendingFriendAvatarCount;
    int m_loadedFriendAvatarCount;

    QNetworkReply *m_updateReply;
    QString        m_updateDestPath;

    static const int API_VERSION = 671;
    static const int API_TYPE = 30;
    static const int KEEPALIVE_INTERVAL_MS = 120000;
    static const char *USER_AGENT;
    QString generateRandomUserAgent();
    static const char *AES_FIXED_KEY;
};

#endif
