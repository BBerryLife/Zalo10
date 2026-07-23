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
    // Exposes our own uid to QML so a reply's quoteOwnerId can be compared
    // against it (needed to tell "I'm replying to my own earlier message"
    // apart from "I'm replying to the other person" — see quoteSenderResolved
    // in ChatView.qml's delegate). No NOTIFY: set once during login and never
    // changes for the lifetime of a session, same as how m_uid itself behaves.
    Q_PROPERTY(QString selfUid READ selfUid CONSTANT)

public:
    explicit ZaloService(QObject *parent = 0);
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }
    QString selfUid() const { return m_uid; }
    // Reliable uid -> display name lookup for group members, built from
    // getmg-v2's currentMems (see m_memberNames above). Returns "" if the
    // uid isn't known yet (e.g. group details haven't been fetched this
    // session) — callers should fall back to something honest rather than
    // the wire's per-message dName in that case, same as
    // GroupBoardSheet.qml's creatorLabel() already does.
    Q_INVOKABLE QString memberDisplayName(const QString &uid) const { return m_memberNames.value(uid, QString()); }

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
    // Group board: fetches all pinned messages/notes/polls for a group.
    // Ported from zca-js's getListBoard.ts (.../api/board/list, board_type=0
    // = "all types"). page/count mirror that API's pagination knobs; the QML
    // side currently always asks for page 1 with a generous count since the
    // board sheet shows everything in one scrollable list rather than paging.
    Q_INVOKABLE void fetchGroupBoard(const QString &groupId, int page, int count);
    // Pin a message to the group board. zca-js (the JS reference library this
    // app otherwise ports its API calls from) doesn't expose a "pin message"
    // endpoint at all, which is why this used to be a silent no-op stub —
    // but zlapi (a separate, independently-reverse-engineered Zalo API for
    // Python: github.com/Its-VrxxDev/zlapi) does, as pinGroupMsg(). Ported
    // from ITS actual request shape instead: POSTs to the SAME
    // .../api/board/topic/createv2 endpoint fetchGroupBoard's sibling
    // createNote already uses, just with type:2 (PinnedMessage) instead of
    // type:0 (Note) and a params.params payload describing the pinned
    // message rather than a note title. msgType here is Zalo's *client*
    // message type code (1=text/webchat, 32=photo — see
    // sendMessageQuote()'s qmsgType doc above), NOT our local 1/2 msgType;
    // QML converts before calling this, same as it already does for quotes.
    // Group-only: Zalo has no equivalent 1-1 "pin" endpoint (zlapi doesn't
    // define one either), matching the "Group board" action's isGroup-only
    // gating in ChatView.qml.
    Q_INVOKABLE void pinGroupMessage(const QString &groupId, const QString &msgId,
                                      const QString &cliMsgId, const QString &senderId,
                                      const QString &senderName, const QString &content,
                                      int msgType);
    // Create a note in the group board. Ported from zca-js's createNote.ts:
    // same .../api/board/topic/createv2 endpoint pinGroupMessage() uses,
    // type:0 (BoardType.Note) instead of type:2 (PinnedMessage), and
    // params.params only carries {title} — no client_msg_id/global_msg_id/
    // senderUid, since a note isn't tied to an existing chat message.
    Q_INVOKABLE void createGroupNote(const QString &groupId, const QString &title, bool pinAct);
    // Create a poll in the group. Ported from zca-js's createPoll.ts —
    // NOTE this hits the plain "group" service (m_groupServiceUrl), NOT
    // group_board and NOT group_poll; see fetchGroupBoard()'s doc comment
    // for why those two are easy to confuse with each other here.
    // optionsList is a plain list of option strings (2+ required by Zalo).
    Q_INVOKABLE void createGroupPoll(const QString &groupId, const QString &question,
                                      const QStringList &optionsList, bool allowMultiChoices,
                                      bool allowAddNewOption, bool hideVotePreview,
                                      bool isAnonymous);
    // Vote (or change vote / unvote with an empty optionIds) on a poll.
    // Ported from zca-js's votePoll.ts — GET with encrypted params in the
    // query string (like fetchGroupBoard), hits m_groupServiceUrl same as
    // createGroupPoll above. optionIds empty = clear the caller's vote.
    // groupId isn't part of Zalo's vote API itself — it's only carried
    // through so onVoteGroupPollDone can target the Hub notification at
    // the right thread (votePoll.ts's endpoint has no group_id parameter).
    Q_INVOKABLE void voteGroupPoll(const QString &groupId, const QString &pollId, const QList<int> &optionIds);
    Q_INVOKABLE void fetchMessages(const QString &threadId, bool isGroup);
    Q_INVOKABLE void sendMessage(const QString &threadId, const QString &content, bool isGroup);
    // Send a text message that quotes/replies to an earlier one. Ported from
    // zca-js's sendMessage.ts "quote" path (params qmsgOwner/qmsgId/qmsgCliId/
    // qmsgType/qmsgTs/qmsg, posted to .../quote instead of .../sms|sendmsg).
    // quoteMsgType is Zalo's *client* message type code (1=text/webchat,
    // 32=photo — see zca-js's getClientMessageType()), not our local msgType.
    Q_INVOKABLE void sendMessageQuote(const QString &threadId, const QString &content, bool isGroup,
                                       const QString &quoteMsgId, const QString &quoteCliMsgId,
                                       const QString &quoteOwnerId, const QString &quoteContent,
                                       int quoteMsgType, const QString &quoteTs,
                                       const QString &quoteSenderName);
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
    // Emitted after fetchGroupBoard() resolves — items is a flat list of
    // QVariantMaps, each tagged with a "boardType" string ("note"/"pin"/"poll")
    // so GroupBoardSheet.qml can filter into its 4 tabs without needing 3
    // separate signals. error is "" on success.
    void groupBoardReady(const QString &groupId, const QVariantList &items, const QString &error);
    // Result of pinGroupMessage(). error is "" on success.
    void pinMessageDone(bool success, const QString &error);
    // Result of createGroupNote(). error is "" on success. QML re-calls
    // fetchGroupBoard() on success to pick up the new item, same pattern
    // as other create/send actions in this app that don't locally splice
    // their own result into an existing list.
    void createNoteDone(bool success, const QString &error);
    // Result of createGroupPoll(). error is "" on success.
    void createPollDone(bool success, const QString &error);
    // Result of voteGroupPoll(). Carries the updated option list straight
    // from Zalo's response (zca-js's VotePollResponse) so QML can update
    // the poll card in place without needing a full fetchGroupBoard()
    // round-trip. pollId lets QML find the right card if more than one
    // poll is visible. error is "" on success (updatedOptions then valid).
    void votePollDone(bool success, const QString &pollId, const QVariantList &updatedOptions, const QString &error);
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    // Fired when a message already saved to the DB (almost always via the
    // HTTP send-confirm path, which has no server timestamp yet and falls
    // back to the device clock) gets its ts corrected once the WS echo
    // brings the real server timestamp — see the m_seenMsgIds branch in
    // ZaloService_WebSocket.cpp for the full explanation. If this message is
    // currently loaded in ChatView's model, QML should patch its ts in place
    // and re-run grouping; otherwise this is a no-op until the thread is
    // next opened, since dbLoadMessages() will pick up the corrected value.
    void messageTsCorrected(const QString &threadId, const QString &msgId, const QString &newTs);
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
    void onFetchGroupBoardDone();
    void onPinGroupMessageDone();
    void onCreateGroupNoteDone();
    void onCreateGroupPollDone();
    void onVoteGroupPollDone();
    void onAcceptFriendDone();
    void onRejectFriendDone();
    void onGroupDetailsDone();
    void onFetchMsgDone();
    void onFetchPhotoDetailDone();  // HTTP fallback khi cmd=510 không trả HTTP URL
    void onSendMsgDone();
    void onSendMsgQuoteDone();
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
    bool isMessageDeletedForMe(const QString &msgId) const; // tombstone lookup — must be checked before handing any resynced msg to the UI, not just before writing to DB

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
    // Set khi mất kết nối do lỗi tầng thấp trước khi handshake WS thành công
    // (đặc biệt SSL handshake fail — xem onWsSocketError/onWsSslErrors) —
    // báo cho onWsReconnectTimer biết lần reconnect tới nên thử HOST KHÁC
    // trong m_wsUrls thay vì cứ quay lại đúng host cũ. Một số host trong
    // pool zpw_ws (vd ws8-msg, ws12-msg) đã tắt TLS 1.0 phía server, mà
    // BB10's Qt4/OpenSSL không có TLS 1.1/1.2 (giới hạn platform, không
    // sửa được bằng QSsl::AnyProtocol — AnyProtocol trên stack này vẫn chỉ
    // negotiate tối đa TLS 1.0) nên các host đó sẽ luôn handshake fail với
    // BB10, retry cùng host mãi mãi không bao giờ connect được. Các host
    // khác trong cùng pool (ws3, ws4...) vẫn chấp nhận TLS 1.0 bình thường.
    bool        m_wsAdvanceUrlOnReconnect;
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
    // Decodes a WS command's raw payload body { data: "<base64>", encrypt:
    // 0|1|2|3 } into its inner QVariantMap. Extracted from the cmd=501/521
    // (new message) handling so cmd=601 (group_event — pin/note/poll
    // activity, see handleWsMessage) can reuse the exact same
    // GCM-decrypt/gzip-inflate/AES-CBC-fallback pipeline instead of a third
    // copy-pasted version. debugTag is only used in qDebug() lines to tell
    // which caller's log output is which (e.g. "cmd501", "cmd601").
    QVariantMap decodeWsEnvelope(const QVariantMap &outer, const QString &debugTag);
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
    QString m_groupPollServiceUrl; // zpwServiceMap.group_poll[0] — NOT used by
                                    // board/pin/note or even poll vote/create/lock
                                    // actions (those use group_board / group — see
                                    // m_groupBoardServiceUrl below); kept parsed
                                    // since it's a distinct real service key, in
                                    // case something future needs it specifically.
    QString m_groupBoardServiceUrl; // zpwServiceMap.group_board[0] — the actual
                                     // host for /api/board/list and
                                     // /api/board/topic/createv2|updatev2 (board
                                     // listing, pin, note create/edit). Confirmed
                                     // against zca-js's getListBoard.ts/
                                     // createNote.ts/editNote.ts, which all build
                                     // their serviceURL from zpwServiceMap.group_board,
                                     // a DIFFERENT array from zpwServiceMap.group_poll.
                                     // Poll actions themselves (detail/create/vote/
                                     // end/option/add/share) use plain
                                     // zpwServiceMap.group == m_groupServiceUrl,
                                     // already parsed above — not this one either.
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
    // uid -> display name, built from every group's "currentMems" list as
    // groups are fetched (see fetchGroupDetails()). This is the reliable
    // source for "who sent this group message" — the per-message wire
    // "dName" field is NOT reliable for incoming messages (confirmed
    // on-device: an incoming message from another member can carry OUR OWN
    // display name instead of theirs — same bug class the Reply feature's
    // otherDisplayName/quoteSenderResolved fix in ChatView.qml works around
    // for 1-1 threads via threadName; groups need this uid-keyed map instead
    // since there's no single "the other person").
    QMap<QString, QString> m_memberNames;
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
