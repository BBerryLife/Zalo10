#include "ZaloServiceProxy.hpp"
#include "ZaloServiceUtils.hpp"
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QHostAddress>
#include <bb/system/InvokeRequest>

// ---------------------------------------------------------------------------
// Xem giải thích kiến trúc đầy đủ trong ZaloServiceProxy.hpp. Tóm tắt:
//   QML gọi zService.sendMessage(...)
//     -> ZaloServiceProxy::sendMessage() ghi 1 dòng vào command_queue
//     -> HeadlessService (process riêng) đọc, gọi ZaloService::sendMessage() THẬT
//     -> kết quả (tin nhắn mới, messageSent...) đi ra qua SQLite (đã ghi sẵn
//        bởi dbSaveMessage) + bảng service_state, proxy poll để emit lại đúng
//        signal cho QML.
// ---------------------------------------------------------------------------

// Chuyển 1 QVariantMap đơn giản (chỉ String/Bool/Int/StringList — đủ cho toàn
// bộ tham số của các hàm Nhóm A) thành 1 chuỗi JSON thủ công — không dùng bất
// kỳ script engine nào, chỉ để ghi 1 dòng JSON hợp lệ vào command_queue.
static QString jsonEscape(const QString &s)
{
    QString out = s;
    out.replace('\\', "\\\\");
    out.replace('"', "\\\"");
    out.replace('\n', "\\n");
    out.replace('\r', "\\r");
    out.replace('\t', "\\t");
    return out;
}

static QString variantToJson(const QVariant &v)
{
    switch (v.type()) {
        case QVariant::Bool:
            return v.toBool() ? "true" : "false";
        case QVariant::Int:
        case QVariant::LongLong:
        case QVariant::UInt:
        case QVariant::ULongLong:
            return QString::number(v.toLongLong());
        case QVariant::StringList: {
            QStringList list = v.toStringList();
            QStringList quoted;
            for (int i = 0; i < list.size(); ++i)
                quoted << QString("\"%1\"").arg(jsonEscape(list.at(i)));
            return "[" + quoted.join(",") + "]";
        }
        default:
            return QString("\"%1\"").arg(jsonEscape(v.toString()));
    }
}

static QString mapToJsonSimple(const QVariantMap &args)
{
    QStringList parts;
    QMapIterator<QString, QVariant> it(args);
    while (it.hasNext()) {
        it.next();
        parts << QString("\"%1\":%2").arg(it.key(), variantToJson(it.value()));
    }
    return "{" + parts.join(",") + "}";
}

ZaloServiceProxy::ZaloServiceProxy(QObject *parent)
    : QObject(parent),
      m_localService(new ZaloService(this)),
      m_statePollTimer(new QTimer(this)),
      m_loggedIn(false),
      m_eventSocket(new QTcpSocket(this)),
      m_reconnectTimer(new QTimer(this)),
      m_invokeManager(new bb::system::InvokeManager(this)),
      m_connectFailCount(0),
      m_headlessInvokeAttempted(false)
{
    // QUAN TRỌNG: m_localService KHÔNG BAO GIỜ được gọi loadSession(), bất kỳ
    // hàm startXxxLogin/fetchXxx/sendXxx/connectWebSocket nào — nó chỉ tồn tại
    // để tái dùng các hàm đọc/ghi SQLite + filesystem cục bộ (Nhóm B, xem
    // .hpp). Nếu 1 dòng code nào trong tương lai lỡ gọi 1 hàm mạng qua
    // m_localService, UI process sẽ vô tình tự mở 1 kết nối WS thứ 2 song
    // song với HeadlessService — đúng lỗi mà toàn bộ kiến trúc này cố tránh.

    connect(m_statePollTimer, SIGNAL(timeout()), this, SLOT(onStatePollTimer()));
    m_statePollTimer->start(400); // nhanh hơn command poll của service 1 chút

    // ---- EventBridge client: kết nối tới HeadlessService, nhận sự kiện tức thời ----
    connect(m_eventSocket, SIGNAL(connected()), this, SLOT(onEventBridgeConnected()));
    connect(m_eventSocket, SIGNAL(disconnected()), this, SLOT(onEventBridgeDisconnected()));
    connect(m_eventSocket, SIGNAL(readyRead()), this, SLOT(onEventBridgeReadyRead()));
    connect(m_reconnectTimer, SIGNAL(timeout()), this, SLOT(onEventBridgeReconnectTimer()));
    m_reconnectTimer->start(2000); // thử kết nối mỗi 2s cho tới khi HeadlessService sẵn sàng
    m_eventSocket->connectToHost(QHostAddress::LocalHost, EventBridgeServer::PORT);

    refreshStateFromDb();
}

