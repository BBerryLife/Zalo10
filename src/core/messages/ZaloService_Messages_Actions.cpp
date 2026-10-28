#include "core/messages/MessagesCommon.hpp"

void ZaloService::deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                 const QString &cliMsgId, const QString &senderId, bool onlyMe)
{
    if (!m_loggedIn) return;

    // Our own messages can only be removed for everyone via recall; the server rejects "delete" for
    // them, so fail early.
    bool isSelf = (senderId == m_uid);
    if (isSelf && !onlyMe) {
        emit messageDeleted(threadId, msgId, false, "To delete your message for everyone, use Recall instead");
        return;
    }
    if (!isGroup && !onlyMe) {
        emit messageDeleted(threadId, msgId, false, "Can't delete this message for everyone in a direct chat");
        return;
    }

    QVariantMap msgEntry;
    msgEntry["cliMsgId"]   = cliMsgId;
    msgEntry["globalMsgId"] = msgId;
    msgEntry["ownerId"]    = senderId;
    msgEntry["destId"]     = threadId;
    QVariantList msgList;
    msgList << msgEntry;

    QVariantMap params;
    params[isGroup ? "grid" : "toid"] = threadId;
    params["cliMsgId"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    params["msgs"]     = msgList;
    params["onlyMe"]   = onlyMe ? 1 : 0;
    if (!isGroup) params["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString base = isGroup ? m_groupServiceUrl + "/api/group/deletemsg"
                           : m_chatServiceUrl  + "/api/message/delete";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("msgId",    msgId);
    connect(reply, SIGNAL(finished()), this, SLOT(onDeleteMsgDone()));
}

void ZaloService::onDeleteMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError  = (reply->error() != QNetworkReply::NoError);
    QString tid    = reply->property("threadId").toString();
    QString msgId  = reply->property("msgId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            // "Delete for me" removes the message completely (hard DELETE, no placeholder), unlike recall,
            // which only tags it.
            markMessageDeletedForMe(tid, msgId);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qWarning() << "[Msg] deleteMessage error_code:" << outer["error_code"].toInt() << errMsg;
        }
    } else {
        errMsg = reply->errorString();
    }
    emit messageDeleted(tid, msgId, !hasError, errMsg);
}

void ZaloService::recallMessage(const QString &threadId, bool isGroup, const QString &msgId, const QString &cliMsgId)
{
    if (!m_loggedIn) return;

    QVariantMap params;
    params["msgId"]        = msgId;
    params["clientId"]     = QString::number(QDateTime::currentMSecsSinceEpoch());
    params["cliMsgIdUndo"] = cliMsgId;
    if (isGroup) {
        params["grid"]       = threadId;
        params["visibility"] = 0;
        params["imei"]       = m_imei;
    } else {
        params["toid"] = threadId;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString base = isGroup ? m_groupServiceUrl + "/api/group/undomsg"
                           : m_chatServiceUrl  + "/api/message/undo";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("msgId",    msgId);
    connect(reply, SIGNAL(finished()), this, SLOT(onRecallMsgDone()));
}

void ZaloService::onRecallMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError  = (reply->error() != QNetworkReply::NoError);
    QString tid    = reply->property("threadId").toString();
    QString msgId  = reply->property("msgId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            markMessageRecalled(tid, msgId);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qWarning() << "[Msg] recallMessage error_code:" << outer["error_code"].toInt() << errMsg;
        }
    } else {
        errMsg = reply->errorString();
    }
    emit messageRecalledDone(tid, msgId, !hasError, errMsg);
}

// Reactions: icon -> wire symbol sent as rIcon, rType via reactionIconToRType(). The server needs
// "react_list" whose entry is a JSON STRING under "message":
// { rMsg: [{ gMsgID, cMsgID, msgType }], rIcon, rType, source } with gMsgID/cMsgID as real numbers
// and source=6 (tap on a bubble). The old flat-param payload returned 200 but was never broadcast.
void ZaloService::reactMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                const QString &cliMsgId, int msgType, int rType, const QString &icon)
{
    if (!m_loggedIn) return;

    // rType comes from the icon via reactionIconToRType(), not the caller's slot index (QML's
    // like=0..angry=5 is not Zalo's numbering). The removal sentinel (rType < 0) is still read from
    // the caller.
    bool removing = (rType < 0);
    int  wireRType = removing ? -1 : reactionIconToRType(icon);
    qint64 clientId = QDateTime::currentMSecsSinceEpoch();

    // gMsgID/cMsgID are embedded as raw numeric text: going through LongLong -> mapToJson() casts to
    // double and corrupts Zalo's 64-bit ids.
    QString rIconOut = removing ? QString() : reactionIconToEmoji(icon);
    QString innerMessage = QString(
        "{\"rMsg\":[{\"gMsgID\":%1,\"cMsgID\":%2,\"msgType\":1}],"
        "\"rIcon\":%3,\"rType\":%4,\"source\":6}")
        .arg(msgId)
        .arg(cliMsgId)
        .arg(jsonQuote(rIconOut))
        .arg(wireRType);

    // The whole params object is hand-built too: mapToJson()'s List branch only handles scalars (a
    // list of maps yields garbage). The react_list entry's "message" must be a quoted JSON string
    // and "clientId" a bare number.
    QString params = QString("{\"react_list\":[{\"message\":%1,\"clientId\":%2}]")
        .arg(jsonQuote(innerMessage))
        .arg(clientId);
    if (isGroup) {
        params += QString(",\"grid\":%1,\"imei\":%2")
            .arg(jsonQuote(threadId))
            .arg(jsonQuote(m_imei));
    } else {
        params += QString(",\"toid\":%1").arg(jsonQuote(threadId));
    }
    params += "}";

    QString encParams = aesEncryptBase64(m_secretKey, params);
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString reactionHost = !m_reactionServiceUrl.isEmpty()
        ? m_reactionServiceUrl
        : (isGroup ? m_groupServiceUrl : m_chatServiceUrl);
    QString base = isGroup ? reactionHost + "/api/group/reaction"
                           : reactionHost + "/api/message/reaction";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("icon",     removing ? QString() : icon);
    connect(reply, SIGNAL(finished()), this, SLOT(onReactMsgDone()));
}

void ZaloService::onReactMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError  = (reply->error() != QNetworkReply::NoError);
    QString tid    = reply->property("threadId").toString();
    QString msgId  = reply->property("msgId").toString();
    QString icon   = reply->property("icon").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            if (icon.isEmpty()) dbRemoveReaction(msgId, m_uid);
            else                dbSaveReaction(tid, msgId, m_uid, icon);
            emit reactionUpdated(tid, msgId, m_uid, icon);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qWarning() << "[Msg] reactMessage error_code:" << outer["error_code"].toInt() << errMsg;
        }
    } else {
        errMsg = reply->errorString();
    }
    emit reactMessageDone(tid, msgId, !hasError, errMsg);
}
