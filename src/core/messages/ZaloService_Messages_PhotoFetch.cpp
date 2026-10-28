#include "core/messages/MessagesCommon.hpp"

void ZaloService::fetchPhotoViaWs510(const QString &msgId, const QString &threadId)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;
    if (!m_wsConnected || !m_webSocket) {
        return;
    }

    m_pendingPhotoMsgIds[msgId] = threadId;

    qint64 lastIdNum = msgId.toLongLong() - 1;
    QString lastIdStr = lastIdNum > 0 ? QString::number(lastIdNum) : "0";
    QString req = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                  .arg(lastIdStr).arg(threadId);
    sendWsRequest(510, 1, req);
}

static QStringList photoFetchEndpoints(const QString &chatUrl, const QString &fileUrl)
{
    QString fileBase = fileUrl;
    if (fileBase.isEmpty()) {
        fileBase = chatUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QStringList list;
    list << fileBase  + "/api/message/getmsg";
    list << chatUrl   + "/api/message/list";
    list << chatUrl   + "/api/message/getmsgv2";
    list << chatUrl   + "/api/message/getconversation";
    return list;
}

void ZaloService::fetchPhotoViaHttp(const QString &msgId, const QString &threadId)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;

    m_pendingPhotoMsgIds[msgId] = threadId;

    fetchPhotoViaHttpAtIndex(msgId, threadId, 0);
}

void ZaloService::fetchPhotoViaHttpAtIndex(const QString &msgId, const QString &threadId, int idx)
{
    QStringList endpoints = photoFetchEndpoints(m_chatServiceUrl, m_fileServiceUrl);
    if (idx >= endpoints.size()) {
        return;
    }

    QVariantMap p;
    p["msgId"]  = msgId;
    p["toid"]   = threadId;
    p["imei"]   = m_imei;
    p["count"]  = 1;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));
    QString urlStr = endpoints[idx]
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("threadId", threadId);
    reply->setProperty("endpointIdx", idx);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchPhotoDetailDone()));
}

static QString extractPhotoUrl(const QVariantMap &mm)
{
    QString content = mm["content"].toString();
    if (!content.isEmpty() && content.trimmed().startsWith("{")) {
        QVariantMap cm = jsonToMap(content.toUtf8());
        QString u = cm["normalUrl"].toString();
        if (u.isEmpty()) u = cm["hdUrl"].toString();
        if (u.isEmpty()) u = cm["thumbUrl"].toString();
        if (u.isEmpty()) u = cm["href"].toString();
        if (u.isEmpty()) u = cm["oriUrl"].toString();
        if (!u.isEmpty()) return u;
    }
    QString u = mm["normalUrl"].toString();
    if (u.isEmpty()) u = mm["hdUrl"].toString();
    if (u.isEmpty()) u = mm["thumbUrl"].toString();
    if (u.isEmpty()) u = mm["href"].toString();
    return u;
}

void ZaloService::onFetchPhotoDetailDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString msgId    = reply->property("msgId").toString();
    QString threadId = reply->property("threadId").toString();
    int     idx      = reply->property("endpointIdx").toInt();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        QByteArray errBody = reply->readAll();
        qWarning() << "[Msg] photo HTTP fetch" << idx << "failed, msgId=" << msgId << reply->errorString();
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
        return;
    }

    QByteArray raw = reply->readAll();
    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qWarning() << "[Msg] fetchPhotoViaHttp[" << idx << "] API error msgId=" << msgId
                 << outer["error_message"].toString() << "-> trying next endpoint";
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
        return;
    }

    QString dataEnc = outer["data"].toString();
    QString dataJson;
    if (!dataEnc.isEmpty() && !dataEnc.startsWith("{")) {
        dataJson = aesDecryptBase64(m_secretKey, dataEnc);
    } else {
        dataJson = dataEnc;
    }

    QVariantMap data = jsonToMap(dataJson.toUtf8());

    QString photoUrl;

    static const char* listKeys[] = {"msgs", "groupMsgs", "msgList", "data", 0};
    for (int k = 0; listKeys[k] && photoUrl.isEmpty(); ++k) {
        QVariantList list = data[listKeys[k]].toList();
        for (int i = 0; i < list.size(); ++i) {
            QVariantMap mm = list[i].toMap();
            QString mid = mm["msgId"].toString();
            if (!mid.isEmpty() && mid != msgId) continue;
            photoUrl = extractPhotoUrl(mm);
            if (!photoUrl.isEmpty()) break;
        }
    }
    if (photoUrl.isEmpty())
        photoUrl = extractPhotoUrl(data);

    if (!photoUrl.isEmpty() && photoUrl.startsWith("http")) {
        m_pendingPhotoMsgIds.remove(msgId);
        downloadImageMessage(msgId, photoUrl, threadId);
    } else {
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
    }
}
