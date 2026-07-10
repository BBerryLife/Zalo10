#include "EventBridgeServer.hpp"
#include <QTcpSocket>
#include <QHostAddress>
#include <QDebug>

// ---------------------------------------------------------------------------
// Serialize QVariantMap -> JSON thủ công (không QScriptEngine, không cần parse
// ở phía server — chỉ cần build chuỗi hợp lệ để gửi đi). Hỗ trợ các kiểu giá
// trị thực tế xuất hiện trong payload các signal của ZaloService: String,
// Bool, Int, QVariantList (danh sách message/friend/thread — mỗi phần tử lại
// là QVariantMap), QVariantMap lồng nhau.
// ---------------------------------------------------------------------------

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

static QString variantToJson(const QVariant &v);

static QString listToJson(const QVariantList &list)
{
    QStringList parts;
    for (int i = 0; i < list.size(); ++i)
        parts << variantToJson(list.at(i));
    return "[" + parts.join(",") + "]";
}

static QString mapToJson(const QVariantMap &map)
{
    QStringList parts;
    QMapIterator<QString, QVariant> it(map);
    while (it.hasNext()) {
        it.next();
        parts << QString("\"%1\":%2").arg(it.key(), variantToJson(it.value()));
    }
    return "{" + parts.join(",") + "}";
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
        case QVariant::Double:
            return v.toString();
        case QVariant::List:
            return listToJson(v.toList());
        case QVariant::Map:
            return mapToJson(v.toMap());
        default:
            return QString("\"%1\"").arg(jsonEscape(v.toString()));
    }
}

EventBridgeServer::EventBridgeServer(ZaloService *service, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this))
{
    if (!m_server->listen(QHostAddress::LocalHost, PORT)) {
        qDebug() << "[EventBridge] FAILED to listen on port" << PORT << "-" << m_server->errorString();
    } else {
        qDebug() << "[EventBridge] listening on 127.0.0.1:" << PORT;
    }
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onNewConnection()));

    connect(service, SIGNAL(newMessage(QString,QVariantMap)), this, SLOT(onNewMessage(QString,QVariantMap)));
    connect(service, SIGNAL(messagesReady(QString,QVariantList)), this, SLOT(onMessagesReady(QString,QVariantList)));
    connect(service, SIGNAL(friendsReady(QVariantList)), this, SLOT(onFriendsReady(QVariantList)));
    connect(service, SIGNAL(conversationsReady(QVariantList)), this, SLOT(onConversationsReady(QVariantList)));
    connect(service, SIGNAL(invitesReady(QVariantList)), this, SLOT(onInvitesReady(QVariantList)));
    connect(service, SIGNAL(friendRequestResponded(QString,bool,bool)), this, SLOT(onFriendRequestResponded(QString,bool,bool)));
    connect(service, SIGNAL(avatarReady(QString,QString)), this, SLOT(onAvatarReady(QString,QString)));
    connect(service, SIGNAL(imageMsgReady(QString,QString,int,int)), this, SLOT(onImageMsgReady(QString,QString,int,int)));
    connect(service, SIGNAL(messageSent(bool,QString)), this, SLOT(onMessageSent(bool,QString)));
    connect(service, SIGNAL(messageRecalled(QString,QString)), this, SLOT(onMessageRecalled(QString,QString)));
    connect(service, SIGNAL(messageDeletedLocally(QString,QString)), this, SLOT(onMessageDeletedLocally(QString,QString)));
    connect(service, SIGNAL(messageDeleted(QString,QString,bool,QString)), this, SLOT(onMessageDeleted(QString,QString,bool,QString)));
    connect(service, SIGNAL(messageRecalledDone(QString,QString,bool,QString)), this, SLOT(onMessageRecalledDone(QString,QString,bool,QString)));
    connect(service, SIGNAL(muteDone(QString,bool,bool)), this, SLOT(onMuteDone(QString,bool,bool)));
    connect(service, SIGNAL(blockUserDone(QString,bool)), this, SLOT(onBlockUserDone(QString,bool)));
    connect(service, SIGNAL(unblockUserDone(QString,bool)), this, SLOT(onUnblockUserDone(QString,bool)));
    connect(service, SIGNAL(clearHistoryDone(QString,bool)), this, SLOT(onClearHistoryDone(QString,bool)));
    connect(service, SIGNAL(leaveGroupDone(QString,bool)), this, SLOT(onLeaveGroupDone(QString,bool)));
    connect(service, SIGNAL(serverQuickMessagesReady(int,int,QString)), this, SLOT(onServerQuickMessagesReady(int,int,QString)));
}

void EventBridgeServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        m_clients.append(client);
        connect(client, SIGNAL(disconnected()), this, SLOT(onClientDisconnected()));
        qDebug() << "[EventBridge] UI client connected, total:" << m_clients.size();
    }
}

void EventBridgeServer::onClientDisconnected()
{
    QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;
    m_clients.removeAll(client);
    client->deleteLater();
    qDebug() << "[EventBridge] UI client disconnected, total:" << m_clients.size();
}

