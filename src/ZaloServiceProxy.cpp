#include "ZaloServiceProxy.hpp"
#include "ZaloServiceUtils.hpp"

#include <QLocalSocket>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>

ZaloServiceProxy::ZaloServiceProxy(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_loggedIn(false)
    , m_callIdCounter(0)
{
    m_reconnectTimer->setInterval(RECONNECT_MS);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, SIGNAL(timeout()), this, SLOT(onReconnectTimer()));
    connect(m_socket, SIGNAL(connected()),    this, SLOT(onSocketConnected()));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
    connect(m_socket, SIGNAL(readyRead()),    this, SLOT(onSocketReadyRead()));
    connect(m_socket, SIGNAL(error(QLocalSocket::LocalSocketError)),
            this, SLOT(onSocketError(QLocalSocket::LocalSocketError)));
    connectToHeadless();
}

void ZaloServiceProxy::connectToHeadless()
{
    m_socket->connectToServer("/tmp/zalo10_ipc");
}

void ZaloServiceProxy::onSocketConnected()
{
    qDebug() << "[Proxy] connected to headless";
    m_reconnectTimer->stop();
}

void ZaloServiceProxy::onSocketDisconnected()
{
    qDebug() << "[Proxy] headless disconnected — retry in" << RECONNECT_MS << "ms";
    m_reconnectTimer->start();
}

void ZaloServiceProxy::onSocketError(QLocalSocket::LocalSocketError)
{
    if (!m_reconnectTimer->isActive()) m_reconnectTimer->start();
}

void ZaloServiceProxy::onReconnectTimer() { connectToHeadless(); }

void ZaloServiceProxy::onSocketReadyRead()
{
    m_readBuf += m_socket->readAll();
    QVariantMap msg;
    while (decodeNext(m_readBuf, msg)) handleMessage(msg);
}

void ZaloServiceProxy::handleMessage(const QVariantMap &msg)
{
    QString type = msg.value("type").toString();

    if (type == "prop") {
        if (msg.value("name").toString() == "loggedIn") {
            bool v = msg.value("value").toBool();
            if (v != m_loggedIn) { m_loggedIn = v; emit loggedInChanged(); }
        }
        return;
    }

    if (type == "result") {
        QString id = msg.value("id").toString();
        if (m_syncPending.contains(id)) {
            m_syncPending[id]->value = msg.value("value");
            m_syncPending[id]->done  = true;
        }
        return;
    }

    if (type != "signal") return;
    QString name = msg.value("name").toString();
    QVariantList a = msg.value("args").toList();

#define S(i) a.value(i).toString()
#define B(i) a.value(i).toBool()
#define I(i) a.value(i).toInt()
#define L(i) a.value(i).toList()
#define M(i) a.value(i).toMap()

    if      (name=="loginFailed")     emit loginFailed(S(0));
    else if (name=="sessionExpired")  emit sessionExpired();
    else if (name=="loginSuccess")    emit loginSuccess(S(0),S(1));
    else if (name=="sessionRefreshed")emit sessionRefreshed();
    else if (name=="qrCodeReady")     emit qrCodeReady(S(0),S(1));
    else if (name=="qrScanned")       emit qrScanned(S(0));
    else if (name=="qrExpired")       emit qrExpired();
    else if (name=="conversationsReady") emit conversationsReady(L(0));
    else if (name=="friendsReady")    emit friendsReady(L(0));
    else if (name=="invitesReady")    emit invitesReady(L(0));
    else if (name=="friendRequestResponded") emit friendRequestResponded(S(0),B(1),B(2));
    else if (name=="messagesReady")   emit messagesReady(S(0),L(1));
    else if (name=="messageSent")     emit messageSent(B(0),S(1));
    else if (name=="newMessage")      emit newMessage(S(0),M(1));
    else if (name=="messageRecalled") emit messageRecalled(S(0),S(1));
    else if (name=="threadLastMessageChanged") emit threadLastMessageChanged(S(0),S(1),S(2));
    else if (name=="avatarReady")     emit avatarReady(S(0),S(1));
    else if (name=="imageMsgReady")   emit imageMsgReady(S(0),S(1),I(2),I(3));
    else if (name=="blockUserDone")   emit blockUserDone(S(0),B(1));
    else if (name=="unblockUserDone") emit unblockUserDone(S(0),B(1));
    else if (name=="muteDone")        emit muteDone(S(0),B(1),B(2));
    else if (name=="clearHistoryDone")emit clearHistoryDone(S(0),B(1));
    else if (name=="leaveGroupDone")  emit leaveGroupDone(S(0),B(1));
    else if (name=="serverQuickMessagesReady") emit serverQuickMessagesReady(I(0),I(1),S(2));

#undef S
#undef B
#undef I
#undef L
#undef M
}

