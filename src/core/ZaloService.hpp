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
    // cardMode: this process is the Hub reply card (InvokeCard startup). It only restores the saved session and
    // talks HTTP: no WebSocket (a second listener would kick the main app's), no Hub account, no timers.
    explicit ZaloService(QObject *parent = 0, bool cardMode = false);
    bool cardMode() const { return m_cardMode; }
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }
    QString selfUid() const { return m_uid; }
    // Name of a group member. Unknown ids are looked up in the background (getprofiles) and the
    // answer arrives through memberNamesResolved(); until then the result is empty.
    Q_INVOKABLE QString memberDisplayName(const QString &uid);

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
    Q_INVOKABLE void fetchProfileDetails(const QString &uid);
    // Own avatar from a file on the device (zca-js changeAccountAvatar: profile/upavatar). The picture is
    // squared, oriented and shrunk first. Result: selfAvatarChanged().
    Q_INVOKABLE void changeSelfAvatar(const QString &localFilePath);
    // Own bio / status line (zca-js updateProfileBio: profile/status). Result: selfBioChanged().
    Q_INVOKABLE void updateProfileBio(const QString &status);
    Q_INVOKABLE void fetchSelfProfile();   // own avatar URL + display name (profile/me-v2)
    Q_INVOKABLE void fetchBizAccount(const QString &uid);
    Q_INVOKABLE void fetchFullAvatar(const QString &uid);
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
    // Older group messages from Zalo's cloud message service (zca-js getCloudMessage, PR #397).
    // Saves one page into SQLite and answers with groupCloudOlderDone().
    Q_INVOKABLE void fetchGroupCloudOlder(const QString &groupId);
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
    // Sends a local sticker image from an installed pack (see StickerManager). GIFs use the zca-js
    // `gif` upload path (sendGif); png/jpg/webp reuse sendPhoto.
    Q_INVOKABLE void sendSticker(const QString &threadId, const QString &localFilePath, bool isGroup);
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
    // A contact changed their picture: forget what is cached for the old link, remember `url` as
    // the newest link for `uid` and download it again. Answers through avatarReady(uid, path) with
    // a NEW file path, so every list that shows this person refreshes.
    Q_INVOKABLE void refreshAvatar(const QString &uid, const QString &url);
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
    // Marks every thread with a yellow pellet as read locally, in the Hub and on Zalo's servers.
    // Returns how many threads were marked.
    Q_INVOKABLE int markAllThreadsRead();
    // Long-press "Mark as Read / Unread" in the Chats and Groups lists: same as the Hub actions (Hub item, in-app
    // pellet, official Zalo). syncUnreadMarks() pulls Zalo's own "marked unread" list (zca-js getUnreadMark).
    Q_INVOKABLE void markThreadReadUser(const QString &threadId, bool isGroup);
    // title / preview / timestampMs: used only to create the Hub item when the thread has none yet.
    // timestampMs is a QString on purpose: a qint64 Q_INVOKABLE parameter breaks the call from QML on Qt 4.8.
    Q_INVOKABLE void markThreadUnreadUser(const QString &threadId, bool isGroup, const QString &title = QString(),
                                          const QString &preview = QString(), const QString &timestampMs = QString());
    Q_INVOKABLE void syncUnreadMarks();
    Q_INVOKABLE bool isActiveThread(const QString &threadId) const
        { return !threadId.isEmpty() && m_activeThreadId == threadId; }
    Q_INVOKABLE void blockUser(const QString &userId);
    Q_INVOKABLE void unblockUser(const QString &userId);
    Q_INVOKABLE bool isBlocked(const QString &userId) const { return m_blockedUsers.contains(userId); }
    Q_INVOKABLE void setMute(const QString &threadId, bool isGroup, bool mute);
    Q_INVOKABLE bool isMutedThread(const QString &threadId) const { return m_mutedThreads.contains(threadId); }
    Q_INVOKABLE void clearHistory(const QString &threadId, bool isGroup);
    Q_INVOKABLE void leaveGroup(const QString &groupId);
    // Group management (zca-js createGroup / addUserToGroup / changeGroupName / changeGroupAvatar).
    // Results come back on createGroupDone / addGroupMembersDone / changeGroupNameDone / changeGroupAvatarDone.
    Q_INVOKABLE void createGroup(const QString &name, const QStringList &memberIds);
    Q_INVOKABLE void addGroupMembers(const QString &groupId, const QStringList &memberIds);
    Q_INVOKABLE void changeGroupName(const QString &groupId, const QString &name);
    Q_INVOKABLE void changeGroupAvatar(const QString &groupId, const QString &localFilePath);
    Q_INVOKABLE void sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    // Banner shown even while the app is in the foreground (Hub banners only show when inactive).
    Q_INVOKABLE void sendBannerNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    void setAppForeground(bool foreground) {
        m_appForeground = foreground;
        if (foreground) QTimer::singleShot(800, this, SLOT(onUnreadMarkTick()));
    }

    // UI <-> headless service handoff: Zalo allows one WebSocket per account, so only one process
    // holds realtime at a time. The UI process never calls these.
    void suspendRealtime();
    bool resumeRealtime();
    bool realtimeSuspended() const { return m_realtimeSuspended; }
    Q_INVOKABLE bool isGroupHubThread(const QString &threadId) const;
    Q_INVOKABLE QString hubThreadTitle(const QString &threadId) const;
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
    // Chats tab: one entry per 1-1 conversation that has at least one stored message (threadId, name,
    // avatar url, localAvatar). Read from the local database only, no network.
    Q_INVOKABLE QVariantList cachedChatThreads();
    // New Chat sheet: the friend list of the last successful fetchFriends() (empty before the first one).
    Q_INVOKABLE QVariantList cachedFriends() const { return m_lastFriendThreads; }
    // Group list saved by the last complete fetch, so the Groups tab can fill instantly at start.
    Q_INVOKABLE QVariantList cachedConversations();
    // Feeds: changes in contacts' profiles (avatar, cover, bio, birthday), found by comparing with
    // the last stored snapshot. startFeedsWatch() is idempotent and only ever called by the UI.
    Q_INVOKABLE void startFeedsWatch();
    Q_INVOKABLE QVariantList getFeedEvents(int limit);
    Q_INVOKABLE int unseenFeedCount();
    Q_INVOKABLE void markFeedsSeen();
    Q_INVOKABLE void clearFeedEvents();
    Q_INVOKABLE void setFeedsViewing(bool viewing);
    Q_INVOKABLE bool feedsNotifyEnabled() const;
    Q_INVOKABLE void setFeedsNotifyEnabled(bool enabled);
    // Hub "Reply" opens the reply card (card.composer, HubReplyCard.qml) instead of the app. Read when the Hub account is
    // registered at startup, so a change applies from the next launch. Default on.
    Q_INVOKABLE bool hubSheetEnabled() const;
    Q_INVOKABLE void setHubSheetEnabled(bool enabled);

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
    void memberNamesResolved();           // new group member names are known: refresh name labels
    void feedEventsChanged(int unseen);   // feed list changed; unseen = events not yet seen
    void friendsFetchFailed();   // Lite: fetchFriends ended without a usable list (429 / error / empty / network)
    void profileDetailsReady(const QString &uid, const QVariantMap &profile);
    void selfProfileReady(const QString &avatarUrl, const QString &displayName);
    void selfProfileDetails(const QVariantMap &profile);   // whole me-v2 profile (zca-js fetchAccountInfo)
    void selfAvatarChanged(bool ok, const QString &error);
    void bizAccountReady(const QString &uid, const QVariantMap &biz);
    void fullAvatarReady(const QString &uid, const QString &url);
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
    void groupCloudOlderDone(const QString &threadId, int saved, bool hasMore);
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
    void createGroupDone(bool ok, const QString &groupId, const QString &error);
    void addGroupMembersDone(const QString &groupId, bool ok, int failedCount, const QString &error);
    void changeGroupNameDone(const QString &groupId, bool ok, const QString &error);
    void changeGroupAvatarDone(const QString &groupId, bool ok, const QString &error);
    void selfBioChanged(bool ok, const QString &error);
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

