#include "ZaloServiceProxy.hpp"
#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"

#include <QLocalSocket>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>
#include <QDebug>

ZaloServiceProxy::ZaloServiceProxy(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_reconnectTimer(new QTimer(this))
    , m_fallbackTimer(new QTimer(this))
    , m_directService(0)
    , m_loggedIn(false)
    , m_callIdCounter(0)
    , m_welcomeReceived(false)
{
    m_reconnectTimer->setInterval(RECONNECT_MS);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, SIGNAL(timeout()), this, SLOT(onReconnectTimer()));

    m_fallbackTimer->setInterval(FALLBACK_MS);
    m_fallbackTimer->setSingleShot(true);
    connect(m_fallbackTimer, SIGNAL(timeout()), this, SLOT(onFallbackTimer()));

    connect(m_socket, SIGNAL(connected()),    this, SLOT(onSocketConnected()));
    connect(m_socket, SIGNAL(disconnected()), this, SLOT(onSocketDisconnected()));
    connect(m_socket, SIGNAL(readyRead()),    this, SLOT(onSocketReadyRead()));
    connect(m_socket, SIGNAL(error(QLocalSocket::LocalSocketError)),
            this, SLOT(onSocketError(QLocalSocket::LocalSocketError)));

    m_fallbackTimer->start();
    connectToHeadless();
}

// ─── IPC connection management ────────────────────────────────────────────

void ZaloServiceProxy::connectToHeadless()
{
    if (m_directService) return; // already in direct mode, don't bother
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        m_socket->abort();
    QString path = QDir::homePath() + "/zalo10_ipc.sock";
    qDebug() << "[Proxy] connectToHeadless:" << path;
    m_socket->connectToServer(path);
}

void ZaloServiceProxy::onSocketConnected()
{
    qDebug() << "[Proxy] connected to headless — cancelling fallback, flushing"
             << m_pendingCalls.size() << "queued calls";
    m_reconnectTimer->stop();
    m_fallbackTimer->stop(); // headless is alive — no need for direct fallback
    m_welcomeReceived = false;
    QList<QPair<QString,QVariantList> > pending = m_pendingCalls;
    m_pendingCalls.clear();
    for (int i = 0; i < pending.size(); ++i)
        sendCall(pending[i].first, pending[i].second);
}

void ZaloServiceProxy::onSocketDisconnected()
{
    qDebug() << "[Proxy] headless disconnected — retry in" << RECONNECT_MS << "ms";
    m_welcomeReceived = false;
    m_reconnectTimer->start();
}

void ZaloServiceProxy::onSocketError(QLocalSocket::LocalSocketError err)
{
    qDebug() << "[Proxy] socket error:" << (int)err << "— retry in" << RECONNECT_MS << "ms";
    if (!m_reconnectTimer->isActive()) m_reconnectTimer->start();
}

void ZaloServiceProxy::onReconnectTimer()
{
    qDebug() << "[Proxy] reconnect timer fired";
    connectToHeadless();
}

// ─── Fallback: direct ZaloService in UI process ───────────────────────────