ZaloServiceProxy::~ZaloServiceProxy()
{
}

void ZaloServiceProxy::onEventBridgeConnected()
{
    qDebug() << "[ZaloServiceProxy] connected to EventBridgeServer";
    m_reconnectTimer->stop(); // đã nối được, không cần tự retry nữa — onEventBridgeDisconnected() sẽ bật lại nếu rớt
    m_connectFailCount = 0;
    m_headlessInvokeAttempted = false; // service rõ ràng đang sống — cho phép invoke lại nếu sau này rớt hẳn
    emit eventBridgeReconnected();
}

void ZaloServiceProxy::onEventBridgeDisconnected()
{
    qDebug() << "[ZaloServiceProxy] EventBridgeServer connection lost, will retry";
    m_recvBuffer.clear();
    if (!m_reconnectTimer->isActive()) m_reconnectTimer->start(2000);
}

void ZaloServiceProxy::onEventBridgeReconnectTimer()
{
    if (m_eventSocket->state() == QAbstractSocket::UnconnectedState) {
        m_connectFailCount++;
        // Sau 3 lần thử liên tiếp (~6s, xem interval start(2000)) không kết
        // nối được EventBridge, coi như HeadlessService chưa chạy — tự
        // invoke() nó lên đúng 1 lần (m_headlessInvokeAttempted chặn lặp lại
        // liên tục nếu Headless khởi động chậm/lỗi). Đây chỉ là lưới an toàn
        // bổ sung cho invoke-target bb.action.system.STARTED trong
        // bar-descriptor.xml — không thay thế, vì đó vẫn là cách chuẩn để
        // Headless tự chạy ngay từ lúc boot máy trước khi user mở UI lần nào.
        if (m_connectFailCount >= 3 && !m_headlessInvokeAttempted) {
            m_headlessInvokeAttempted = true;
            qDebug() << "[ZaloServiceProxy] EventBridge unreachable after"
                     << m_connectFailCount << "attempts — invoking headless service directly";
            bb::system::InvokeRequest req;
            req.setTarget("com.BerryLife.Zalo10.headless");
            req.setAction("bb.action.system.STARTED");
            req.setMimeType("application/vnd.blackberry.system.event.STARTED");
            m_invokeManager->invoke(req);
        }
        m_eventSocket->connectToHost(QHostAddress::LocalHost, EventBridgeServer::PORT);
    }
}

void ZaloServiceProxy::onEventBridgeReadyRead()
{
    m_recvBuffer.append(m_eventSocket->readAll());
    // Mỗi sự kiện là 1 dòng JSON kết thúc bằng '\n' (xem EventBridgeServer::
    // broadcastEvent) — TCP là stream nên 1 lần readyRead() có thể chứa 0,
    // 1, hoặc nhiều dòng ghép lại; tách từng dòng hoàn chỉnh, giữ phần dư lại
    // trong buffer chờ phần còn thiếu tới ở lần đọc sau.
    int idx;
    while ((idx = m_recvBuffer.indexOf('\n')) != -1) {
        QByteArray lineBytes = m_recvBuffer.left(idx);
        m_recvBuffer.remove(0, idx + 1);
        QString line = QString::fromUtf8(lineBytes);
        if (!line.trimmed().isEmpty()) dispatchEventLine(line);
    }
}