// ─── IPC helpers ─────────────────────────────────────────────────────────
QByteArray ZaloServiceProxy::encode(const QVariantMap &payload)
{
    QByteArray json = variantToJsonPretty(QVariant(payload));
    quint32 len = (quint32)json.size();
    QByteArray f(4,0);
    f[0]=(char)((len>>24)&0xFF); f[1]=(char)((len>>16)&0xFF);
    f[2]=(char)((len>> 8)&0xFF); f[3]=(char)((len    )&0xFF);
    return f+json;
}

bool ZaloServiceProxy::decodeNext(QByteArray &buf, QVariantMap &out)
{
    if (buf.size()<4) return false;
    quint32 len=((quint8)buf[0]<<24)|((quint8)buf[1]<<16)|((quint8)buf[2]<<8)|(quint8)buf[3];
    if ((quint32)buf.size()<4+len) return false;
    out = jsonToMap(buf.mid(4,len));
    buf.remove(0,4+len);
    return true;
}

void ZaloServiceProxy::sendCall(const QString &method, const QVariantList &args)
{
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        qWarning() << "[Proxy] not connected, drop call:" << method;
        return;
    }
    QVariantMap m; m["type"]="call"; m["id"]=QString::number(++m_callIdCounter);
    m["method"]=method; m["args"]=args;
    m_socket->write(encode(m));
}

QVariant ZaloServiceProxy::sendCallSync(const QString &method, const QVariantList &args, int timeoutMs)
{
    if (m_socket->state() != QLocalSocket::ConnectedState) return QVariant();
    QString id = QString::number(++m_callIdCounter);
    SyncPending pending; pending.done=false;
    m_syncPending[id] = &pending;

    QVariantMap msg; msg["type"]="call"; msg["id"]=id; msg["method"]=method; msg["args"]=args;
    m_socket->write(encode(msg)); m_socket->flush();

    QTimer t; t.setSingleShot(true); t.setInterval(timeoutMs); t.start();
    while (!pending.done && t.isActive())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    m_syncPending.remove(id);
    return pending.value;
}

// ─── Q_INVOKABLE forwarding ───────────────────────────────────────────────
void ZaloServiceProxy::startQRLogin()        { sendCall("startQRLogin",  QVariantList()); }
void ZaloServiceProxy::retryQRLogin()        { sendCall("retryQRLogin",  QVariantList()); }
void ZaloServiceProxy::cancelQRLogin()       { sendCall("cancelQRLogin", QVariantList()); }
void ZaloServiceProxy::logout()              { sendCall("logout",        QVariantList()); }
void ZaloServiceProxy::fetchConversations()  { sendCall("fetchConversations",  QVariantList()); }
void ZaloServiceProxy::fetchFriends()        { sendCall("fetchFriends",        QVariantList()); }
void ZaloServiceProxy::fetchInvites()        { sendCall("fetchInvites",        QVariantList()); }
void ZaloServiceProxy::clearActiveThread()   { sendCall("clearActiveThread",   QVariantList()); }
void ZaloServiceProxy::fetchServerQuickMessages() { sendCall("fetchServerQuickMessages", QVariantList()); }
void ZaloServiceProxy::saveSession()         { sendCall("saveSession",         QVariantList()); }
void ZaloServiceProxy::closeWebSocketGracefully() { sendCall("closeWebSocketGracefully", QVariantList()); }

void ZaloServiceProxy::loginWithCookie(const QString &a,const QString &b,const QString &c,const QString &d,const QString &e)
{ QVariantList l; l<<a<<b<<c<<d<<e; sendCall("loginWithCookie",l); }

bool ZaloServiceProxy::loadSession()
{ return sendCallSync("loadSession",QVariantList()).toBool(); }

