#include "core/messages/MessagesCommon.hpp"

void ZaloService::sendMessage(const QString &threadId, const QString &content, bool isGroup)
{
    if (!m_loggedIn) return;

    QVariantMap msgData;
    msgData["message"]  = content;
    msgData["clientId"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    msgData["ttl"]      = 0;
    if (isGroup) {
        msgData["visibility"] = 0;
        msgData["grid"]       = threadId;
    } else {
        msgData["toid"] = threadId;
        msgData["imei"] = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(msgData)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString base = isGroup ? m_groupServiceUrl + "/api/group/sendmsg"
                           : m_chatServiceUrl  + "/api/message/sms";

    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest sendReq = buildRequest(urlStr, "https://chat.zalo.me/");
    sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(sendReq, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("content",  content);
    reply->setProperty("isGroup",  isGroup);
    reply->setProperty("cliMsgId", msgData["clientId"].toString());
    connect(reply, SIGNAL(finished()), this, SLOT(onSendMsgDone()));
}

// Quote requests are like sendMessage() plus qmsg* fields, posted to .../quote. qmsgAttach is
// sent only for groups (omitted, not empty, for 1-1).
void ZaloService::sendMessageQuote(const QString &threadId, const QString &content, bool isGroup,
                                    const QString &quoteMsgId, const QString &quoteCliMsgId,
                                    const QString &quoteOwnerId, const QString &quoteContent,
                                    int quoteMsgType, const QString &quoteTs,
                                    const QString &quoteSenderName)
{
    if (!m_loggedIn) return;

    QVariantMap msgData;
    msgData["message"]  = content;
    msgData["clientId"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    msgData["ttl"]      = 0;
    msgData["qmsgOwner"]  = quoteOwnerId;
    msgData["qmsgId"]     = quoteMsgId;
    msgData["qmsgCliId"]  = quoteCliMsgId;
    msgData["qmsgType"]   = quoteMsgType;
    msgData["qmsgTs"]     = quoteTs;
    msgData["qmsg"]       = quoteContent;
    if (isGroup) {
        msgData["visibility"] = 0;
        msgData["grid"]       = threadId;
        QVariantMap attach;
        attach["title"] = quoteContent;
        msgData["qmsgAttach"] = QString::fromUtf8(mapToJson(attach));
    } else {
        msgData["toid"] = threadId;
        msgData["imei"] = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(msgData)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString base = isGroup ? m_groupServiceUrl + "/api/group/quote"
                           : m_chatServiceUrl  + "/api/message/quote";

    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest sendReq = buildRequest(urlStr, "https://chat.zalo.me/");
    sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(sendReq, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("content",  content);
    reply->setProperty("isGroup",  isGroup);
    reply->setProperty("cliMsgId", msgData["clientId"].toString());
    reply->setProperty("quoteMsgId",      quoteMsgId);
    reply->setProperty("quoteContent",    quoteContent);
    reply->setProperty("quoteOwnerId",    quoteOwnerId);
    reply->setProperty("quoteMsgType",    quoteMsgType);
    reply->setProperty("quoteSenderName", quoteSenderName);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendMsgQuoteDone()));
}

void ZaloService::forwardMessage(const QString &content, const QStringList &threadIds, bool isGroup,
                                  const QString &origMsgId, const QString &origTs)
{
    if (!m_loggedIn || content.isEmpty() || threadIds.isEmpty()) {
        emit forwardMessageDone(false, 0, 0, "Nothing to forward");
        return;
    }

    QString clientId = QString::number(QDateTime::currentMSecsSinceEpoch());
    bool hasReference = !origMsgId.isEmpty() && !origTs.isEmpty();

    QVariantMap msgInfo;
    msgInfo["message"] = content;
    QVariantMap decorLog;
    if (hasReference) {
        QVariantMap reference;
        reference["id"]         = origMsgId;
        reference["ts"]         = origTs.toLongLong();
        reference["logSrcType"] = 1;
        reference["fwLvl"]      = 1;
        msgInfo["reference"] = QString::fromUtf8(variantToJsonCompact(reference));

        QVariantMap pmsg;
        pmsg["st"] = 1; pmsg["ts"] = origTs.toLongLong(); pmsg["id"] = origMsgId;
        QVariantMap rmsg = pmsg;
        QVariantMap fw;
        fw["pmsg"] = pmsg;
        fw["rmsg"] = rmsg;
        fw["fwLvl"] = 1;
        decorLog["fw"] = fw;
    }

    QVariantMap params;
    if (isGroup) {
        QVariantList grids;
        foreach (const QString &tid, threadIds) {
            QVariantMap g;
            g["clientId"] = clientId;
            g["grid"]     = tid;
            g["ttl"]      = 0;
            grids.append(g);
        }
        params["grids"] = grids;
    } else {
        QVariantList toIds;
        foreach (const QString &tid, threadIds) {
            QVariantMap t;
            t["clientId"] = clientId;
            t["toUid"]    = tid;
            t["ttl"]      = 0;
            toIds.append(t);
        }
        params["toIds"] = toIds;
        params["imei"]  = m_imei;
    }
    params["ttl"]      = 0;
    params["msgType"]  = "1";
    params["totalIds"] = threadIds.size();
    params["msgInfo"]  = QString::fromUtf8(variantToJsonCompact(msgInfo));
    params["decorLog"] = hasReference ? QString::fromUtf8(variantToJsonCompact(decorLog))
                                       : QString("null");

    // variantToJsonCompact(), not mapToJson(): params.grids/toIds are arrays of OBJECTS.
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    // Both branches use the FILE service host, not m_groupServiceUrl/m_chatServiceUrl.
    QString base = isGroup ? m_fileServiceUrl + "/api/group/mforward"
                           : m_fileServiceUrl + "/api/message/mforward";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onForwardMessageDone()));
}

void ZaloService::onForwardMessageDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        emit forwardMessageDone(false, 0, 0, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit forwardMessageDone(false, 0, 0, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        emit forwardMessageDone(false, 0, 0, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    int successCount = outer["success"].toList().size();
    int failCount    = outer["fail"].toList().size();

    emit forwardMessageDone(successCount > 0, successCount, failCount,
                             (successCount == 0) ? "All targets failed" : QString());
}

void ZaloService::onSendMsgQuoteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError   = (reply->error() != QNetworkReply::NoError);
    QString tid     = reply->property("threadId").toString();
    QString content = reply->property("content").toString();
    bool isGroup    = reply->property("isGroup").toBool();
    QString outCliMsgId = reply->property("cliMsgId").toString();
    QString qMsgId       = reply->property("quoteMsgId").toString();
    QString qContent     = reply->property("quoteContent").toString();
    QString qOwnerId     = reply->property("quoteOwnerId").toString();
    QString qSenderName  = reply->property("quoteSenderName").toString();
    int     qMsgType     = reply->property("quoteMsgType").toInt();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();

    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap decOuter = jsonToMap(dec.toUtf8());
            QVariantMap data = decOuter["data"].toMap();
            qint64 msgIdInt = data["msgId"].toLongLong();
            QString msgId = (msgIdInt != 0) ? QString::number(msgIdInt)
                                            : QString::number(QDateTime::currentMSecsSinceEpoch());
            if (!m_seenMsgIds.contains(msgId)) {
                QVariantMap out;
                out["msgId"]    = msgId;
                out["cliMsgId"] = outCliMsgId;
                out["content"]  = content;
                out["msgType"]  = 1;
                out["isMine"]   = true;
                out["isGroup"]  = isGroup;
                out["senderId"] = m_uid;
                out["dName"]    = m_displayName;
                out["ts"]       = QString::number(QDateTime::currentMSecsSinceEpoch());
                out["quoteMsgId"]      = qMsgId;
                out["quoteContent"]    = qContent;
                out["quoteMsgType"]    = qMsgType;
                out["quoteOwnerId"]    = qOwnerId;
                out["quoteSenderName"] = qSenderName;
                m_seenMsgIds.insert(msgId);
                dbSaveMessage(out, tid);
                emit newMessage(tid, out);
            } else {
            }
        } else {
            hasError = true;
            qWarning() << "[Msg] sendMessageQuote error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
        }
    }
    emit messageSent(!hasError, tid);
}

void ZaloService::onSendMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError   = (reply->error() != QNetworkReply::NoError);
    QString tid     = reply->property("threadId").toString();
    QString content = reply->property("content").toString();
    bool isGroup    = reply->property("isGroup").toBool();
    QString outCliMsgId = reply->property("cliMsgId").toString();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();

    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap decOuter = jsonToMap(dec.toUtf8());
            QVariantMap data = decOuter["data"].toMap();
            qint64 msgIdInt = data["msgId"].toLongLong();
            QString msgId = (msgIdInt != 0) ? QString::number(msgIdInt)
                                            : QString::number(QDateTime::currentMSecsSinceEpoch());
            if (!m_seenMsgIds.contains(msgId)) {
                QVariantMap out;
                out["msgId"]    = msgId;
                out["cliMsgId"] = outCliMsgId;
                out["content"]  = content;
                out["msgType"]  = 1;
                out["isMine"]   = true;
                out["isGroup"]  = isGroup;
                out["senderId"] = m_uid;
                out["dName"]    = m_displayName;
                out["ts"]       = QString::number(QDateTime::currentMSecsSinceEpoch());
                m_seenMsgIds.insert(msgId);
                dbSaveMessage(out, tid);
                emit newMessage(tid, out);
            } else {
            }
        } else {
            hasError = true;
            qWarning() << "[Msg] sendMessage error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
        }
    }
    emit messageSent(!hasError, tid);
}