void ZaloServiceProxy::dispatchEventLine(const QString &jsonLine)
{
    QVariantMap envelope = jsonToMap(jsonLine);
    if (envelope.isEmpty()) {
        qDebug() << "[ZaloServiceProxy] dispatchEventLine: JSON parse failed";
        return;
    }
    QString eventName = envelope.value("event").toString();
    QVariantMap d = envelope.value("data").toMap();

    if (eventName == "newMessage") {
        emit newMessage(d.value("threadId").toString(), d.value("message").toMap());
    } else if (eventName == "messagesReady") {
        emit messagesReady(d.value("threadId").toString(), d.value("messages").toList());
    } else if (eventName == "friendsReady") {
        emit friendsReady(d.value("friends").toList());
    } else if (eventName == "conversationsReady") {
        emit conversationsReady(d.value("threads").toList());
    } else if (eventName == "invitesReady") {
        emit invitesReady(d.value("invites").toList());
    } else if (eventName == "friendRequestResponded") {
        emit friendRequestResponded(d.value("friendId").toString(), d.value("accepted").toBool(), d.value("success").toBool());
    } else if (eventName == "avatarReady") {
        emit avatarReady(d.value("threadId").toString(), d.value("localPath").toString());
    } else if (eventName == "imageMsgReady") {
        emit imageMsgReady(d.value("msgId").toString(), d.value("localPath").toString(), d.value("width").toInt(), d.value("height").toInt());
    } else if (eventName == "messageSent") {
        emit messageSent(d.value("success").toBool(), d.value("threadId").toString());
    } else if (eventName == "messageRecalled") {
        emit messageRecalled(d.value("threadId").toString(), d.value("msgId").toString());
    } else if (eventName == "messageDeletedLocally") {
        emit messageDeletedLocally(d.value("threadId").toString(), d.value("msgId").toString());
    } else if (eventName == "messageDeleted") {
        emit messageDeleted(d.value("threadId").toString(), d.value("msgId").toString(), d.value("success").toBool(), d.value("error").toString());
    } else if (eventName == "messageRecalledDone") {
        emit messageRecalledDone(d.value("threadId").toString(), d.value("msgId").toString(), d.value("success").toBool(), d.value("error").toString());
    } else if (eventName == "muteDone") {
        emit muteDone(d.value("threadId").toString(), d.value("muted").toBool(), d.value("success").toBool());
    } else if (eventName == "blockUserDone") {
        emit blockUserDone(d.value("userId").toString(), d.value("success").toBool());
    } else if (eventName == "unblockUserDone") {
        emit unblockUserDone(d.value("userId").toString(), d.value("success").toBool());
    } else if (eventName == "clearHistoryDone") {
        emit clearHistoryDone(d.value("threadId").toString(), d.value("success").toBool());
    } else if (eventName == "leaveGroupDone") {
        emit leaveGroupDone(d.value("groupId").toString(), d.value("success").toBool());
    } else if (eventName == "serverQuickMessagesReady") {
        emit serverQuickMessagesReady(d.value("imported").toInt(), d.value("skipped").toInt(), d.value("error").toString());
    } else {
        qDebug() << "[ZaloServiceProxy] dispatchEventLine: unknown event" << eventName;
    }
}

void ZaloServiceProxy::writeCommand(const QString &command, const QVariantMap &args)
{
    // Dùng CHUNG connection SQLite với m_localService (xem enqueueCommand()
    // trong ZaloService_Ipc.cpp) thay vì tự mở 1 connection riêng — 2
    // connection khác nhau trong CÙNG process UI này từng gây SQLITE_LOCKED
    // ("database is locked") dù đã bật WAL + busy_timeout, vì busy_timeout
    // chỉ retry được SQLITE_BUSY, không retry được SQLITE_LOCKED.
    QString argsJson = mapToJsonSimple(args);
    m_localService->enqueueCommand(command, argsJson);
}

void ZaloServiceProxy::refreshStateFromDb()
{
    onStatePollTimer();
}