void ZaloServiceProxy::onFallbackTimer()
{
    qDebug() << "[Proxy] headless did not connect in" << FALLBACK_MS
             << "ms — switching to direct ZaloService (debug/unsigned build)";
    m_reconnectTimer->stop();
    m_socket->abort();

    m_directService = new ZaloService(this);

    // Connect all ZaloService signals → forward to proxy signals.
    // loggedInChanged needs to also sync m_loggedIn → use a dedicated slot.
    connect(m_directService, SIGNAL(loggedInChanged()),
            this, SLOT(onDirectLoggedInChanged()));
    connect(m_directService, SIGNAL(loginFailed(QString)),
            this, SIGNAL(loginFailed(QString)));
    connect(m_directService, SIGNAL(sessionExpired()),
            this, SIGNAL(sessionExpired()));
    connect(m_directService, SIGNAL(loginSuccess(QString,QString)),
            this, SIGNAL(loginSuccess(QString,QString)));
    connect(m_directService, SIGNAL(sessionRefreshed()),
            this, SIGNAL(sessionRefreshed()));
    connect(m_directService, SIGNAL(qrCodeReady(QString,QString)),
            this, SIGNAL(qrCodeReady(QString,QString)));
    connect(m_directService, SIGNAL(qrScanned(QString)),
            this, SIGNAL(qrScanned(QString)));
    connect(m_directService, SIGNAL(qrExpired()),
            this, SIGNAL(qrExpired()));
    connect(m_directService, SIGNAL(conversationsReady(QVariantList)),
            this, SIGNAL(conversationsReady(QVariantList)));
    connect(m_directService, SIGNAL(friendsReady(QVariantList)),
            this, SIGNAL(friendsReady(QVariantList)));
    connect(m_directService, SIGNAL(invitesReady(QVariantList)),
            this, SIGNAL(invitesReady(QVariantList)));
    connect(m_directService, SIGNAL(friendRequestResponded(QString,bool,bool)),
            this, SIGNAL(friendRequestResponded(QString,bool,bool)));
    connect(m_directService, SIGNAL(messagesReady(QString,QVariantList)),
            this, SIGNAL(messagesReady(QString,QVariantList)));
    connect(m_directService, SIGNAL(messageSent(bool,QString)),
            this, SIGNAL(messageSent(bool,QString)));
    connect(m_directService, SIGNAL(newMessage(QString,QVariantMap)),
            this, SIGNAL(newMessage(QString,QVariantMap)));
    connect(m_directService, SIGNAL(messageRecalled(QString,QString)),
            this, SIGNAL(messageRecalled(QString,QString)));
    connect(m_directService, SIGNAL(threadLastMessageChanged(QString,QString,QString)),
            this, SIGNAL(threadLastMessageChanged(QString,QString,QString)));
    connect(m_directService, SIGNAL(avatarReady(QString,QString)),
            this, SIGNAL(avatarReady(QString,QString)));
    connect(m_directService, SIGNAL(imageMsgReady(QString,QString,int,int)),
            this, SIGNAL(imageMsgReady(QString,QString,int,int)));
    connect(m_directService, SIGNAL(blockUserDone(QString,bool)),
            this, SIGNAL(blockUserDone(QString,bool)));
    connect(m_directService, SIGNAL(unblockUserDone(QString,bool)),
            this, SIGNAL(unblockUserDone(QString,bool)));
    connect(m_directService, SIGNAL(muteDone(QString,bool,bool)),
            this, SIGNAL(muteDone(QString,bool,bool)));
    connect(m_directService, SIGNAL(clearHistoryDone(QString,bool)),
            this, SIGNAL(clearHistoryDone(QString,bool)));
    connect(m_directService, SIGNAL(leaveGroupDone(QString,bool)),
            this, SIGNAL(leaveGroupDone(QString,bool)));
    connect(m_directService, SIGNAL(serverQuickMessagesReady(int,int,QString)),
            this, SIGNAL(serverQuickMessagesReady(int,int,QString)));

    // loadSession() is ASYNC: it reads cookies from QSettings and kicks off
    // refreshSessionKey() (HTTP). loggedIn() is still false at this point.
    // Use the RETURN VALUE (session data found) not loggedIn() to decide
    // whether to show QR or wait for the async restore to complete.
    bool sessionExists = m_directService->loadSession();

    if (!sessionExists) {
        // No saved session → show QR
        qDebug() << "[Proxy] direct mode: no session — emitting headlessReadyNotLoggedIn";
        emit headlessReadyNotLoggedIn();
        // Flush pending calls (startQRLogin queued while waiting for headless)
        QList<QPair<QString,QVariantList> > pending = m_pendingCalls;
        m_pendingCalls.clear();
        for (int i = 0; i < pending.size(); ++i)
            dispatchDirect(pending[i].first, pending[i].second);
    } else {
        // Session data found — refreshSessionKey() is running async.
        // loginSuccess (or sessionRefreshed) will arrive shortly and close the
        // login sheet. Discard any queued startQRLogin so QR doesn't flash.
        qDebug() << "[Proxy] direct mode: session found, waiting for async refresh";
        m_pendingCalls.clear();
    }
}

void ZaloServiceProxy::onDirectLoggedInChanged()
{
    if (!m_directService) return;
    bool v = m_directService->loggedIn();
    if (v != m_loggedIn) {
        m_loggedIn = v;
        emit loggedInChanged();
    }
}

// ─── Direct-mode dispatch ─────────────────────────────────────────────────