private:
    // Tells Zalo's servers a thread was read (zca-js sendSeenEvent + removeUnreadMark), so the
    // official app shows it as read too. Fire-and-forget.
    void sendThreadSeenRemote(const QString &threadId, bool isGroupFallback = false);
    void sendRemoveUnreadMark(const QStringList &userIds, const QStringList &groupIds);
    void sendAddUnreadMark(const QString &threadId, bool isGroup);   // zca-js addUnreadMark
    void applyRemoteUnreadMarks(const QStringList &ids);
    bool dbLastIncomingMessage(const QString &threadId, QVariantMap &out) const;
    QString dbOldestMsgId(const QString &threadId) const;

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
    void onProfileDetailsDone();
    void onSelfProfileDone();
    void onChangeAvatarDone();
    void onBizAccountDone();
    void onFullAvatarDone();
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
    void onGroupChunkNext();
    void onFriendsNextPage();
    void onFeedScanTick();
    void onFeedProfilesDone();
    void onFeedRequestTimeout();
    void onMemberLookupTick();
    void onMemberLookupDone();
    void onFetchPhotoDetailDone();
    void onSendMsgDone();
    void onSendMsgQuoteDone();
    void onDeleteMsgDone();
    void onRecallMsgDone();
    void onReactMsgDone();
    void onSendPhotoDone();
    void onSendPhotoMsgDone();
    void onSendGifThumbDone();
    void onSendGifDone();
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
    void onGroupManageDone();   // createGroup / addGroupMembers / changeGroupName / changeGroupAvatar / updateProfileBio
    void onUnreadMarksDone();   // getUnreadMark reply
    void onUnreadMarkTick();    // periodic pull while the app is in the foreground

    void onQRExpired();
    void onListenTimer();
    void onListenDone();
    void onPollMsgDone();
    void onGroupCloudMsgDone();
    void onAvatarDownloaded();
    void onAvatarTimeout();   // Lite: aborts an avatar download that hung (QNetworkReply::abort is not a slot in Qt4)
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
    void sendGif(const QString &threadId, const QString &localPath, bool isGroup);
    void markMessageRecalled(const QString &threadId, const QString &msgId);
    void markMessageDeletedForMe(const QString &threadId, const QString &msgId);
    bool isMessageDeletedForMe(const QString &msgId) const;

    QVariantList dbLoadAllMessages() const;
    QStringList  cacheFilePatterns() const;

    void    loadAvatarCacheFromDb();
    bool    avatarMetaLookup(const QString &threadId, QString &urlHashOut, QString &localPathOut) const;
    void    avatarMetaUpsert(const QString &threadId, const QString &urlHash, const QString &localPath);
    void    avatarMetaForget(const QString &threadId);

    EncryptedParams buildEncryptedParams(const QVariantMap &data);
    QString buildSignKey(const QString &type, const QVariantMap &params);
    QString generateIMEI();
    QString generateUUIDv4();
    QString buildRawUrl(const QString &base, const QVariantMap &params);
    QNetworkRequest buildRequest(const QString &urlStr, const QString &referer, bool jsonAccept = false);
    QNetworkReply *sendProfileApi(const QString &path, bool post, QVariantMap params);
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
    QTimer *m_unreadMarkTimer;
    bool m_cardMode;

    // WebSocket TLS is built by hand: QSslSocket on BB10 always fails the handshake (error:1407742E)
    // and Qt 4.8's QSsl has no TlsV1_1/1_2. m_webSocket is a plain QTcpSocket with TLS run through
    // the OpenSSL C API (TLS_client_method(), min TLS 1.2). See wsTlsHandshakeStep()/wsWriteRaw()/wsReadAvailable().
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
    void handleWsHandshakeReply(const QByteArray &data);
    void handleWsChatPush(qint32 cmd, const QByteArray &data);
    void handleWsPushedMessage(QVariantMap m, bool isGroup);
    void handleWsHistoryReply(const QByteArray &data);
    void handleWsGroupHistoryReply(const QByteArray &data);
    void handleWsGroupEvent(const QByteArray &data);
    void handleWsReceipts(qint32 cmd, const QByteArray &data);
    void handleWsReactions(const QByteArray &data);
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
    // Members whose name is missing are fetched in small batches (see Contacts_Feeds.cpp). Names are
    // kept in the member_names table so they are there right after a restart.
    QStringList   m_memberQueue;
    QSet<QString> m_memberAsked;
    QTimer       *m_memberTimer;
    bool          m_memberBusy;
    bool          m_memberDbLoaded;
    void memberEnsureTable();
    void loadMemberNamesFromDb();
    void rememberMemberName(const QString &uid, const QString &name, bool overwrite = true);
    void rememberMemberNames(const QMap<QString, QString> &names);   // one transaction, never overwrites
    QSet<QString> m_seenMsgIds;
    QString m_pending510Toid;
    QString m_pending511Gid;

    struct HistorySyncState {
        HistorySyncState()
            : active(false), awaiting(false), pages(0), totalMsgs(0), retries(0),
              groupActive(false), groupAwaiting(false), groupRan(false), groupPages(0), groupRetries(0), groupMsgs(0),
              lastStartMs(0), watchdog(0) {}
        bool    active;
        bool    awaiting;
        int     pages;
        int     totalMsgs;
        int     retries;
        QString cursor;
        QSet<QString> threads;
        bool    groupActive;
        bool    groupAwaiting;
        bool    groupRan;
        int     groupPages;
        int     groupRetries;
        int     groupMsgs;
        QString groupCursor;
        qint64  lastStartMs;
        QTimer *watchdog;
    };
    HistorySyncState m_sync;

    struct PsHandle { PsHandle() : p(0) {} PhoneSyncRuntime *p; };
    PsHandle m_psh;
    void handlePhoneSyncEvent(const QString &act, const QVariantMap &data);
    QStringList m_friendUids;
    QVariantList m_lastFriendThreads;   // last complete friend list, kept for the New Chat sheet
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
    void sendGroupSyncPage();
    void handleGroupSyncPage(const QVariantList &rawMsgs);
    void saveGroupBuckets(const QVariantList &rawMsgs, const QString &openGid);
    QVariantList saveGroupHistory(const QVariantList &rawMsgs, const QString &tid, bool syncGroup);
    void finishGroupSync(const QString &why);
    bool dbMessageExists(const QString &msgId) const;
    QString m_conversationServiceUrl;
    QString m_groupCloudMsgServiceUrl;                 // zpwServiceMap.group_cloud_message
    QMap<QString, QString> m_cloudCursorId;            // groupId -> lastMsgId of the last cloud page
    QMap<QString, bool>    m_cloudCursorIsOld;         // groupId -> isOld of the last cloud page
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
    // uid -> newest avatar link seen in a profile-change event; older links requested for the same
    // uid (stale friend lists) are answered with this one.
    QMap<QString, QString> m_avatarLatestUrl;
    QSet<QString> m_pendingAvatars;
    QMap<QString, QSet<QString> > m_pendingAvatarWaiters;
    // Lite: at most MAX_AVATAR_DOWNLOADS avatar HTTP requests in flight; the rest wait here.
    int m_activeAvatarDownloads;
    QList<QPair<QString, QString> > m_avatarWaitQueue;
    static const int MAX_AVATAR_DOWNLOADS = 6;
    void startAvatarRequest(const QString &threadId, const QString &url);
    QMap<qint64, QString> m_stickerCache;
    QSet<qint64> m_pendingStickers;

    sqlite3 *m_db;
    QVariantList m_pendingFriends;
    int m_pendingFriendAvatarCount;
    int m_loadedFriendAvatarCount;

    QNetworkReply *m_updateReply;
    QString        m_updateDestPath;

    // ---- Scale: friends fetched page by page, groups in chunks (see Contacts_Friends/Conversations) ----
    QVariantList m_friendsAccum;
    QSet<QString> m_friendsSeen;
    int  m_friendsPage;
    int  m_friendsRetry;
    bool m_friendsLegacy;      // true: one big request (count=20000), the pre-paging behaviour
    void requestFriendsPage();
    void finishFriends();

    QStringList  m_groupQueue;
    int          m_groupPos;
    int          m_groupRetry;
    QVariantList m_groupAccum;
    bool         m_groupPartialSent;
    bool         m_groupHadCache;
    void requestGroupChunk();
    void finishGroups(bool complete);
    void saveGroupCache(const QVariantList &threads);

    // ---- Feeds ----
    QTimer  *m_feedTimer;
    QTimer  *m_feedWatchdog;
    QNetworkReply *m_feedReply;
    bool     m_feedWatching;
    bool     m_feedBusy;
    bool     m_feedsViewing;
    bool     m_feedTablesReady;
    bool     m_feedBatchProbe;
    int      m_feedBatch;
    int      m_feedTickMs;
    int      m_feedBackoffStep;
    int      m_feedOkStreak;
    qint64   m_feedStartAt;
    qint64   m_feedBackoffUntil;
    qint64   m_feedCycleStart;
    QStringList m_feedQueue;
    void feedsEnsureTables();
    void feedsOnFriends(const QVariantList &threads, bool complete);
    void feedsBuildQueue();
    void feedsBackoff(bool rateLimited);
    int  feedsApplyProfiles(const QStringList &uids, const QMap<QString, QVariantMap> &profiles, bool force);

    static const int API_VERSION = 671;
    static const int API_TYPE = 30;
    static const int KEEPALIVE_INTERVAL_MS = 120000;
    static const char *USER_AGENT;
    QString generateRandomUserAgent();
    static const char *AES_FIXED_KEY;
};

#endif