void ZaloServiceProxy::onStatePollTimer()
{
    QMap<QString, QString> state = m_localService->readServiceState();

    bool newLoggedIn = state.value("loggedIn") == "1";
    if (newLoggedIn != m_loggedIn) {
        m_loggedIn = newLoggedIn;
        emit loggedInChanged();
        if (m_loggedIn) {
            emit loginSuccess(state.value("uid"), state.value("displayName"));
        }
    }

    QString qrPath = state.value("qrImagePath");
    QString qrRaw  = state.value("qrCodeRaw");
    if (!qrRaw.isEmpty() && (qrPath != m_lastQrImagePath || qrRaw != m_lastQrCodeRaw)) {
        m_lastQrImagePath = qrPath;
        m_lastQrCodeRaw   = qrRaw;
        emit qrCodeReady(qrPath, qrRaw);
    }

    QString sessionExpiredFlag = state.value("sessionExpired");
    if (sessionExpiredFlag == "1" && m_lastSessionExpiredFlag != "1") {
        m_lastSessionExpiredFlag = "1";
        emit sessionExpired();
    } else if (sessionExpiredFlag == "0") {
        m_lastSessionExpiredFlag = "0";
    }

    // ---- Durable fallback for friendsReady/conversationsReady/messageSent ----
    // See onPublishFriendsReady/onPublishConversationsReady/onPublishMessageSent
    // in ZaloService.hpp/.cpp for why this exists: EventBridgeServer's TCP
    // broadcast is live-only and gets silently dropped if the UI's
    // m_eventSocket isn't connected at the exact moment HeadlessService emits
    // these — most commonly right after reopening the app (HeadlessService's
    // loadSession() runs independently of the UI and may already be answering
    // fetchFriends/fetchConversations before EventBridge reconnects) or right
    // after tapping refresh a 2nd time in quick succession. Comparing "seq"
    // (a timestamp bumped on every publish) against the last one we acted on
    // means this only fires for genuinely new data, and is naturally
    // idempotent with whatever the live EventBridge path already delivered.
    QString friendsSeq = state.value("friendsReadySeq");
    if (!friendsSeq.isEmpty() && friendsSeq != m_lastFriendsReadySeq) {
        m_lastFriendsReadySeq = friendsSeq;
        QVariantMap wrap = jsonToMap(state.value("friendsReadyJson"));
        emit friendsReady(wrap.value("list").toList());
    }

    QString convoSeq = state.value("conversationsReadySeq");
    if (!convoSeq.isEmpty() && convoSeq != m_lastConversationsReadySeq) {
        m_lastConversationsReadySeq = convoSeq;
        QVariantMap wrap = jsonToMap(state.value("conversationsReadyJson"));
        emit conversationsReady(wrap.value("list").toList());
    }

    QString msgSentSeq = state.value("messageSentSeq");
    if (!msgSentSeq.isEmpty() && msgSentSeq != m_lastMessageSentSeq) {
        m_lastMessageSentSeq = msgSentSeq;
        bool success = state.value("messageSentSuccess") == "1";
        emit messageSent(success, state.value("messageSentThreadId"));
    }
}

// ---- Nhóm A: mọi hàm dưới đây CHỈ ghi command_queue, không gọi network -----

void ZaloServiceProxy::startQRLogin() { writeCommand("startQRLogin", QVariantMap()); }
void ZaloServiceProxy::retryQRLogin() { writeCommand("retryQRLogin", QVariantMap()); }
void ZaloServiceProxy::cancelQRLogin() { writeCommand("cancelQRLogin", QVariantMap()); }
void ZaloServiceProxy::logout() { writeCommand("logout", QVariantMap()); }

void ZaloServiceProxy::loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei, const QString &ua, const QString &token)
{
    QVariantMap a;
    a["zpsid"] = zpsid; a["zpwSek"] = zpwSek; a["imei"] = imei; a["ua"] = ua; a["token"] = token;
    writeCommand("loginWithCookie", a);
}

void ZaloServiceProxy::fetchConversations() { writeCommand("fetchConversations", QVariantMap()); }
void ZaloServiceProxy::fetchFriends() { writeCommand("fetchFriends", QVariantMap()); }
void ZaloServiceProxy::fetchInvites() { writeCommand("fetchInvites", QVariantMap()); }

void ZaloServiceProxy::acceptFriendRequest(const QString &friendId)
{
    QVariantMap a; a["friendId"] = friendId;
    writeCommand("acceptFriendRequest", a);
}