void ZaloServiceProxy::dispatchDirect(const QString &method, const QVariantList &a)
{
    ZaloService *s = m_directService;
    if (!s) return;

    if      (method=="startQRLogin")    s->startQRLogin();
    else if (method=="retryQRLogin")    s->retryQRLogin();
    else if (method=="cancelQRLogin")   s->cancelQRLogin();
    else if (method=="logout")          s->logout();
    else if (method=="fetchConversations")      s->fetchConversations();
    else if (method=="fetchFriends")            s->fetchFriends();
    else if (method=="fetchInvites")            s->fetchInvites();
    else if (method=="clearActiveThread")       s->clearActiveThread();
    else if (method=="fetchServerQuickMessages")s->fetchServerQuickMessages();
    else if (method=="saveSession")             s->saveSession();
    else if (method=="closeWebSocketGracefully")s->closeWebSocketGracefully();
    else if (method=="loginWithCookie")
        s->loginWithCookie(a.value(0).toString(),a.value(1).toString(),
                           a.value(2).toString(),a.value(3).toString(),a.value(4).toString());
    else if (method=="acceptFriendRequest") s->acceptFriendRequest(a.value(0).toString());
    else if (method=="rejectFriendRequest") s->rejectFriendRequest(a.value(0).toString());
    else if (method=="fetchGroupDetails") {
        QStringList ids;
        QVariantList vl = a.value(0).toList();
        for (int i=0;i<vl.size();++i) ids<<vl[i].toString();
        s->fetchGroupDetails(ids);
    }
    else if (method=="fetchMessages")
        s->fetchMessages(a.value(0).toString(),a.value(1).toBool());
    else if (method=="sendMessage")
        s->sendMessage(a.value(0).toString(),a.value(1).toString(),a.value(2).toBool());
    else if (method=="sendPhoto")
        s->sendPhoto(a.value(0).toString(),a.value(1).toString(),a.value(2).toBool());
    else if (method=="sendFile")
        s->sendFile(a.value(0).toString(),a.value(1).toString(),a.value(2).toBool());
    else if (method=="downloadImageMessage")
        s->downloadImageMessage(a.value(0).toString(),a.value(1).toString(),a.value(2).toString());
    else if (method=="downloadAvatar")
        s->downloadAvatar(a.value(0).toString(),a.value(1).toString());
    else if (method=="setActiveThread")
        s->setActiveThread(a.value(0).toString(),a.value(1).toBool());
    else if (method=="blockUser")   s->blockUser(a.value(0).toString());
    else if (method=="unblockUser") s->unblockUser(a.value(0).toString());
    else if (method=="setMute")
        s->setMute(a.value(0).toString(),a.value(1).toBool(),a.value(2).toBool());
    else if (method=="clearHistory")
        s->clearHistory(a.value(0).toString(),a.value(1).toBool());
    else if (method=="leaveGroup")  s->leaveGroup(a.value(0).toString());
    else if (method=="sendHubNotification")
        s->sendHubNotification(a.value(0).toString(),a.value(1).toString(),
                               a.value(2).toString(),a.value(3).toBool());
    else if (method=="dbSaveMessage")
        s->dbSaveMessage(a.value(0).toMap(),a.value(1).toString());
    else qDebug() << "[Proxy] dispatchDirect: unknown method" << method;
}

QVariant ZaloServiceProxy::dispatchDirectSync(const QString &method, const QVariantList &a)
{
    ZaloService *s = m_directService;
    if (!s) return QVariant();

    if      (method=="loadSession")         return s->loadSession();
    else if (method=="isBlocked")           return s->isBlocked(a.value(0).toString());
    else if (method=="isMutedThread")       return s->isMutedThread(a.value(0).toString());
    else if (method=="dbLoadMessages")      return QVariant(s->dbLoadMessages(a.value(0).toString()));
    else if (method=="getQuickMessages")    return QVariant(s->getQuickMessages());
    else if (method=="addQuickMessage")
        return s->addQuickMessage(a.value(0).toString(),a.value(1).toString());
    else if (method=="updateQuickMessage")
        return s->updateQuickMessage(a.value(0).toInt(),a.value(1).toString(),a.value(2).toString());
    else if (method=="deleteQuickMessage")  return s->deleteQuickMessage(a.value(0).toInt());
    else if (method=="getImageDimensions")  return QVariant(s->getImageDimensions(a.value(0).toString()));
    else if (method=="exportData")          return QVariant(s->exportData(a.value(0).toString()));
    else if (method=="importData")          return QVariant(s->importData(a.value(0).toString()));
    else if (method=="clearCache")          return s->clearCache();

    qDebug() << "[Proxy] dispatchDirectSync: unknown method" << method;
    return QVariant();
}

// ─── IPC message parsing ─────────────────────────────────────────────────

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
            bool changed = (v != m_loggedIn);
            m_loggedIn = v;
            if (changed) emit loggedInChanged();
            if (!m_welcomeReceived) {
                m_welcomeReceived = true;
                if (!m_loggedIn) {
                    qDebug() << "[Proxy] headless ready, not logged in — emitting headlessReadyNotLoggedIn";
                    emit headlessReadyNotLoggedIn();
                }
            }
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
    if (m_directService) { dispatchDirect(method, args); return; }
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        qDebug() << "[Proxy] not connected, queuing:" << method;
        m_pendingCalls.append(qMakePair(method, args));
        return;
    }
    QVariantMap m; m["type"]="call"; m["id"]=QString::number(++m_callIdCounter);
    m["method"]=method; m["args"]=args;
    m_socket->write(encode(m));
}

QVariant ZaloServiceProxy::sendCallSync(const QString &method, const QVariantList &args, int timeoutMs)
{
    if (m_directService) return dispatchDirectSync(method, args);
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
