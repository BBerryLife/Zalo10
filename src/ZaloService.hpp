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

// Forward-declare thay vì #include <bb/cascades/pickers/ContactPicker> ở
// đây — header đó kéo theo khá nhiều dependency (bb/pim/contacts, QtDeclarative)
// không cần thiết cho các file khác chỉ include ZaloService.hpp để dùng các
// hàm không liên quan tới contact. Include đầy đủ nằm trong
// ZaloService_ContactPicker.cpp, nơi thực sự dùng class này.
namespace bb { namespace cascades { namespace pickers { class ContactPicker; } } }

class HubIntegration;

class ZaloService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    // Expose uid của mình cho QML, để so sánh quoteOwnerId khi biết đang
    // reply tin nhắn của chính mình hay của người khác.
    Q_PROPERTY(QString selfUid READ selfUid CONSTANT)

public:
    explicit ZaloService(QObject *parent = 0);
    virtual ~ZaloService();

    bool loggedIn() const { return m_loggedIn; }
    QString selfUid() const { return m_uid; }
    // Tra tên hiển thị thành viên nhóm từ uid, build từ getmg-v2's currentMems.
    // Trả về "" nếu chưa biết uid này (chưa fetch group details).
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
    // Group board: fetch toàn bộ pinned message/note/poll của 1 nhóm.
    // page/count là tham số phân trang, hiện QML luôn xin page 1 với count lớn
    // vì board sheet hiển thị hết trong 1 list cuộn được, không phân trang.
    Q_INVOKABLE void fetchGroupBoard(const QString &groupId, int page, int count);
    // Ghim 1 tin nhắn vào group board. POST tới cùng endpoint createNote dùng,
    // chỉ khác type:2 (PinnedMessage) thay vì type:0 (Note). msgType ở đây là
    // mã message type của Zalo (1=text, 32=photo...), không phải mã 1/2 nội bộ.
    // Chỉ dùng cho group — Zalo không có API pin cho chat 1-1.
    Q_INVOKABLE void pinGroupMessage(const QString &groupId, const QString &msgId,
                                      const QString &cliMsgId, const QString &senderId,
                                      const QString &senderName, const QString &content,
                                      int msgType);
    // Tạo note trong group board, dùng cùng endpoint pinGroupMessage() nhưng
    // type:0 (Note), params chỉ có {title} vì note không gắn với tin nhắn nào.
    Q_INVOKABLE void createGroupNote(const QString &groupId, const QString &title, bool pinAct);
    // Tạo poll trong nhóm — LƯU Ý dùng service "group" thường, không phải
    // group_board hay group_poll (xem comment fetchGroupBoard() bên trên).
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
    // "Who voted" detail for a single poll. Ported from zca-js's
    // getPollDetail.ts (POST .../api/poll/detail on the plain "group"
    // service, same host as create/vote above) — unlike the vote-result
    // payload, PollDetail's per-option shape includes a `voters` array of
    // uids (zca-js's models/Board.ts: PollOptions.voters: string[]), which
    // Lấy chi tiết poll (danh sách người vote từng option) cho link "View
    // voters" trên poll card. Kết quả trả về qua pollDetailReady; resolve
    // uid sang tên hiển thị do QML tự làm (zService.memberDisplayName).
    Q_INVOKABLE void getPollDetail(const QString &pollId);
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
    // Forward nội dung 1 tin nhắn đã gửi sang 1 hay nhiều thread KHÁC, cùng
    // loại (toàn group hoặc toàn 1-1, không trộn). Post lên file service
    // (không phải group/chat service như sendMessage()). content là nội
    // dung gốc để replicate y hệt (text thường, hoặc JSON blob cho photo —
    // không cần re-upload vì server tự resolve URL CDN đã có sẵn).
    // origMsgId/origTs là msgId/ts của tin nhắn GỐC (không phải clientId
    // mới sinh) — cần để server đánh dấu đây là tin "forwarded" thay vì
    // tin thường gõ trùng nội dung. Kết quả trả về qua forwardMessageDone.
    Q_INVOKABLE void forwardMessage(const QString &content, const QStringList &threadIds, bool isGroup,
                                     const QString &origMsgId, const QString &origTs);
    // Xóa 1 tin nhắn.
    //   - onlyMe=true:  "xóa cho tôi" — luôn cho phép, mọi loại thread.
    //   - onlyMe=false: "xóa cho mọi người" — chỉ nhóm; với chat 1-1 phải
    //     dùng recallMessage() (thu hồi).
    // senderId dùng để check quyền client-side trước khi gửi lên server.
    Q_INVOKABLE void deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId, const QString &senderId, bool onlyMe);
    // Thu hồi tin nhắn mình đã gửi — xóa khỏi cả 2 phía, dùng được cho
    // cả chat 1-1 và nhóm.
    Q_INVOKABLE void recallMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                    const QString &cliMsgId);
    // Thêm/đổi/xóa reaction của MÌNH trên 1 tin nhắn. icon là 1 trong
    // "like"/"heart"/"haha"/"wow"/"cry"/"angry" (rỗng khi rType==-1 = xóa
    // reaction — QML đã apply optimistic local state, hàm này chỉ relay
    // lên server và lưu lại nếu thành công). rType là chỉ số reaction 0..5.
    Q_INVOKABLE void reactMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                   const QString &cliMsgId, int msgType, int rType, const QString &icon);
    // Load hết reaction đã biết cho toàn bộ tin nhắn trong 1 thread, trong
    // 1 query duy nhất — {msgId: {uid: {icon, ts}}} — để mở thread không
    // cần query DB từng tin nhắn một.
    Q_INVOKABLE QVariantMap dbLoadThreadReactions(const QString &threadId);
    Q_INVOKABLE void sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption = QString());
    // Copy ảnh từ picker vào cache persistent "/tmp/zalo_img_local_<ts>.<ext>"
    // ngay trước khi upload, để ảnh gốc không mất kể cả khi round-trip
    // WS echo/CDN thất bại. Trả về path mới, hoặc path gốc nếu copy lỗi.
    Q_INVOKABLE QString cacheLocalImage(const QString &sourcePath);
    // Gửi file tài liệu (doc/docx, ppt/pptx, xls/xlsx, txt, pdf). Dùng chung
    // pipeline chunked-upload (<=512K/chunk) với sendVideo() — an toàn cho
    // file nặng (vd. ~30MB): tránh 1 POST khổng lồ dễ timeout, và progress
    // báo về qua fileUploadProgress thay vì videoUploadProgress.
    Q_INVOKABLE void sendFile(const QString &threadId, const QString &localFilePath, bool isGroup);
    // Xoá 1 file cục bộ — dùng bởi các luồng tạo file tạm trước khi gửi
    // (VoiceNoteSheet ghi .m4a ra /tmp, ContactPicker build .vcf ra /tmp)
    // để dọn dẹp khi người dùng huỷ (Discard/Cancel) thay vì gửi đi. Chấp
    // nhận cả path có/không có tiền tố "file://". Không log lỗi nếu file
    // không tồn tại — Discard gọi hàm này ngay cả khi chưa từng ghi được gì.
    Q_INVOKABLE void deleteLocalFile(const QString &path);
    // Mở bb::cascades::pickers::ContactPicker (single-select) để chọn 1
    // danh bạ trên máy. Khi người dùng chọn xong, build 1 file .vcf (VCF
    // 3.0, tự viết tay — BB10 SDK không có API export vCard sẵn cho
    // Contact) từ bb::pim::contacts::ContactService::contactDetails(), lưu
    // vào /tmp, rồi emit contactVcfReady(threadId, path) để QML gửi đi qua
    // sendFile() giống mọi file đính kèm khác. Nếu người dùng bấm Cancel
    // trên picker, hoặc contact rỗng/không tìm thấy, emit contactPickError
    // thay vào đó — không có gì được gửi trong cả 2 trường hợp.
    //
    // threadId phải được truyền vào và mang theo lại trong cả 2 signal kết
    // quả — KHÔNG được bỏ qua dù chỉ có 1 ContactPicker mở tại 1 thời điểm.
    // Lý do: mỗi lần push 1 ChatView, ComponentDefinition.createObject() tạo
    // 1 Page mới, và Page đó không bị destroy ngay khi pop khỏi
    // NavigationPane — nó có thể còn sống trong lịch sử pane. Mỗi Page còn
    // sống đều có 1 "Connections { target: zService; onContactVcfReady }"
    // của riêng nó lắng nghe zService (singleton toàn app). Nếu signal
    // không mang threadId để mỗi Page tự lọc "đây có phải thread của tôi
    // không", MỌI Page còn sống đều nhận và gửi file — đây chính là
    // nguyên nhân bug gửi trùng .vcf 3 lần (3 ChatView instance còn sống
    // cùng lắng nghe 1 signal không phân biệt được thread).
    Q_INVOKABLE void pickContact(const QString &threadId);
    // Gửi video .mp4: upload asyncfile/upload rồi đợi WS cmd=601 act_type=
    // "file_done" trả fileUrl thật (khác ảnh, upload video không trả URL
    // ngay trong response HTTP) trước khi gửi tin nhắn qua asyncfile/msg.
    Q_INVOKABLE void sendVideo(const QString &threadId, const QString &localFilePath, bool isGroup);
    // Gọi nội bộ từ WebSocket handler (cmd=601 act_type="file_done") khi
    // fileId khớp 1 video đang chờ trong m_pendingVideoUpload — không expose
    // ra QML.
    void handleFileUploadDone(const QString &fileId, const QString &fileUrl);
    Q_INVOKABLE void downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId = QString());
    // Tải video/file (msgType=3) từ href CDN về /tmp. Idempotent — nếu file
    // đã tồn tại ở /tmp cho msgId đó thì trả về path luôn, không tải lại.
    // Kết quả trả về async qua videoDownloadProgress/Finished/Failed.
    Q_INVOKABLE void downloadVideoMessage(const QString &msgId, const QString &url, const QString &fileName);
    Q_INVOKABLE void downloadAvatar(const QString &threadId, const QString &url);
    // Tải ảnh sticker (msgType=5, content={"stickerId":N}) từ CDN public
    // zalo-api.zadn.vn về /tmp. Cache theo stickerId vĩnh viễn (không như
    // avatar, ảnh sticker của 1 id không bao giờ đổi) — 1 lần tải cho mọi
    // thread/mọi lần xem sau, kể cả restart app. Kết quả async qua
    // stickerReady(stickerId, localPath).
    //
    // stickerId là QString (không phải qint64) dù bản chất là số — mọi ID
    // khác truyền qua ranh giới QML/C++ trong codebase này đều theo convention
    // QString (threadId, msgId, groupId, pollId...); qint64 là kiểu Q_INVOKABLE
    // param duy nhất phá lệ và gây SIGSEGV thật trong QString::fromLatin1_helper
    // khi QML JS number marshal sang qint64 qua Qt 4.8's meta-object system —
    // đổi về QString rồi parse bằng toLongLong() bên trong là an toàn.
    Q_INVOKABLE void downloadSticker(const QString &stickerId);
    // Update downloader — gọi từ AboutSheet khi user xác nhận update.
    // Lưu vào /accounts/1000/shared/downloads/<filename>.
    Q_INVOKABLE void downloadUpdate(const QString &url, const QString &filename);
    // Long-press "Download" trên bubble ảnh: copy file cache local sẵn có
    // sang thư mục downloads chung để hiện trong gallery máy. Không tải lại
    // từ CDN. Trả về path đã lưu nếu ok, rỗng nếu lỗi.
    Q_INVOKABLE QString downloadPhotoToGallery(const QString &localImagePath, const QString &msgId);
    // Hủy 1 downloadUpdate() đang chạy dở (vd user bấm Cancel). An toàn để
    // gọi kể cả khi không có gì đang tải.
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
    // Banner dialog hiện ngay ở đầu màn hình, kể cả khi Zalo10 đang là app
    // foreground — khác sendHubNotification() vì banner của Hub chỉ hiện
    // khi app KHÔNG active, ngược với cái cần ở đây.
    Q_INVOKABLE void sendBannerNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup = false);
    // Tra lại isGroup đã lưu khi item được đẩy lên Hub (upsertThreadItem)
    // cho threadId này — dùng ở ApplicationUI::onInvoked() vì payload JSON
    // Hub gửi khi tap item không có field isGroup/is_group nào (chỉ có
    // "attributes.sourceId"). Xem HubIntegration::isGroupThread().
    Q_INVOKABLE bool isGroupHubThread(const QString &threadId) const;
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
    Q_INVOKABLE QVariantList getQuickMessages() const;                                        // [{id,name,content}], sắp xếp A-Z theo tên
    Q_INVOKABLE int          addQuickMessage(const QString &name, const QString &content);     // trả id mới, -1 nếu lỗi/trùng tên
    Q_INVOKABLE bool         updateQuickMessage(int id, const QString &name, const QString &content); // false nếu lỗi/trùng tên
    Q_INVOKABLE bool         deleteQuickMessage(int id);
    // Lấy list "Tin nhắn nhanh" từ tài khoản Zalo thật của user, merge vào
    // bảng quick_messages local — khớp theo tên, cái nào có sẵn thì giữ.
    Q_INVOKABLE void         fetchServerQuickMessages();
    // Trả {"width": w, "height": h} của 1 ảnh local, để size bubble đúng
    // tỉ lệ ngay khi chọn ảnh, trước khi upload xong.
    Q_INVOKABLE QVariantMap getImageDimensions(const QString &localFilePath) const;
    Q_INVOKABLE qint64 getFileSize(const QString &localFilePath) const;

    // ---- Data export / import / cache management (Settings) -----------------
    // Chạy trên UI thread nhưng đủ nhanh (SQLite + ghi 1 file nhỏ) nên chỉ
    // cần SystemProgressToast, không cần QThread riêng.
    //
    // exportData: xuất toàn bộ tin nhắn + quick message ra 1 file JSON.
    // Chỉ text, không copy ảnh (ảnh nằm ở cache tmp, không sống sót qua
    // update/reinstall). Tin nhắn có ảnh vẫn giữ text kèm tag "[Photo]".
    // Trả về {"success": bool, "path": ..., "messageCount": n, "error": msg}
    Q_INVOKABLE QVariantMap exportData(const QString &destDir);

    // importData: đọc lại file JSON từ exportData(). Tin nhắn đã có msgId
    // local thì bỏ qua (data cũ luôn thắng), nên chạy lại bao nhiêu lần
    // cũng an toàn. Quick message khớp theo tên (không phân biệt hoa/thường).
    // Trả về: {"success": bool, "importedMessages": n, "skippedMessages": n,
    //           "importedQuickMessages": n, "skippedQuickMessages": n, "error": msg}
    Q_INVOKABLE QVariantMap importData(const QString &jsonFilePath);

    // clearCache: xóa hết file ảnh cache (avatar, thumbnail, QR code) và
    // xóa sạch bảng lịch sử tin nhắn local (giữ nguyên cleared_threads và
    // quick_messages vì đó là data/settings của user, không phải cache).
    // Conversation phía server không bị ảnh hưởng. Trả về số file đã xóa.
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
    // Bắn khi fetchGroupBoard() xong — items là list phẳng, mỗi item tag
    // sẵn "boardType" ("note"/"pin"/"poll") để QML tự lọc vào 4 tab.
    void groupBoardReady(const QString &groupId, const QVariantList &items, const QString &error);
    // Kết quả pinGroupMessage(). error rỗng nếu thành công.
    void pinMessageDone(bool success, const QString &error);
    // Kết quả createGroupNote(). error rỗng nếu ok. QML gọi lại
    // fetchGroupBoard() để lấy item mới.
    void createNoteDone(bool success, const QString &error);
    // Kết quả createGroupPoll(). error rỗng nếu ok.
    void createPollDone(bool success, const QString &error);
    // Kết quả voteGroupPoll(). Trả kèm option list đã cập nhật từ response
    // của Zalo, để QML update poll card tại chỗ không cần fetch lại toàn bộ.
    void votePollDone(bool success, const QString &pollId, const QVariantList &updatedOptions, const QString &error);
    // Kết quả getPollDetail(). Mỗi option trong detail có thêm "voters"
    // (list uid) bên cạnh content/votes/optionId. error rỗng nếu ok.
    void pollDetailReady(const QString &pollId, const QVariantMap &detail, const QString &error);
    // Bắn cho MỌI event cmd601 group-board (pin/unpin/update_board/
    // update_topic/remove_*), kể cả khi Hub notification bị suppress cho
    // self-action/thread đang mở — signal này KHÔNG bị suppress, để
    // ChatView vẫn phản ứng in-app (system-notice row, refresh poll card)
    // dù user đang xem thread đó. QML tự lọc theo groupId.
    void boardEventOccurred(const QString &groupId, const QString &act, const QString &actorName,
                             bool isSelf, int topicType, const QString &topicId, const QString &title);
    // successCount/failCount lấy thẳng từ response server ({success:[...], fail:[...]}),
    // không surface chi tiết lỗi từng target, chỉ tổng hợp để hiện toast.
    void forwardMessageDone(bool success, int successCount, int failCount, const QString &error);
    void friendRequestResponded(const QString &friendId, bool accepted, bool success);
    void messagesReady(const QString &threadId, const QVariantList &messages);
    void messageSent(bool success, const QString &threadId);
    void newMessage(const QString &threadId, const QVariantMap &message);
    // Bắn khi 1 tin nhắn đã lưu DB (qua HTTP send-confirm, chưa có ts thật
    // của server) được sửa ts đúng sau khi WS echo mang ts server về.
    // Nếu tin đang hiện trong ChatView, QML nên patch ts tại chỗ và group lại.
    void messageTsCorrected(const QString &threadId, const QString &msgId, const QString &newTs);
    // Bắn khi 1 tin nhắn đang hiển thị bị người gửi thu hồi (chat.undo).
    // QML nên update bubble tại chỗ.
    void messageRecalled(const QString &threadId, const QString &msgId);
    // Bắn khi "xóa cho tôi" của CHÍNH MÌNH được xác nhận qua WS và DB row
    // đã bị xóa cứng. Khác messageRecalled ở chỗ: xóa hẳn bubble khỏi model,
    // không giữ placeholder. Chỉ bắn cho hành động xóa của MÌNH.
    void messageDeletedLocally(const QString &threadId, const QString &msgId);
    // Bắn khi reaction trên 1 tin nhắn thay đổi — của người khác qua WS,
    // hoặc echo của chính reactMessage() mình gọi (QML đã optimistic-update
    // rồi nên apply lại là no-op). icon == "" nghĩa là uid đó gỡ reaction.
    void reactionUpdated(const QString &threadId, const QString &msgId, const QString &uid, const QString &icon);
    // Kết quả của reactMessage() do CHÍNH MÌNH gọi (khác reactionUpdated,
    // cái đó bắn cả cho action của người khác) — để QML hiện toast lỗi và
    // rollback optimistic update nếu server call thất bại.
    void reactMessageDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    // Kết quả deleteMessage()/recallMessage() do CHÍNH MÌNH gọi (khác
    // messageRecalled — đó là notification đến từ người khác qua WS).
    void messageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void messageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error);
    void threadLastMessageChanged(const QString &threadId, const QString &lastMsg, const QString &lastTime);
    // threadId, localFilePath (file:///tmp/...)
    void avatarReady(const QString &threadId, const QString &localPath);
    // stickerId, localFilePath (file:///tmp/...) — ChatView.qml's
    // stickerBubble listens for this to swap its ImageView source in once
    // the download completes.
    void stickerReady(const QString &stickerId, const QString &localPath);
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
    // Tải video/file message (msgType=3) về /tmp — msgId để QML khớp đúng
    // bubble đang chờ, localPath rỗng nghĩa là lỗi (kèm errorMsg).
    void videoDownloadProgress(const QString &msgId, int percent);
    void videoDownloadFinished(const QString &msgId, const QString &localPath);
    void videoDownloadFailed(const QString &msgId, const QString &errorMsg);
    // Gửi video (sendVideo) — chia file thành nhiều chunk <=512K (giới hạn
    // server), percent = số chunk đã gửi / tổng chunk. threadId để QML khớp
    // đúng cuộc trò chuyện đang gửi (chỉ 1 video gửi cùng lúc/thread).
    void videoUploadProgress(const QString &threadId, int percent);
    // Cùng cơ chế chunk <=512K như video nhưng cho file tài liệu (doc/docx,
    // ppt/pptx, xls/xlsx, txt, pdf) — sendFile() giờ dùng chung pipeline
    // chunked-upload với sendVideo() (an toàn cho file nặng, ví dụ ~30MB,
    // tránh 1 POST khổng lồ dễ timeout/lỗi giữa chừng không rõ nguyên nhân).
    // Tách signal riêng khỏi videoUploadProgress để QML không lẫn lộn 2
    // thanh tiến trình khi cả video lẫn file cùng đang gửi ở các thread khác nhau.
    void fileUploadProgress(const QString &threadId, int percent);
    // Kết quả pickContact(threadId) — threadId khớp lại đúng cuộc trò
    // chuyện đã gọi pickContact() (xem ghi chú dài ở khai báo pickContact()
    // để hiểu vì sao bắt buộc phải có, không phải tuỳ chọn). path là
    // đường dẫn hệ thống (không tiền tố "file://") .vcf đã build xong, sẵn
    // sàng gửi qua sendFile() từ phía QML. Emit đúng 1 trong 2 signal này
    // (không cả 2) mỗi lần pickContact() được gọi.
    void contactVcfReady(const QString &threadId, const QString &path);
    // reason: "canceled" (người dùng bấm Cancel trên ContactPicker) hoặc
    // "error" (contactId không hợp lệ / contact rỗng / ghi file thất bại).
    void contactPickError(const QString &threadId, const QString &reason);

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
    void onGetPollDetailDone();
    void onForwardMessageDone();
    void onAcceptFriendDone();
    void onRejectFriendDone();
    void onGroupDetailsDone();
    void onFetchMsgDone();
    void onFetchPhotoDetailDone();  // HTTP fallback khi cmd=510 không trả HTTP URL
    void onSendMsgDone();
    void onSendMsgQuoteDone();
    void onDeleteMsgDone();
    void onRecallMsgDone();
    void onReactMsgDone();
    void onSendPhotoDone();
    void onSendPhotoMsgDone();
    void onSendVideoChunkUploadDone();
    void onSendVideoMsgDone();
    // ContactPicker chỉ emit 1 trong 3 signal này mỗi lần open() — canceled()
    // khi bấm Cancel, error() khi picker không mở được (tài nguyên hệ thống
    // cạn), contactSelected(id) khi chọn xong (mode Single, đúng mode
    // pickContact() dùng). Không cần onContactsSelected/onContactAttribute*
    // vì không dùng multi-select hay attribute-selection mode.
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

    // WebSocket (RFC 6455, TLS tự dựng bằng OpenSSL) — real-time messages
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
    // Helper lưu reaction xuống DB — dùng chung cho reactMessage() (tap
    // của mình) và WS handler cmd 501/521 (tap của người khác).
    void dbSaveReaction(const QString &threadId, const QString &msgId, const QString &uid, const QString &icon);
    void dbRemoveReaction(const QString &msgId, const QString &uid);
    QSize imageDimensions(const QString &localFileUrlOrPath) const; // bỏ "file://", đọc kích thước pixel
    void markMessageRecalled(const QString &threadId, const QString &msgId); // xử lý chat.undo
    void markMessageDeletedForMe(const QString &threadId, const QString &msgId); // xử lý chat.delete — local-only, xóa cứng
    bool isMessageDeletedForMe(const QString &msgId) const; // tra tombstone trước khi đưa msg resync lên UI

    // Helper export/import/cache
    QVariantList dbLoadAllMessages() const;     // toàn bộ row mọi thread — dùng cho exportData
    QStringList  cacheFilePatterns() const;     // các prefix filename app ghi vào tempPath()

    // Cache avatar persistent (bảng avatar_meta) — giúp downloadAvatar()
    // nhận ra avatar đã có sẵn trên máy qua các lần mở app, chỉ tải lại
    // khi URL avatar thực sự đổi hoặc file cache bị mất.
    void    loadAvatarCacheFromDb();                              // log avatar_meta vs file thực tế lúc khởi động
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

    // WebSocket (RFC 6455) — real-time messages. Trước đây dùng QSslSocket
    // trực tiếp, nhưng QSslSocket trên BB10 NDK luôn fail TLS handshake
    // (error:1407742E) dù OpenSSL link cùng app thực sự hỗ trợ TLS 1.2 —
    // giới hạn nằm ở tầng QSslSocket của BlackBerry, không phải OpenSSL.
    // Enum QSsl::SslProtocol của Qt4.8 trên BB10 còn không có TlsV1_1/1_2.
    //
    // Fix: dùng QTcpSocket thuần (chỉ TCP) cho m_webSocket, tự dựng TLS
    // bằng OpenSSL C API thẳng trên fd của nó, ép TLSv1_2_client_method(),
    // bỏ qua hoàn toàn QSslSocket. Xem wsTlsHandshakeStep()/wsWriteRaw()/
    // wsReadAvailable() trong ZaloService_WebSocket.cpp.
    QTcpSocket *m_webSocket;
    SSL_CTX    *m_wsSslCtx;      // 0 khi không dùng SSL (ws:// thường) hoặc chưa init
    SSL        *m_wsSsl;         // 0 cho tới khi bắt đầu handshake TLS
    bool        m_wsUseSsl;      // true nếu URL hiện tại là wss://
    bool        m_wsTlsEstablished; // true sau khi SSL_connect() thành công lần đầu
    QStringList m_wsUrls;       // zpw_ws[] từ login response (dùng m_wsUrls thay m_zpwWsUrls nội bộ)
    int         m_wsUrlIndex;
    // Set khi mất kết nối do lỗi tầng thấp trước khi handshake WS thành
    // công (đặc biệt SSL handshake fail) — báo cho onWsReconnectTimer biết
    // lần reconnect tới nên thử host khác trong m_wsUrls thay vì quay lại
    // đúng host cũ, đề phòng lỗi do một host cụ thể (mạng chập chờn, host
    // tạm downtime...).
    bool        m_wsAdvanceUrlOnReconnect;
    // Đếm số lần reconnect WS thất bại LIÊN TIẾP (reset về 0 khi upgrade WS
    // thành công). Dùng để tính backoff tăng dần thay vì retry cố định mỗi
    // 5s vô thời hạn — tránh xoay vòng tất cả host trong pool zpw_ws liên
    // tục gây ra hàng loạt TLS handshake thất bại dồn dập. Backoff tăng dần
    // để server "thở" — xem tính toán trong onWsDisconnected/onWsReadyRead.
    int         m_wsConsecutiveFailCount;
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
    // Giải mã payload thô của 1 command WS { data: "<base64>", encrypt:
    // 0|1|2|3 } thành QVariantMap. Dùng chung cho cmd=501/521 (tin nhắn
    // mới) và cmd=601 (group event) qua cùng 1 pipeline GCM-decrypt/
    // gzip-inflate/AES-CBC-fallback. debugTag chỉ để phân biệt log của
    // từng caller (vd "cmd501", "cmd601").
    QVariantMap decodeWsEnvelope(const QVariantMap &outer, const QString &debugTag);
    QByteArray maskWsFrame(int opcode, const QByteArray &data); // client→server cần mask
    int  wsNextReconnectDelayMs(); // tăng m_wsConsecutiveFailCount và trả về backoff (ms), cap 60s
    bool wsTlsHandshakeStep();     // dựng/tiếp tục TLS handshake bằng OpenSSL thô (thay QSslSocket)
    void wsPumpTlsOutput();        // rút ciphertext từ wbio, đẩy ra QTcpSocket thật
    qint64 wsWriteRaw(const QByteArray &data); // SSL_write() hoặc QTcpSocket::write() tuỳ m_wsUseSsl
    QByteArray wsReadDecrypted();  // rút hết plaintext đã giải mã sẵn có (SSL_read loop)
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
    QString m_groupPollServiceUrl; // zpwServiceMap.group_poll[0] — KHÔNG dùng cho
                                    // board/pin/note hay poll vote/create/lock
                                    // (những cái đó dùng group_board / group —
                                    // xem m_groupBoardServiceUrl); vẫn giữ parse
                                    // phòng khi cần dùng riêng sau này.
    QString m_groupBoardServiceUrl; // zpwServiceMap.group_board[0] — host thật
                                     // cho /api/board/list và
                                     // /api/board/topic/createv2|updatev2.
                                     // Các action poll (detail/create/vote/end/
                                     // option/add/share) dùng zpwServiceMap.group
                                     // (m_groupServiceUrl) như thường, không
                                     // phải service này.
    QString m_friendServiceUrl;    // zpwServiceMap.friend[0]
    QString m_fileServiceUrl;      // zpwServiceMap.file[0]
    QString m_quickMessageServiceUrl; // zpwServiceMap.quick_message[0]
    QString m_reactionServiceUrl;  // zpwServiceMap.reaction[0] — a DISTINCT
                                    // host from m_chatServiceUrl/m_groupServiceUrl.
                                    // reactMessage() previously posted to
                                    // m_chatServiceUrl + "/api/message/reaction",
                                    // which 404'd (confirmed from a live device
                                    // log: nginx 404 on tt-chat1-wpa.chat.zalo.me)
                                    // because that host doesn't serve the
                                    // reaction endpoint at all — zca-js's own
                                    // addReactionFactory() builds its URL from
                                    // api.zpwServiceMap.reaction[0] specifically,
                                    // a separate array in the service map, same
                                    // as m_groupBoardServiceUrl being separate
                                    // from m_groupServiceUrl above.

    QSet<QString> m_mutedThreads;  // threadIds currently muted
    QSet<QString> m_blockedUsers;  // userIds currently blocked
    QString m_externalToken;
    QStringList m_zpwWsUrls; // zpw_ws[] — lưu session, copy sang m_wsUrls khi connect

    QString m_activeThreadId;
    bool    m_activeThreadIsGroup;
    // Đăng ký Zalo10 thành 1 account/tab riêng trong BlackBerry Hub (kiểu
    // TBBX) thay vì rơi vào mục Notifications chung — xem HubIntegration.hpp
    // để biết lý do. Owned bởi ZaloService (parented), không cần tự delete.
    HubIntegration *m_hub;
    QString m_lastPollMsgId; // msgId cuối cùng đã biết, tránh emit trùng
    QMap<QString, QString> m_threadLastMsgId; // per-thread last msgId để fetch chính xác
    QMap<QString, QString> m_groupNames;        // groupId -> group name for notifications
    // uid -> tên hiển thị, build từ "currentMems" của mỗi group khi fetch.
    // Đây là nguồn đáng tin cậy để biết ai gửi tin nhắn nhóm — field "dName"
    // trên wire không đáng tin cho tin nhắn đến (đã xác nhận: có lúc tin của
    // người khác lại mang tên hiển thị của chính mình).
    QMap<QString, QString> m_memberNames;
    QSet<QString> m_seenMsgIds; // Tất cả msgId đã emit — dedup chắc chắn
    QString m_pending510Toid; // Thread đang chờ WS cmd=510 response (chỉ 1 tại 1 thời điểm)
    QMap<QString, QString> m_pendingPhotoMsgIds; // msgId -> threadId, waiting for photo URL via WS cmd=510
    // clientId (cliMsgId) -> {"localPath","fileSize","fileName"} cho ảnh vừa gửi.
    // Set trong sendPhoto() trước khi upload, để khi WS echo cmd=501 về (thường
    // đến TRƯỚC response HTTP send-msg) có thể dùng luôn file cache local thay
    // vì tải lại từ CDN (nguyên nhân cũ khiến ảnh vừa gửi hiện ô xám sau
    // logout/login). Entry bị xóa khi đã dùng xong, dù từ WS echo hay HTTP confirm.
    QMap<QString, QVariantMap> m_pendingSentPhotoInfo;

    // fileId (từ response asyncfile/upload) -> {"threadId","clientId",
    // "fileName","fileSize","isGroup","localPath","isFile"} — video/file
    // chờ WS cmd=601 act_type="file_done" trả fileUrl thật trước khi gửi
    // bước 2 (asyncfile/msg). Khác ảnh: upload video/file KHÔNG trả URL
    // ngay trong response HTTP. "isFile" (bool) phân biệt gửi progress qua
    // videoUploadProgress hay fileUploadProgress ở bước chunk — dùng chung
    // 1 map cho cả 2 loại vì cấu trúc pending giống hệt nhau.
    QMap<QString, QVariantMap> m_pendingVideoUpload;

    // clientId -> {"threadId","isGroup","localPath","fileName","fileSize",
    // "totalChunks","chunkIndex","fileData","isFile"} — state for the
    // chunked sendVideo()/sendFile() upload in progress. Server rejects any
    // single chunk over 512K, so both are split into <=512K pieces uploaded
    // one at a time via sendVideoChunk(); this map carries the remaining
    // bytes + progress between each chunk's async QNetworkReply. "isFile"
    // routes the progress signal (videoUploadProgress vs fileUploadProgress)
    // so a document upload doesn't drive the video bubble's UI or vice versa.
    QMap<QString, QVariantMap> m_pendingVideoChunkUpload;
    void sendVideoChunk(const QString &clientId);

    // Video/file đang tải về /tmp qua downloadVideoMessage(). Chỉ 1 tại 1
    // thời điểm (đủ dùng — tap-to-download tuần tự, không cần hàng đợi).
    QNetworkReply *m_videoDownloadReply;
    QString        m_videoDownloadMsgId;
    QString        m_videoDownloadDestPath;

    // ContactPicker đang mở qua pickContact(). Cần giữ làm member (không
    // phải biến cục bộ) vì open() không blocking — object phải sống tới
    // khi 1 trong 3 signal (contactSelected/canceled/error) bắn về ở slot
    // riêng. deleteLater() ở cuối mỗi slot, con trỏ set về 0 ngay sau đó.
    bb::cascades::pickers::ContactPicker *m_contactPicker;
    // threadId của ChatView đã gọi pickContact() — mang theo lại trong
    // contactVcfReady/contactPickError để đúng 1 ChatView instance (Page)
    // xử lý kết quả, không phải mọi Page còn sống trong NavigationPane
    // history đều nhận và tự gửi (xem ghi chú dài ở khai báo pickContact()).
    QString m_contactPickerThreadId;

    // Cache avatar: url -> localPath (file:///tmp/avatar_<md5>.jpg)
    QMap<QString, QString> m_avatarCache;
    QSet<QString> m_pendingAvatars; // Ngăn tải trùng lặp
    QMap<QString, QSet<QString> > m_pendingAvatarWaiters; // url -> set of threadIds đang chờ
    // Cache sticker theo stickerId — không cần waiters map như avatar vì 1
    // sticker chỉ có duy nhất 1 "chủ" đang chờ nó trong session này thường
    // (không multi-thread cùng chờ 1 stickerId theo threadId), QML tự lọc
    // đúng bubble qua stickerReady(stickerId,...).
    QMap<qint64, QString> m_stickerCache;
    QSet<qint64> m_pendingStickers;

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
