#include "HeadlessService.hpp"
#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"

#include <QLocalServer>
#include <QLocalSocket>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QDebug>

const char *HeadlessService::SOCKET_PATH = "/tmp/zalo10_ipc";

// ─── Constructor ──────────────────────────────────────────────────────────
HeadlessService::HeadlessService(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
    , m_zService(new ZaloService(this))
{
    QLocalServer::removeServer(QString::fromLatin1(SOCKET_PATH));

    if (!m_server->listen(QString::fromLatin1(SOCKET_PATH)))
        qWarning() << "[HS] listen failed:" << m_server->errorString();
    else
        qDebug() << "[HS] listening on" << SOCKET_PATH;

    connect(m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    connect(m_zService, SIGNAL(loggedInChanged()),                    this, SLOT(onLoggedInChanged()));
    connect(m_zService, SIGNAL(loginFailed(QString)),                 this, SLOT(onLoginFailed(QString)));
    connect(m_zService, SIGNAL(sessionExpired()),                     this, SLOT(onSessionExpired()));
    connect(m_zService, SIGNAL(loginSuccess(QString,QString)),        this, SLOT(onLoginSuccess(QString,QString)));
    connect(m_zService, SIGNAL(sessionRefreshed()),                   this, SLOT(onSessionRefreshed()));
    connect(m_zService, SIGNAL(qrCodeReady(QString,QString)),         this, SLOT(onQrCodeReady(QString,QString)));
    connect(m_zService, SIGNAL(qrScanned(QString)),                   this, SLOT(onQrScanned(QString)));
    connect(m_zService, SIGNAL(qrExpired()),                          this, SLOT(onQrExpired()));
    connect(m_zService, SIGNAL(conversationsReady(QVariantList)),     this, SLOT(onConversationsReady(QVariantList)));
    connect(m_zService, SIGNAL(friendsReady(QVariantList)),           this, SLOT(onFriendsReady(QVariantList)));
    connect(m_zService, SIGNAL(invitesReady(QVariantList)),           this, SLOT(onInvitesReady(QVariantList)));
    connect(m_zService, SIGNAL(friendRequestResponded(QString,bool,bool)), this, SLOT(onFriendRequestResponded(QString,bool,bool)));
    connect(m_zService, SIGNAL(messagesReady(QString,QVariantList)),  this, SLOT(onMessagesReady(QString,QVariantList)));
    connect(m_zService, SIGNAL(messageSent(bool,QString)),            this, SLOT(onMessageSent(bool,QString)));
    connect(m_zService, SIGNAL(newMessage(QString,QVariantMap)),      this, SLOT(onNewMessage(QString,QVariantMap)));
    connect(m_zService, SIGNAL(messageRecalled(QString,QString)),     this, SLOT(onMessageRecalled(QString,QString)));
    connect(m_zService, SIGNAL(threadLastMessageChanged(QString,QString,QString)), this, SLOT(onThreadLastMessageChanged(QString,QString,QString)));
    connect(m_zService, SIGNAL(avatarReady(QString,QString)),         this, SLOT(onAvatarReady(QString,QString)));
    connect(m_zService, SIGNAL(imageMsgReady(QString,QString,int,int)),this, SLOT(onImageMsgReady(QString,QString,int,int)));
    connect(m_zService, SIGNAL(blockUserDone(QString,bool)),          this, SLOT(onBlockUserDone(QString,bool)));
    connect(m_zService, SIGNAL(unblockUserDone(QString,bool)),        this, SLOT(onUnblockUserDone(QString,bool)));
    connect(m_zService, SIGNAL(muteDone(QString,bool,bool)),          this, SLOT(onMuteDone(QString,bool,bool)));
    connect(m_zService, SIGNAL(clearHistoryDone(QString,bool)),       this, SLOT(onClearHistoryDone(QString,bool)));
    connect(m_zService, SIGNAL(leaveGroupDone(QString,bool)),         this, SLOT(onLeaveGroupDone(QString,bool)));
    connect(m_zService, SIGNAL(serverQuickMessagesReady(int,int,QString)), this, SLOT(onServerQuickMessagesReady(int,int,QString)));

    // Tự load session khi headless khởi động
    bool loaded = m_zService->loadSession();
    qDebug() << "[HS] startup loadSession:" << (loaded ? "OK — session restored" : "no saved session, will wait for UI to login");
}

// ─── IPC server ───────────────────────────────────────────────────────────
void HeadlessService::onNewConnection()
{
    QLocalSocket *c = m_server->nextPendingConnection();
    if (!c) return;
    m_clients.append(c);
    m_readBufs[c] = QByteArray();
    connect(c, SIGNAL(readyRead()),    this, SLOT(onClientReadyRead()));
    connect(c, SIGNAL(disconnected()), this, SLOT(onClientDisconnected()));
    qDebug() << "[HS] UI connected, clients:" << m_clients.size();
    sendWelcomeState(c);
}

void HeadlessService::sendWelcomeState(QLocalSocket *c)
{
    // Gửi trạng thái loggedIn ngay để UI không phải đợi signal
    QVariantMap m; m["type"]="prop"; m["name"]="loggedIn"; m["value"]=m_zService->loggedIn();
    send(c, m);
}

void HeadlessService::onClientReadyRead()
{
    QLocalSocket *c = qobject_cast<QLocalSocket*>(sender());
    if (!c) return;
    m_readBufs[c] += c->readAll();
    QVariantMap msg;
    while (decodeNext(m_readBufs[c], msg))
        handleMessage(c, msg);
}

void HeadlessService::onClientDisconnected()
{
    QLocalSocket *c = qobject_cast<QLocalSocket*>(sender());
    if (!c) return;
    qDebug() << "[HS] UI disconnected — WebSocket stays alive";
    m_clients.removeAll(c);
    m_readBufs.remove(c);
    c->deleteLater();
}

// ─── Dispatch call ────────────────────────────────────────────────────────
void HeadlessService::handleMessage(QLocalSocket *client, const QVariantMap &msg)
{
    if (msg.value("type").toString() != "call") return;
    QString method = msg.value("method").toString();
    QString id     = msg.value("id").toString();
    QVariantList a = msg.value("args").toList();

    QVariantMap res; res["type"]="result"; res["id"]=id; res["value"]=QVariant();

#define S(i)  a.value(i).toString()
#define B(i)  a.value(i).toBool()
#define I(i)  a.value(i).toInt()
#define SL(i) a.value(i).toStringList()

    if      (method=="startQRLogin")         m_zService->startQRLogin();
    else if (method=="retryQRLogin")         m_zService->retryQRLogin();
    else if (method=="cancelQRLogin")        m_zService->cancelQRLogin();
    else if (method=="logout")               m_zService->logout();
    else if (method=="loginWithCookie")      m_zService->loginWithCookie(S(0),S(1),S(2),S(3),S(4));
    else if (method=="loadSession")          { res["value"]=m_zService->loadSession(); send(client,res); return; }
    else if (method=="saveSession")          m_zService->saveSession();
    else if (method=="closeWebSocketGracefully") { /* UI tắt → headless giữ WS sống, bỏ qua */ }
    else if (method=="fetchConversations")   m_zService->fetchConversations();
    else if (method=="fetchFriends")         m_zService->fetchFriends();
    else if (method=="fetchInvites")         m_zService->fetchInvites();
    else if (method=="acceptFriendRequest")  m_zService->acceptFriendRequest(S(0));
    else if (method=="rejectFriendRequest")  m_zService->rejectFriendRequest(S(0));
    else if (method=="fetchGroupDetails")    m_zService->fetchGroupDetails(SL(0));
    else if (method=="fetchMessages")        m_zService->fetchMessages(S(0),B(1));
    else if (method=="sendMessage")          m_zService->sendMessage(S(0),S(1),B(2));
    else if (method=="sendPhoto")            m_zService->sendPhoto(S(0),S(1),B(2));
    else if (method=="sendFile")             m_zService->sendFile(S(0),S(1),B(2));
    else if (method=="downloadImageMessage") m_zService->downloadImageMessage(S(0),S(1),S(2));
    else if (method=="downloadAvatar")       m_zService->downloadAvatar(S(0),S(1));
    else if (method=="setActiveThread")      m_zService->setActiveThread(S(0),B(1));
    else if (method=="clearActiveThread")    m_zService->clearActiveThread();
    else if (method=="blockUser")            m_zService->blockUser(S(0));
    else if (method=="unblockUser")          m_zService->unblockUser(S(0));
    else if (method=="isBlocked")            { res["value"]=m_zService->isBlocked(S(0)); send(client,res); return; }
    else if (method=="setMute")              m_zService->setMute(S(0),B(1),B(2));
    else if (method=="isMutedThread")        { res["value"]=m_zService->isMutedThread(S(0)); send(client,res); return; }
    else if (method=="clearHistory")         m_zService->clearHistory(S(0),B(1));
    else if (method=="leaveGroup")           m_zService->leaveGroup(S(0));
    else if (method=="sendHubNotification")  m_zService->sendHubNotification(S(0),S(1),S(2),B(3));
    else if (method=="dbSaveMessage")        m_zService->dbSaveMessage(a.value(0).toMap(),S(1));
    else if (method=="dbLoadMessages")       { res["value"]=QVariant(m_zService->dbLoadMessages(S(0))); send(client,res); return; }
    else if (method=="getQuickMessages")     { res["value"]=QVariant(m_zService->getQuickMessages()); send(client,res); return; }
    else if (method=="addQuickMessage")      { res["value"]=m_zService->addQuickMessage(S(0),S(1)); send(client,res); return; }
    else if (method=="updateQuickMessage")   { res["value"]=m_zService->updateQuickMessage(I(0),S(1),S(2)); send(client,res); return; }
    else if (method=="deleteQuickMessage")   { res["value"]=m_zService->deleteQuickMessage(I(0)); send(client,res); return; }
    else if (method=="fetchServerQuickMessages") m_zService->fetchServerQuickMessages();
    else if (method=="getImageDimensions")   { res["value"]=m_zService->getImageDimensions(S(0)); send(client,res); return; }
    else if (method=="exportData")           { res["value"]=m_zService->exportData(S(0)); send(client,res); return; }
    else if (method=="importData")           { res["value"]=m_zService->importData(S(0)); send(client,res); return; }
    else if (method=="clearCache")           { res["value"]=m_zService->clearCache(); send(client,res); return; }
    else qWarning() << "[HS] unknown method:" << method;

#undef S
#undef B
#undef I
#undef SL

    send(client, res);
}

// ─── Signal forwarding ────────────────────────────────────────────────────
#define SIG0(name) \
void HeadlessService::on##name() \
{ QVariantMap m; m["type"]="signal"; m["name"]=#name; m["args"]=QVariantList(); broadcast(m); }

#define SIG1(name,t0,a0) \
void HeadlessService::on##name(t0 a0) \
{ QVariantList a; a<<a0; QVariantMap m; m["type"]="signal"; m["name"]=#name; m["args"]=a; broadcast(m); }

#define SIG2(name,t0,a0,t1,a1) \
void HeadlessService::on##name(t0 a0,t1 a1) \
{ QVariantList a; a<<a0<<a1; QVariantMap m; m["type"]="signal"; m["name"]=#name; m["args"]=a; broadcast(m); }

#define SIG3(name,t0,a0,t1,a1,t2,a2) \
void HeadlessService::on##name(t0 a0,t1 a1,t2 a2) \
{ QVariantList a; a<<a0<<a1<<a2; QVariantMap m; m["type"]="signal"; m["name"]=#name; m["args"]=a; broadcast(m); }

void HeadlessService::onLoggedInChanged()
{ QVariantMap m; m["type"]="prop"; m["name"]="loggedIn"; m["value"]=m_zService->loggedIn(); broadcast(m); }

SIG1(LoginFailed,           const QString&, message)
SIG0(SessionExpired)
SIG2(LoginSuccess,          const QString&, uid,       const QString&, displayName)
SIG0(SessionRefreshed)
SIG2(QrCodeReady,           const QString&, imagePath, const QString&, qrCode)
SIG1(QrScanned,             const QString&, displayName)
SIG0(QrExpired)

void HeadlessService::onConversationsReady(const QVariantList &threads)
{ QVariantList a; a<<QVariant(threads); QVariantMap m; m["type"]="signal"; m["name"]="conversationsReady"; m["args"]=a; broadcast(m); }
void HeadlessService::onFriendsReady(const QVariantList &friends)
{ QVariantList a; a<<QVariant(friends); QVariantMap m; m["type"]="signal"; m["name"]="friendsReady"; m["args"]=a; broadcast(m); }
void HeadlessService::onInvitesReady(const QVariantList &invites)
{ QVariantList a; a<<QVariant(invites); QVariantMap m; m["type"]="signal"; m["name"]="invitesReady"; m["args"]=a; broadcast(m); }

SIG3(FriendRequestResponded,const QString&, friendId,  bool, accepted, bool, success)

void HeadlessService::onMessagesReady(const QString &threadId, const QVariantList &messages)
{ QVariantList a; a<<threadId<<QVariant(messages); QVariantMap m; m["type"]="signal"; m["name"]="messagesReady"; m["args"]=a; broadcast(m); }
void HeadlessService::onNewMessage(const QString &threadId, const QVariantMap &message)
{ QVariantList a; a<<threadId<<QVariant(message); QVariantMap m; m["type"]="signal"; m["name"]="newMessage"; m["args"]=a; broadcast(m); }

SIG2(MessageSent,           bool, success,  const QString&, threadId)
SIG2(MessageRecalled,       const QString&, threadId, const QString&, msgId)
SIG3(ThreadLastMessageChanged, const QString&, threadId, const QString&, lastMsg, const QString&, lastTime)
SIG2(AvatarReady,           const QString&, threadId, const QString&, localPath)

void HeadlessService::onImageMsgReady(const QString &msgId, const QString &localPath, int width, int height)
{ QVariantList a; a<<msgId<<localPath<<width<<height; QVariantMap m; m["type"]="signal"; m["name"]="imageMsgReady"; m["args"]=a; broadcast(m); }

SIG2(BlockUserDone,         const QString&, userId,   bool, success)
SIG2(UnblockUserDone,       const QString&, userId,   bool, success)
SIG3(MuteDone,              const QString&, threadId, bool, muted, bool, success)
SIG2(ClearHistoryDone,      const QString&, threadId, bool, success)
SIG2(LeaveGroupDone,        const QString&, groupId,  bool, success)

void HeadlessService::onServerQuickMessagesReady(int imported, int skipped, const QString &error)
{ QVariantList a; a<<imported<<skipped<<error; QVariantMap m; m["type"]="signal"; m["name"]="serverQuickMessagesReady"; m["args"]=a; broadcast(m); }

#undef SIG0
#undef SIG1
#undef SIG2
#undef SIG3

// ─── Low-level IPC ────────────────────────────────────────────────────────
QByteArray HeadlessService::encode(const QVariantMap &payload)
{
    QByteArray json = variantToJsonPretty(QVariant(payload));
    quint32 len = (quint32)json.size();
    QByteArray frame(4, 0);
    frame[0]=(char)((len>>24)&0xFF); frame[1]=(char)((len>>16)&0xFF);
    frame[2]=(char)((len>> 8)&0xFF); frame[3]=(char)((len    )&0xFF);
    return frame + json;
}

bool HeadlessService::decodeNext(QByteArray &buf, QVariantMap &out)
{
    if (buf.size() < 4) return false;
    quint32 len = ((quint8)buf[0]<<24)|((quint8)buf[1]<<16)|((quint8)buf[2]<<8)|(quint8)buf[3];
    if ((quint32)buf.size() < 4+len) return false;
    out = jsonToMap(buf.mid(4, len));
    buf.remove(0, 4+len);
    return true;
}

void HeadlessService::send(QLocalSocket *c, const QVariantMap &payload)
{
    if (c && c->state()==QLocalSocket::ConnectedState) c->write(encode(payload));
}

void HeadlessService::broadcast(const QVariantMap &payload)
{
    QByteArray f = encode(payload);
    foreach (QLocalSocket *c, m_clients)
        if (c && c->state()==QLocalSocket::ConnectedState) c->write(f);
}