void ZaloServiceProxy::rejectFriendRequest(const QString &friendId)
{
    QVariantMap a; a["friendId"] = friendId;
    writeCommand("rejectFriendRequest", a);
}

void ZaloServiceProxy::fetchGroupDetails(const QStringList &groupIds)
{
    QVariantMap a; a["groupIds"] = groupIds;
    writeCommand("fetchGroupDetails", a);
}

void ZaloServiceProxy::fetchMessages(const QString &threadId, bool isGroup)
{
    QVariantMap a; a["threadId"] = threadId; a["isGroup"] = isGroup;
    writeCommand("fetchMessages", a);
}

void ZaloServiceProxy::sendMessage(const QString &threadId, const QString &content, bool isGroup)
{
    QVariantMap a; a["threadId"] = threadId; a["content"] = content; a["isGroup"] = isGroup;
    writeCommand("sendMessage", a);
}

void ZaloServiceProxy::deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                      const QString &cliMsgId, const QString &senderId, bool onlyMe)
{
    QVariantMap a;
    a["threadId"] = threadId; a["isGroup"] = isGroup; a["msgId"] = msgId;
    a["cliMsgId"] = cliMsgId; a["senderId"] = senderId; a["onlyMe"] = onlyMe;
    writeCommand("deleteMessage", a);
}

void ZaloServiceProxy::recallMessage(const QString &threadId, bool isGroup, const QString &msgId, const QString &cliMsgId)
{
    QVariantMap a;
    a["threadId"] = threadId; a["isGroup"] = isGroup; a["msgId"] = msgId; a["cliMsgId"] = cliMsgId;
    writeCommand("recallMessage", a);
}

void ZaloServiceProxy::sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption)
{
    QVariantMap a;
    a["threadId"] = threadId; a["localFilePath"] = localFilePath; a["isGroup"] = isGroup; a["caption"] = caption;
    writeCommand("sendPhoto", a);
}

void ZaloServiceProxy::sendFile(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    QVariantMap a; a["threadId"] = threadId; a["localFilePath"] = localFilePath; a["isGroup"] = isGroup;
    writeCommand("sendFile", a);
}

void ZaloServiceProxy::downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId)
{
    QVariantMap a; a["msgId"] = msgId; a["url"] = url; a["threadId"] = threadId;
    writeCommand("downloadImageMessage", a);
}

void ZaloServiceProxy::downloadAvatar(const QString &threadId, const QString &url)
{
    QVariantMap a; a["threadId"] = threadId; a["url"] = url;
    writeCommand("downloadAvatar", a);
}

void ZaloServiceProxy::setActiveThread(const QString &threadId, bool isGroup)
{
    QVariantMap a; a["threadId"] = threadId; a["isGroup"] = isGroup;
    writeCommand("setActiveThread", a);
}

void ZaloServiceProxy::clearActiveThread() { writeCommand("clearActiveThread", QVariantMap()); }

void ZaloServiceProxy::blockUser(const QString &userId)
{
    QVariantMap a; a["userId"] = userId;
    writeCommand("blockUser", a);
}

void ZaloServiceProxy::unblockUser(const QString &userId)
{
    QVariantMap a; a["userId"] = userId;
    writeCommand("unblockUser", a);
}

void ZaloServiceProxy::setMute(const QString &threadId, bool isGroup, bool mute)
{
    QVariantMap a; a["threadId"] = threadId; a["isGroup"] = isGroup; a["mute"] = mute;
    writeCommand("setMute", a);
}

void ZaloServiceProxy::clearHistory(const QString &threadId, bool isGroup)
{
    QVariantMap a; a["threadId"] = threadId; a["isGroup"] = isGroup;
    writeCommand("clearHistory", a);
}

void ZaloServiceProxy::leaveGroup(const QString &groupId)
{
    QVariantMap a; a["groupId"] = groupId;
    writeCommand("leaveGroup", a);
}

void ZaloServiceProxy::fetchServerQuickMessages() { writeCommand("fetchServerQuickMessages", QVariantMap()); }