void EventBridgeServer::broadcastEvent(const QString &eventName, const QVariantMap &payload)
{
    if (m_clients.isEmpty()) return; // không ai đang nghe (UI đóng) — bỏ qua, dữ liệu gốc đã an toàn trong SQLite

    QVariantMap envelope;
    envelope["event"] = eventName;
    envelope["data"]  = payload;
    QString line = mapToJson(envelope) + "\n"; // '\n' làm delimiter cho stream TCP
    QByteArray bytes = line.toUtf8();

    for (int i = 0; i < m_clients.size(); ++i) {
        m_clients.at(i)->write(bytes);
    }
}

void EventBridgeServer::onNewMessage(const QString &threadId, const QVariantMap &message)
{
    QVariantMap p; p["threadId"] = threadId; p["message"] = message;
    broadcastEvent("newMessage", p);
}

void EventBridgeServer::onMessagesReady(const QString &threadId, const QVariantList &messages)
{
    QVariantMap p; p["threadId"] = threadId; p["messages"] = messages;
    broadcastEvent("messagesReady", p);
}

void EventBridgeServer::onFriendsReady(const QVariantList &friends)
{
    QVariantMap p; p["friends"] = friends;
    broadcastEvent("friendsReady", p);
}

void EventBridgeServer::onConversationsReady(const QVariantList &threads)
{
    QVariantMap p; p["threads"] = threads;
    broadcastEvent("conversationsReady", p);
}

void EventBridgeServer::onInvitesReady(const QVariantList &invites)
{
    QVariantMap p; p["invites"] = invites;
    broadcastEvent("invitesReady", p);
}

void EventBridgeServer::onFriendRequestResponded(const QString &friendId, bool accepted, bool success)
{
    QVariantMap p; p["friendId"] = friendId; p["accepted"] = accepted; p["success"] = success;
    broadcastEvent("friendRequestResponded", p);
}

void EventBridgeServer::onAvatarReady(const QString &threadId, const QString &localPath)
{
    QVariantMap p; p["threadId"] = threadId; p["localPath"] = localPath;
    broadcastEvent("avatarReady", p);
}

void EventBridgeServer::onImageMsgReady(const QString &msgId, const QString &localPath, int width, int height)
{
    QVariantMap p; p["msgId"] = msgId; p["localPath"] = localPath; p["width"] = width; p["height"] = height;
    broadcastEvent("imageMsgReady", p);
}

void EventBridgeServer::onMessageSent(bool success, const QString &threadId)
{
    QVariantMap p; p["success"] = success; p["threadId"] = threadId;
    broadcastEvent("messageSent", p);
}

void EventBridgeServer::onMessageRecalled(const QString &threadId, const QString &msgId)
{
    QVariantMap p; p["threadId"] = threadId; p["msgId"] = msgId;
    broadcastEvent("messageRecalled", p);
}

void EventBridgeServer::onMessageDeletedLocally(const QString &threadId, const QString &msgId)
{
    QVariantMap p; p["threadId"] = threadId; p["msgId"] = msgId;
    broadcastEvent("messageDeletedLocally", p);
}

void EventBridgeServer::onMessageDeleted(const QString &threadId, const QString &msgId, bool success, const QString &error)
{
    QVariantMap p; p["threadId"] = threadId; p["msgId"] = msgId; p["success"] = success; p["error"] = error;
    broadcastEvent("messageDeleted", p);
}

void EventBridgeServer::onMessageRecalledDone(const QString &threadId, const QString &msgId, bool success, const QString &error)
{
    QVariantMap p; p["threadId"] = threadId; p["msgId"] = msgId; p["success"] = success; p["error"] = error;
    broadcastEvent("messageRecalledDone", p);
}

void EventBridgeServer::onMuteDone(const QString &threadId, bool muted, bool success)
{
    QVariantMap p; p["threadId"] = threadId; p["muted"] = muted; p["success"] = success;
    broadcastEvent("muteDone", p);
}

void EventBridgeServer::onBlockUserDone(const QString &userId, bool success)
{
    QVariantMap p; p["userId"] = userId; p["success"] = success;
    broadcastEvent("blockUserDone", p);
}

void EventBridgeServer::onUnblockUserDone(const QString &userId, bool success)
{
    QVariantMap p; p["userId"] = userId; p["success"] = success;
    broadcastEvent("unblockUserDone", p);
}

void EventBridgeServer::onClearHistoryDone(const QString &threadId, bool success)
{
    QVariantMap p; p["threadId"] = threadId; p["success"] = success;
    broadcastEvent("clearHistoryDone", p);
}

void EventBridgeServer::onLeaveGroupDone(const QString &groupId, bool success)
{
    QVariantMap p; p["groupId"] = groupId; p["success"] = success;
    broadcastEvent("leaveGroupDone", p);
}

void EventBridgeServer::onServerQuickMessagesReady(int imported, int skipped, const QString &error)
{
    QVariantMap p; p["imported"] = imported; p["skipped"] = skipped; p["error"] = error;
    broadcastEvent("serverQuickMessagesReady", p);
}