void ZaloServiceProxy::acceptFriendRequest(const QString &id) { QVariantList l;l<<id; sendCall("acceptFriendRequest",l); }
void ZaloServiceProxy::rejectFriendRequest(const QString &id) { QVariantList l;l<<id; sendCall("rejectFriendRequest",l); }
void ZaloServiceProxy::fetchGroupDetails(const QStringList &ids) { QVariantList l;l<<QVariant(ids); sendCall("fetchGroupDetails",l); }
void ZaloServiceProxy::fetchMessages(const QString &t,bool g) { QVariantList l;l<<t<<g; sendCall("fetchMessages",l); }
void ZaloServiceProxy::sendMessage(const QString &t,const QString &c,bool g) { QVariantList l;l<<t<<c<<g; sendCall("sendMessage",l); }
void ZaloServiceProxy::sendPhoto(const QString &t,const QString &p,bool g)   { QVariantList l;l<<t<<p<<g; sendCall("sendPhoto",l); }
void ZaloServiceProxy::sendFile(const QString &t,const QString &p,bool g)    { QVariantList l;l<<t<<p<<g; sendCall("sendFile",l); }
void ZaloServiceProxy::downloadImageMessage(const QString &m,const QString &u,const QString &t) { QVariantList l;l<<m<<u<<t; sendCall("downloadImageMessage",l); }
void ZaloServiceProxy::downloadAvatar(const QString &t,const QString &u) { QVariantList l;l<<t<<u; sendCall("downloadAvatar",l); }
void ZaloServiceProxy::setActiveThread(const QString &t,bool g) { QVariantList l;l<<t<<g; sendCall("setActiveThread",l); }
void ZaloServiceProxy::blockUser(const QString &u)   { QVariantList l;l<<u; sendCall("blockUser",l); }
void ZaloServiceProxy::unblockUser(const QString &u) { QVariantList l;l<<u; sendCall("unblockUser",l); }
bool ZaloServiceProxy::isBlocked(const QString &u) const
{ ZaloServiceProxy*s=const_cast<ZaloServiceProxy*>(this); QVariantList l;l<<u; return s->sendCallSync("isBlocked",l).toBool(); }
void ZaloServiceProxy::setMute(const QString &t,bool g,bool m) { QVariantList l;l<<t<<g<<m; sendCall("setMute",l); }
bool ZaloServiceProxy::isMutedThread(const QString &t) const
{ ZaloServiceProxy*s=const_cast<ZaloServiceProxy*>(this); QVariantList l;l<<t; return s->sendCallSync("isMutedThread",l).toBool(); }
void ZaloServiceProxy::clearHistory(const QString &t,bool g) { QVariantList l;l<<t<<g; sendCall("clearHistory",l); }
void ZaloServiceProxy::leaveGroup(const QString &g) { QVariantList l;l<<g; sendCall("leaveGroup",l); }
void ZaloServiceProxy::sendHubNotification(const QString &ti,const QString &b,const QString &t,bool g)
{ QVariantList l;l<<ti<<b<<t<<g; sendCall("sendHubNotification",l); }
void ZaloServiceProxy::dbSaveMessage(const QVariantMap &m,const QString &t)
{ QVariantList l;l<<QVariant(m)<<t; sendCall("dbSaveMessage",l); }
QVariantList ZaloServiceProxy::dbLoadMessages(const QString &t)
{ QVariantList l;l<<t; return sendCallSync("dbLoadMessages",l).toList(); }
QVariantList ZaloServiceProxy::getQuickMessages() const
{ ZaloServiceProxy*s=const_cast<ZaloServiceProxy*>(this); return s->sendCallSync("getQuickMessages",QVariantList()).toList(); }
int ZaloServiceProxy::addQuickMessage(const QString &n,const QString &c)
{ QVariantList l;l<<n<<c; return sendCallSync("addQuickMessage",l).toInt(); }
bool ZaloServiceProxy::updateQuickMessage(int id,const QString &n,const QString &c)
{ QVariantList l;l<<id<<n<<c; return sendCallSync("updateQuickMessage",l).toBool(); }
bool ZaloServiceProxy::deleteQuickMessage(int id)
{ QVariantList l;l<<id; return sendCallSync("deleteQuickMessage",l).toBool(); }
QVariantMap ZaloServiceProxy::getImageDimensions(const QString &p) const
{ ZaloServiceProxy*s=const_cast<ZaloServiceProxy*>(this); QVariantList l;l<<p; return s->sendCallSync("getImageDimensions",l).toMap(); }
QVariantMap ZaloServiceProxy::exportData(const QString &d)
{ QVariantList l;l<<d; return sendCallSync("exportData",l,10000).toMap(); }
QVariantMap ZaloServiceProxy::importData(const QString &f)
{ QVariantList l;l<<f; return sendCallSync("importData",l,10000).toMap(); }
int ZaloServiceProxy::clearCache()
{ return sendCallSync("clearCache",QVariantList(),5000).toInt(); }
