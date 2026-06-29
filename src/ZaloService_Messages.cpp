#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeManager>

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QScriptEngine>
#include <QScriptValue>
#include <QUuid>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegExp>
#include <QStringList>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSslError>
#include <sqlite3.h>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <string.h>

// Sending and receiving chat content: text messages, photos, files,
// recall handling, and the foreground polling fallback used when the
// WebSocket connection isn't available.

void ZaloService::fetchMessages(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn) return;

    if (isGroup) {
        // Group: dùng HTTP GET /api/group/history (theo zca-js getGroupChatHistory.ts)
        QVariantMap innerParams;
        innerParams["grid"]  = threadId;
        innerParams["count"] = 50;

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
        QString urlStr = m_groupServiceUrl + "/api/group/history"
                       + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE)
                       + "&params=" + QUrl::toPercentEncoding(encParams);

        qDebug() << "[Zalo] fetchMessages group GET" << urlStr.left(100) << "isGroup:" << isGroup;
        QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
        reply->setProperty("threadId", threadId);
        reply->setProperty("isGroup",  true);
        connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDone()));
    } else {
        // DM: dùng WebSocket cmd=510 requestOldMessages (theo zca-js listen.js)
        // HTTP /api/message/getmsglist không tồn tại → 404
        m_pending510Toid = threadId; // track thread đang request (ghi đè nếu user switch tab)
        if (!m_wsConnected || !m_webSocket) {
            qDebug() << "[Zalo] fetchMessages DM: WS not ready, connecting for" << threadId;
            if (!m_zpwWsUrls.isEmpty()) connectWebSocket();
            return;
        }
        QString lastId = m_threadLastMsgId.value(threadId, "0");
        QString req510 = QString("{\"first\":true,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                         .arg(lastId).arg(threadId);
        sendWsRequest(510, 1, req510);
        qDebug() << "[Zalo] fetchMessages DM: sent WS cmd=510 toid=" << threadId << "lastId=" << lastId;
    }
}

void ZaloService::onFetchMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid    = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchMessages raw (first200):" << raw.left(200);
    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchMessages error:" << root["error_message"].toString();
        emit messagesReady(tid, QVariantList());
        return;
    }

    QVariantList msgs;
    bool isGroup = reply->property("isGroup").toBool();

    QString dec2 = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchMessages decrypted (first150):" << dec2.left(150);

    QVariantMap outer2 = jsonToMap(dec2.toUtf8());
    QVariantMap d2;
    if (outer2.contains("data") && outer2["data"].type() == QVariant::Map)
        d2 = outer2["data"].toMap();
    else
        d2 = outer2;

    QVariantList rawMsgs = isGroup ? d2["groupMsgs"].toList() : d2["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["data"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["msgList"].toList();
    qDebug() << "[Zalo] fetchMessages rawMsgs count:" << rawMsgs.size() << "d2 keys:" << d2.keys() << "myUid:" << m_uid;

    qint64 maxMsgNum = -1;
    QString newestMsgId;

    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();

        // chat.undo = recall/unsend notification, not a real message. Patch the
        // original message if it's earlier in this same history batch, persist the
        // recall to SQLite either way, and skip adding this event as its own bubble.
        QString recalledIdG = extractRecalledMsgId(m);
        if (!recalledIdG.isEmpty()) {
            markMessageRecalled(tid, recalledIdG);
            for (int pj = 0; pj < msgs.size(); ++pj) {
                QVariantMap pm = msgs[pj].toMap();
                if (pm["msgId"].toString() == recalledIdG) {
                    pm["content"] = QString();
                    pm["msgType"] = 99;
                    msgs[pj] = pm;
                    break;
                }
            }
            continue;
        }

        QVariantMap out;
        QString msgId    = m["msgId"].toString();
        QString uidFrom  = m["uidFrom"].toString();
        bool    isMine   = (uidFrom == m_uid);
        out["msgId"]    = msgId;
        out["senderId"] = uidFrom;
        out["dName"]    = m["dName"].toString();
        out["ts"]       = m["ts"].toString();
        out["isGroup"]  = isGroup;
        out["isMine"]   = isMine;
        out["msgType"]  = m["msgType"].toInt();
        int mt = m["msgType"].toInt();
        QString rawContent = m["content"].toString();
        if (mt == 2) rawContent = normalizePhotoContent(m, rawContent);
        out["content"]  = rawContent;
        msgs.append(out);
        if (i < 5)
            qDebug() << "[Zalo] msg[" << i << "] msgId=" << msgId
                     << "uidFrom=" << uidFrom << "isMine=" << isMine
                     << "content=" << m["content"].toString().left(30);

        if (!msgId.isEmpty()) {
            qint64 num = msgId.toLongLong();
            if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
        }
    }
    if (!newestMsgId.isEmpty())
        m_lastPollMsgId = newestMsgId;

    m_seenMsgIds.clear();
    for (int i = 0; i < msgs.size(); ++i) {
        QVariantMap mm = msgs[i].toMap();
        if (mm["isMine"].toBool()) {
            QString mid = mm["msgId"].toString();
            if (!mid.isEmpty()) m_seenMsgIds.insert(mid);
        }
    }

    qDebug() << "[Zalo] fetchMessages found" << msgs.size() << "msgs, lastPollMsgId=" << m_lastPollMsgId;

    // Lưu vào SQLite persistent
    for (int i = 0; i < msgs.size(); ++i)
        dbSaveMessage(msgs[i].toMap(), tid);

    emit messagesReady(tid, msgs);
}

// Fetch full-res photo URL via WS cmd=510 history.
// Called when a real-time WS photo arrives with only a protobuf previewThumb (no HTTP URL).
// Sends cmd=510 with lastId=(msgId-1) so the server returns msgId and any newer messages,
// which allows us to extract the real CDN normalUrl/hdUrl from the content field.
void ZaloService::fetchPhotoViaWs510(const QString &msgId, const QString &threadId)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;
    if (!m_wsConnected || !m_webSocket) {
        qDebug() << "[Zalo] fetchPhotoViaWs510: WS not connected, skipping msgId=" << msgId;
        return;
    }

    // Track msgId -> threadId so cmd=510 handler knows this is a photo fetch
    m_pendingPhotoMsgIds[msgId] = threadId;

    // Request lastId = (msgId - 1) so the server includes msgId itself in the
    // response — sending lastId=msgId would ask for messages older than it,
    // which returns nothing when msgId is the newest message in the thread.
    qint64 lastIdNum = msgId.toLongLong() - 1;
    QString lastIdStr = lastIdNum > 0 ? QString::number(lastIdNum) : "0";
    QString req = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                  .arg(lastIdStr).arg(threadId);
    sendWsRequest(510, 1, req);
    qDebug() << "[Zalo] fetchPhotoViaWs510: sent WS cmd=510 msgId=" << msgId
             << "lastId=" << lastIdStr << "toid=" << threadId;
}

// Fetch photo HTTP URL when WS cmd=510 keeps returning protobuf blob.
//
// Zalo does NOT expose /api/message/getmsg (returns 404).
// We try a sequence of known endpoints, retrying on 404 / API error:
//   [0] file service:  file[0]/api/message/getmsg        (file service variant)
//   [1] chat service:  chat[0]/api/message/list           (DM history, like group/history)
//   [2] chat service:  chat[0]/api/message/getmsgv2       (v2 variant)
//   [3] chat service:  chat[0]/api/message/getconversation
//
// Params: {toid, msgId, imei, count} — subset used by each endpoint.
//
static QStringList photoFetchEndpoints(const QString &chatUrl, const QString &fileUrl)
{
    // Build file service base: derive from chatUrl if fileUrl is empty
    QString fileBase = fileUrl;
    if (fileBase.isEmpty()) {
        fileBase = chatUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QStringList list;
    list << fileBase  + "/api/message/getmsg";          // [0] file service
    list << chatUrl   + "/api/message/list";             // [1] DM history (like group/history)
    list << chatUrl   + "/api/message/getmsgv2";         // [2] v2 variant
    list << chatUrl   + "/api/message/getconversation";  // [3] conversation endpoint
    return list;
}

void ZaloService::fetchPhotoViaHttp(const QString &msgId, const QString &threadId)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;

    // Track for response handler
    m_pendingPhotoMsgIds[msgId] = threadId;

    // Start with endpoint index 0
    fetchPhotoViaHttpAtIndex(msgId, threadId, 0);
}

void ZaloService::fetchPhotoViaHttpAtIndex(const QString &msgId, const QString &threadId, int idx)
{
    QStringList endpoints = photoFetchEndpoints(m_chatServiceUrl, m_fileServiceUrl);
    if (idx >= endpoints.size()) {
        qDebug() << "[Zalo] fetchPhotoViaHttp: all endpoints exhausted for msgId=" << msgId;
        return;
    }

    QVariantMap p;
    p["msgId"]  = msgId;
    p["toid"]   = threadId;
    p["imei"]   = m_imei;
    p["count"]  = 1;   // only need this one message

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));
    QString urlStr = endpoints[idx]
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    qDebug() << "[Zalo] fetchPhotoViaHttp[" << idx << "] GET msgId=" << msgId
             << endpoints[idx].section('/', -1);
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("threadId", threadId);
    reply->setProperty("endpointIdx", idx);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchPhotoDetailDone()));
}

// Extract real CDN photo URL from a decrypted message JSON map.
static QString extractPhotoUrl(const QVariantMap &mm)
{
    // Try content JSON
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
    // Try top-level fields
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

    // On HTTP error (e.g. 404), try next endpoint
    if (reply->error() != QNetworkReply::NoError) {
        QByteArray errBody = reply->readAll();
        qDebug() << "[Zalo] fetchPhotoViaHttp[" << idx << "] HTTP error msgId=" << msgId
                 << reply->errorString() << "body=" << errBody.left(100);
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
        return;
    }

    QByteArray raw = reply->readAll();
    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo] fetchPhotoViaHttp[" << idx << "] API error msgId=" << msgId
                 << outer["error_message"].toString() << "→ trying next endpoint";
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
        return;
    }

    // Decrypt data field
    QString dataEnc = outer["data"].toString();
    QString dataJson;
    if (!dataEnc.isEmpty() && !dataEnc.startsWith("{")) {
        dataJson = aesDecryptBase64(m_secretKey, dataEnc);
    } else {
        dataJson = dataEnc;
    }

    QVariantMap data = jsonToMap(dataJson.toUtf8());

    // Try to find the matching message in multiple possible response shapes:
    //   {msgs: [...]}  /  {groupMsgs: [...]}  /  {data: [...]}  /  single message map
    QString photoUrl;

    static const char* listKeys[] = {"msgs", "groupMsgs", "msgList", "data", 0};
    for (int k = 0; listKeys[k] && photoUrl.isEmpty(); ++k) {
        QVariantList list = data[listKeys[k]].toList();
        for (int i = 0; i < list.size(); ++i) {
            QVariantMap mm = list[i].toMap();
            // Accept any message in the list if we only requested 1; or match msgId
            QString mid = mm["msgId"].toString();
            if (!mid.isEmpty() && mid != msgId) continue;
            photoUrl = extractPhotoUrl(mm);
            if (!photoUrl.isEmpty()) break;
        }
    }
    // Fallback: treat data itself as the message
    if (photoUrl.isEmpty())
        photoUrl = extractPhotoUrl(data);

    if (!photoUrl.isEmpty() && photoUrl.startsWith("http")) {
        qDebug() << "[Zalo] fetchPhotoViaHttp[" << idx << "] resolved msgId=" << msgId
                 << "url=" << photoUrl.left(80);
        m_pendingPhotoMsgIds.remove(msgId);
        downloadImageMessage(msgId, photoUrl, threadId);
    } else {
        qDebug() << "[Zalo] fetchPhotoViaHttp[" << idx << "] no HTTP URL yet msgId=" << msgId
                 << "resp=" << dataJson.left(150) << "→ trying next endpoint";
        fetchPhotoViaHttpAtIndex(msgId, threadId, idx + 1);
    }
}


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
        // group không gửi imei (theo zca-js)
    } else {
        msgData["toid"] = threadId;
        msgData["imei"] = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(msgData)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    // 1-1 dùng /api/message/sms, group dùng /api/group/sendmsg (theo zca-js)
    QString base = isGroup ? m_groupServiceUrl + "/api/group/sendmsg"
                           : m_chatServiceUrl  + "/api/message/sms";

    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest sendReq = buildRequest(urlStr, "https://chat.zalo.me/");
    sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] sendMessage POST" << urlStr << "isGroup:" << isGroup;
    QNetworkReply *reply = m_manager->post(sendReq, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("content",  content);
    reply->setProperty("isGroup",  isGroup);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendMsgDone()));
}

void ZaloService::onSendMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError   = (reply->error() != QNetworkReply::NoError);
    QString tid     = reply->property("threadId").toString();
    QString content = reply->property("content").toString();
    bool isGroup    = reply->property("isGroup").toBool();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendMessage response:" << raw.left(200);

    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            // Parse msgId from encrypted response (same pattern as onSendPhotoMsgDone)
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            qDebug() << "[Zalo] sendMessage decrypted:" << dec.left(200);
            QVariantMap decOuter = jsonToMap(dec.toUtf8());
            // Response format: {"error_code":0,"error_message":"...","data":{"msgId":"..."}}
            QVariantMap data = decOuter["data"].toMap();
            qDebug() << "[Zalo] sendMessage data keys:" << data.keys() << "msgId=" << data["msgId"].toString();
            qint64 msgIdInt = data["msgId"].toLongLong();
            QString msgId = (msgIdInt != 0) ? QString::number(msgIdInt)
                                            : QString::number(QDateTime::currentMSecsSinceEpoch());
            // If WS cmd=501 already delivered this message, skip to avoid duplicate bubble
            if (!m_seenMsgIds.contains(msgId)) {
                QVariantMap out;
                out["msgId"]    = msgId;
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
                qDebug() << "[Zalo] sendMessage: WS already delivered msgId=" << msgId << ", skipping duplicate";
            }
        } else {
            hasError = true;
            qDebug() << "[Zalo] sendMessage error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
        }
    }
    emit messageSent(!hasError, tid);
}

// ─── Send Photo ──────────────────────────────────────────────────────────────
// Two-step: 1) upload to file[0]/api/{message|group}/photo_original/upload
//           2) send message via {chat|group}/api/{message|group}/photo
void ZaloService::sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Zalo] sendPhoto: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString ext      = path.section('.', -1).toLower();
    QString mime     = (ext == "png") ? "image/png" : (ext == "webp") ? "image/webp" : "image/jpeg";
    QString filename = path.section('/', -1);
    qint64  ts       = QDateTime::currentMSecsSinceEpoch();
    QString boundary = "----ZaloBoundary" + QString::number(ts);

    // Params in query string (AES-encrypted) per zca-js uploadAttachment.ts
    // Note: imei goes in multipart body, NOT in AES params
    QVariantMap p;
    if (isGroup) p["grid"] = threadId;
    else         p["toid"] = threadId;
    p["totalChunk"] = 1;
    p["fileName"]   = filename;
    p["clientId"]   = QString::number(ts);
    p["totalSize"]  = (int)fileData.size();
    p["isE2EE"]     = 0;
    p["jxl"]        = 0;
    p["chunkId"]    = 1;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    // file service URL: m_fileServiceUrl (file[0]), fallback to regex on chatServiceUrl
    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString upEndpoint = isGroup ? "group" : "message";
    QString urlStr = fileBase + "/api/" + upEndpoint + "/photo_original/upload"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0"
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QByteArray body;
    // imei must be sent as a separate multipart field (not in AES params)
    if (!isGroup) {
        body += ("--" + boundary + "\r\n").toUtf8();
        body += "Content-Disposition: form-data; name=\"imei\"\r\n\r\n";
        body += m_imei.toUtf8() + "\r\n";
    }
    body += ("--" + boundary + "\r\n").toUtf8();
    body += ("Content-Disposition: form-data; name=\"chunkContent\"; filename=\"" + filename + "\"\r\n").toUtf8();
    body += QByteArray("Content-Type: application/octet-stream\r\n\r\n");
    body += fileData + "\r\n";
    body += ("--" + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);

    qDebug() << "[Zalo] sendPhoto upload POST" << urlStr.left(120) << "size:" << fileData.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId",  threadId);
    reply->setProperty("localPath", "file://" + path);
    reply->setProperty("isGroup",   isGroup);
    reply->setProperty("caption",   caption);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendPhotoDone()));
}

void ZaloService::onSendPhotoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok         = (reply->error() == QNetworkReply::NoError);
    QString tid     = reply->property("threadId").toString();
    QString localPath = reply->property("localPath").toString();
    bool isGroup    = reply->property("isGroup").toBool();
    QString caption = reply->property("caption").toString();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendPhoto upload response:" << raw.left(300);

    if (!ok) { emit messageSent(false, tid); return; }

    // Upload response: {"error_code":0,"data":"AES_ENCRYPTED"} — same pattern as other APIs
    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo] sendPhoto upload error:" << outer["error_message"].toString();
        emit messageSent(false, tid);
        return;
    }
    QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());
    qDebug() << "[Zalo] sendPhoto upload decrypted:" << decStr.left(200);

    // Decrypted string is {"error_code":0,"data":{"normalUrl":...,"photoId":...}}
    // Parse the outer wrapper, then get the inner data map
    QVariantMap decOuter = jsonToMap(decStr.toUtf8());
    QVariantMap uploadData;
    QVariant dataVariant = decOuter["data"];
    if (dataVariant.type() == QVariant::Map) {
        uploadData = dataVariant.toMap();
    } else {
        // Fallback: try parsing data as JSON string
        uploadData = jsonToMap(dataVariant.toString().toUtf8());
    }

    qDebug() << "[Zalo] sendPhoto upload keys:" << uploadData.keys();

    QString normalUrl = uploadData["normalUrl"].toString();
    QString thumbUrl  = uploadData["thumbUrl"].toString();
    QString hdUrl     = uploadData["hdUrl"].toString();
    QString photoId   = uploadData["photoId"].toString();
    if (thumbUrl.isEmpty()) thumbUrl = hdUrl;
    if (thumbUrl.isEmpty()) thumbUrl = normalUrl;

    if (normalUrl.isEmpty()) {
        qDebug() << "[Zalo] sendPhoto: upload OK but no normalUrl, raw:" << raw.left(200);
        emit messageSent(false, tid);
        return;
    }

    // Step 2: send photo message
    // zca-js: file[0]/api/{message|group}/photo_original/send
    // params: photoId, clientId, desc, width, height, toid|grid,
    //         rawUrl=normalUrl, hdUrl, thumbUrl, hdSize=totalSize,
    //         oriUrl (group only), normalUrl (DM only),
    //         zsource=-1, ttl=0, jcp
    qint64 ts2 = QDateTime::currentMSecsSinceEpoch();
    QSize photoDim = imageDimensions(localPath);
    QVariantMap mp;
    mp["photoId"]   = photoId;
    mp["clientId"]  = QString::number(ts2);
    mp["desc"]      = caption;
    mp["width"]     = photoDim.width();
    mp["height"]    = photoDim.height();
    mp["rawUrl"]    = normalUrl;
    mp["hdUrl"]     = hdUrl.isEmpty() ? normalUrl : hdUrl;
    mp["thumbUrl"]  = thumbUrl;
    mp["hdSize"]    = uploadData.value("totalSize", "0").toString();
    mp["zsource"]   = -1;
    mp["ttl"]       = 0;
    mp["jcp"]       = "{\"convertible\":\"jxl\"}";
    if (isGroup) {
        mp["grid"]     = tid;
        mp["oriUrl"]   = normalUrl;
    } else {
        mp["toid"]      = tid;
        mp["normalUrl"] = normalUrl;
    }

    QString encMsg = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(mp)));
    QByteArray body2 = "params=" + QUrl::toPercentEncoding(encMsg);

    // Use file service URL (same base as upload)
    QString fileBase2 = m_fileServiceUrl;
    if (fileBase2.isEmpty()) {
        fileBase2 = m_chatServiceUrl;
        QRegExp rx2("tt-chat\\d+-wpa");
        fileBase2.replace(rx2, "tt-files-wpa");
    }
    QString sendEndpoint = isGroup ? "group" : "message";
    QString msgUrl = fileBase2 + "/api/" + sendEndpoint + "/photo_original/send"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0";

    QNetworkRequest req2 = buildRequest(msgUrl, "https://chat.zalo.me/");
    req2.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    // Build the content JSON that QML will store and display.
    // Caption (desc) is included so the QML photo bubble can render it.
    QString captionEsc = caption;
    captionEsc.replace("\\", "\\\\").replace("\"", "\\\"")
              .replace("\n", "\\n").replace("\r", "\\r");
    QString contentJson = captionEsc.isEmpty()
        ? QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
              .arg(normalUrl).arg(thumbUrl)
              .arg(hdUrl.isEmpty() ? normalUrl : hdUrl)
        : QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\",\"caption\":\"%4\"}")
              .arg(normalUrl).arg(thumbUrl)
              .arg(hdUrl.isEmpty() ? normalUrl : hdUrl).arg(captionEsc);

    qDebug() << "[Zalo] sendPhoto send-msg POST" << msgUrl.left(100);
    QNetworkReply *r2 = m_manager->post(req2, body2);
    r2->setProperty("threadId",    tid);
    r2->setProperty("localPath",   localPath);
    r2->setProperty("isGroup",     isGroup);
    r2->setProperty("contentJson", contentJson);
    r2->setProperty("caption",     caption);
    connect(r2, SIGNAL(finished()), this, SLOT(onSendPhotoMsgDone()));
}

void ZaloService::onSendPhotoMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok             = (reply->error() == QNetworkReply::NoError);
    QString tid         = reply->property("threadId").toString();
    QString localPath   = reply->property("localPath").toString();
    bool isGroup        = reply->property("isGroup").toBool();
    QString contentJson = reply->property("contentJson").toString();
    QByteArray raw      = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendPhoto send-msg response:" << raw.left(300);

    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap data = jsonToMap(dec.toUtf8());
            qint64 msgIdInt = data["msgId"].toLongLong();
            QString msgId = (msgIdInt != 0) ? QString::number(msgIdInt)
                                            : QString::number(QDateTime::currentMSecsSinceEpoch());
            // If WS cmd=501 already delivered this message, skip emit but still save localImage to DB
            if (m_seenMsgIds.contains(msgId)) {
                qDebug() << "[Zalo] sendPhoto: WS already delivered msgId=" << msgId << ", skipping duplicate emit";
                // Still persist localImage so it shows on reopen
                if (!localPath.isEmpty() && m_db) {
                    QSize dim = imageDimensions(localPath);
                    const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                    sqlite3_stmt *stmt = 0;
                    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, localPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt,  2, dim.width());
                        sqlite3_bind_int(stmt,  3, dim.height());
                        sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
            } else {
                QSize dim = imageDimensions(localPath);
                QVariantMap out;
                out["msgId"]      = msgId;
                out["content"]    = contentJson;
                out["msgType"]    = 2;
                out["isMine"]     = true;
                out["isGroup"]    = isGroup;
                out["senderId"]   = m_uid;
                out["dName"]      = m_displayName;
                out["ts"]         = QString::number(QDateTime::currentMSecsSinceEpoch());
                out["localImage"] = localPath;
                out["imgWidth"]   = dim.width();
                out["imgHeight"]  = dim.height();
                m_seenMsgIds.insert(msgId);
                dbSaveMessage(out, tid);
                emit newMessage(tid, out);
            }
        } else {
            ok = false;
            qDebug() << "[Zalo] sendPhoto send-msg error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
        }
    }
    emit messageSent(ok, tid);
}

// ─── sendFile: gửi file thường (non-image) ────────────────────────────────
void ZaloService::sendFile(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Zalo] sendFile: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString filename = path.section('/', -1);
    QString clientId = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString boundary = "----ZaloFileBoundary" + clientId;

    // Detect MIME
    QString ext  = filename.section('.', -1).toLower();
    QString mime = "application/octet-stream";
    if (ext == "pdf")  mime = "application/pdf";
    else if (ext == "doc" || ext == "docx") mime = "application/msword";
    else if (ext == "xls" || ext == "xlsx") mime = "application/vnd.ms-excel";
    else if (ext == "ppt" || ext == "pptx") mime = "application/vnd.ms-powerpoint";
    else if (ext == "zip" || ext == "rar")  mime = "application/zip";
    else if (ext == "mp3" || ext == "m4a")  mime = "audio/mpeg";
    else if (ext == "mp4" || ext == "mov")  mime = "video/mp4";
    else if (ext == "txt")                  mime = "text/plain";

    // Build multipart body
    QByteArray bnd = ("--" + boundary).toUtf8();
    QByteArray body;
    body += bnd + "\r\nContent-Disposition: form-data; name=\"clientId\"\r\n\r\n" + clientId.toUtf8() + "\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"ttl\"\r\n\r\n0\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"fileName\"\r\n\r\n" + filename.toUtf8() + "\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"totalSize\"\r\n\r\n"
          + QByteArray::number(fileData.size()) + "\r\n";
    if (isGroup) {
        body += bnd + "\r\nContent-Disposition: form-data; name=\"grid\"\r\n\r\n" + threadId.toUtf8() + "\r\n";
        body += bnd + "\r\nContent-Disposition: form-data; name=\"visibility\"\r\n\r\n0\r\n";
    } else {
        body += bnd + "\r\nContent-Disposition: form-data; name=\"toid\"\r\n\r\n" + threadId.toUtf8() + "\r\n";
        body += bnd + "\r\nContent-Disposition: form-data; name=\"imei\"\r\n\r\n" + m_imei.toUtf8() + "\r\n";
    }
    // File data
    body += "--" + boundary.toUtf8() + "\r\n";
    body += "Content-Disposition: form-data; name=\"fileContent\"; filename=\"" + filename.toUtf8() + "\"\r\n";
    body += "Content-Type: " + mime.toUtf8() + "\r\n\r\n";
    body += fileData + "\r\n";
    body += "--" + boundary.toUtf8() + "--\r\n";

    QVariantMap fileParams;
    if (isGroup) {
        fileParams["grid"]       = threadId;
        fileParams["visibility"] = 0;
    } else {
        fileParams["toid"]       = threadId;
        fileParams["imei"]       = m_imei;
    }
    fileParams["clientId"]  = clientId;
    fileParams["ttl"]       = 0;
    fileParams["fileName"]  = filename;
    fileParams["totalSize"] = fileData.size();

    QString encParamsFile = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(fileParams)));

    // zca-js: file upload endpoint
    QString base = isGroup ? m_groupServiceUrl + "/api/group/sendfile"
                           : m_chatServiceUrl  + "/api/message/sendfile";
    QString urlStr = base
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params="   + QUrl::toPercentEncoding(encParamsFile);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);

    qDebug() << "[Zalo] sendFile POST" << urlStr.left(100) << "name:" << filename << "size:" << fileData.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendFileDone()));
}

void ZaloService::onSendFileDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok  = (reply->error() == QNetworkReply::NoError);
    QString tid = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendFile response:" << raw.left(300);
    emit messageSent(ok, tid);
}

// ─── Download image message thumbnail for display ───────────────────────────
static QByteArray makeJpegMarker(quint8 marker, const QByteArray &data)
{
    QByteArray r;
    r += (char)0xFF;
    r += (char)marker;
    quint16 len = (quint16)(data.size() + 2);
    r += (char)((len >> 8) & 0xFF);
    r += (char)(len & 0xFF);
    r += data;
    return r;
}

void ZaloService::downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId)
{
    if (url.isEmpty() || msgId.isEmpty()) return;

    // Handle base64-encoded inline image (previewThumb from Zalo WS real-time photo)
    // These start with base64 data, not "http"
    if (!url.startsWith("http")) {
        if (m_avatarCache.contains(url)) {
            QSize sz = imageDimensions(m_avatarCache[url]);
            emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
            return;
        }
        QByteArray imgData = QByteArray::fromBase64(url.toUtf8());
        if (!imgData.isEmpty()) {
            // Detect format from magic bytes
            QString ext;
            if (imgData.size() >= 2 &&
                (unsigned char)imgData[0] == 0xFF && (unsigned char)imgData[1] == 0xD8)
                ext = "jpg";
            else if (imgData.startsWith("\x89PNG"))
                ext = "png";
            else if (imgData.startsWith("GIF"))
                ext = "gif";
            else if (imgData.startsWith("RIFF") && imgData.size() > 12 &&
                     imgData.mid(8, 4) == "WEBP")
                ext = "webp";

            if (ext.isEmpty()) {
                // Check for Zalo proprietary thumbnail format:
                // [03][00][W][00][H][FF DA ...JPEG SOS data...]
                // The SOS data needs a standard JPEG header prepended to be valid.
                if (imgData.size() > 6 &&
                    (unsigned char)imgData[0] == 0x03 &&
                    (unsigned char)imgData[1] == 0x00 &&
                    (unsigned char)imgData[5] == 0xFF &&
                    (unsigned char)imgData[6] == 0xDA)
                {
                    int W = (unsigned char)imgData[2];
                    int H = (unsigned char)imgData[4];
                    QByteArray sosData = imgData.mid(5); // FF DA onwards

                    // Standard JPEG quantization tables (quality ~50)
                    static const quint8 lumaQ[64] = {
                        16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
                        14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
                        18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
                        49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
                    };
                    static const quint8 chromaQ[64] = {
                        17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
                        24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
                    };
                    // Standard Huffman tables
                    static const quint8 dcLumLen[16]  = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
                    static const quint8 dcLumVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 dcChrLen[16]  = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
                    static const quint8 dcChrVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 acLumLen[16]  = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
                    static const quint8 acLumVal[162] = {
                        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
                        0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,
                        0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,
                        0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
                        0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,
                        0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,
                        0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
                        0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
                        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
                        0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,
                        0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
                    };
                    static const quint8 acChrLen[16]  = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
                    static const quint8 acChrVal[162] = {
                        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
                        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
                        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
                        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
                        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
                        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
                        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
                        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
                        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
                        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
                        0xf9,0xfa
                    };



                    // DQT
                    QByteArray dqt0; dqt0 += (char)0x00;
                    dqt0 += QByteArray((const char*)lumaQ, 64);
                    QByteArray dqt1; dqt1 += (char)0x01;
                    dqt1 += QByteArray((const char*)chromaQ, 64);

                    // SOF0: 8-bit, 3 components, Y=2x2/qtable0, Cb=1x1/qtable1, Cr=1x1/qtable1
                    // IMPORTANT: build byte-by-byte with explicit (char) casts.
                    // Do NOT use string literals like "\x01\x22\x00" because QByteArray::operator+=
                    // with const char* stops at the first \x00 null byte, silently dropping
                    // the Y-component qtable index byte and corrupting all downstream markers.
                    QByteArray sof0Data;
                    sof0Data += (char)8;                    // sample precision (bits)
                    sof0Data += (char)((H >> 8) & 0xFF);   // height high byte
                    sof0Data += (char)(H & 0xFF);           // height low byte
                    sof0Data += (char)((W >> 8) & 0xFF);   // width high byte
                    sof0Data += (char)(W & 0xFF);           // width low byte
                    sof0Data += (char)3;                    // num components
                    sof0Data += (char)0x01;                 // Y  component id
                    sof0Data += (char)0x22;                 // Y  sampling: 2x2
                    sof0Data += (char)0x00;                 // Y  qtable index: 0  <-- THIS BYTE was lost!
                    sof0Data += (char)0x02;                 // Cb component id
                    sof0Data += (char)0x11;                 // Cb sampling: 1x1
                    sof0Data += (char)0x01;                 // Cb qtable index: 1
                    sof0Data += (char)0x03;                 // Cr component id
                    sof0Data += (char)0x11;                 // Cr sampling: 1x1
                    sof0Data += (char)0x01;                 // Cr qtable index: 1

                    // DHT
                    QByteArray dhtDcLum; dhtDcLum += (char)0x00;
                    dhtDcLum += QByteArray((const char*)dcLumLen, 16);
                    dhtDcLum += QByteArray((const char*)dcLumVal, 12);
                    QByteArray dhtDcChr; dhtDcChr += (char)0x01;
                    dhtDcChr += QByteArray((const char*)dcChrLen, 16);
                    dhtDcChr += QByteArray((const char*)dcChrVal, 12);
                    QByteArray dhtAcLum; dhtAcLum += (char)0x10;
                    dhtAcLum += QByteArray((const char*)acLumLen, 16);
                    dhtAcLum += QByteArray((const char*)acLumVal, 162);
                    QByteArray dhtAcChr; dhtAcChr += (char)0x11;
                    dhtAcChr += QByteArray((const char*)acChrLen, 16);
                    dhtAcChr += QByteArray((const char*)acChrVal, 162);

                    QByteArray jpeg;
                    jpeg += "\xFF\xD8";                                    // SOI

                    // APP0 JFIF marker — required by Qt4's QImageReader on BB10
                    // Format: marker(FF E0) + length(00 10) + "JFIF\0" + version(1.1)
                    //         + density_units(0=no units) + Xdensity(0,1) + Ydensity(0,1)
                    //         + thumbnail_size(0,0)
                    static const char app0Data[] = {
                        'J','F','I','F','\x00',  // identifier
                        '\x01','\x01',           // version 1.1
                        '\x00',                  // density units: none
                        '\x00','\x01','\x00','\x01', // X/Y density = 1,1
                        '\x00','\x00'            // no embedded thumbnail
                    };
                    jpeg += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data))); // APP0

                    jpeg += makeJpegMarker(0xDB, dqt0);                    // DQT luma
                    jpeg += makeJpegMarker(0xDB, dqt1);                    // DQT chroma
                    jpeg += makeJpegMarker(0xC0, sof0Data);                // SOF0
                    jpeg += makeJpegMarker(0xC4, dhtDcLum);                // DHT DC luma
                    jpeg += makeJpegMarker(0xC4, dhtDcChr);                // DHT DC chroma
                    jpeg += makeJpegMarker(0xC4, dhtAcLum);                // DHT AC luma
                    jpeg += makeJpegMarker(0xC4, dhtAcChr);                // DHT AC chroma
                    jpeg += sosData;                                        // SOS + scan data
                    jpeg += "\xFF\xD9";                                     // EOI

                    // Use msgId in filename — unique path avoids BB10 image cache stale data.
                    // Always save as .png — BB10 ImageView is more reliable with PNG than JPEG.
                    // NOTE: hardcoded "/tmp/" (NOT QDir::tempPath()) — on this BB10 device
                    // QDir::tempPath() resolves to a per-launch sandboxed scratch dir that gets
                    // wiped every time the app restarts, while plain "/tmp/" is the same
                    // device-wide location avatars use and is confirmed to survive app restarts
                    // (see avatar_meta persistence). See onImageMsgDownloaded() below for the
                    // same fix applied to full-size photos.
                    QString tmpPath = "/tmp/msgthumb_" +
                                      msgId + ".png";
                    QFile::remove(tmpPath);

                    // Byte-stuff scan data: in JPEG, any 0xFF byte in entropy-coded
                    // data MUST be followed by 0x00 so decoders don't mistake it for
                    // a marker. Zalo's raw SOS data has bare 0xFF bytes that cause
                    // "Bogus marker length" in BB10's libjpeg. Escape them here.
                    QByteArray stuffedSos;
                    // SOS header: FF DA + length(2 bytes) + payload[length-2]
                    // Keep header verbatim, only stuff entropy scan data after it.
                    int sosHeaderLen = 0;
                    if (sosData.size() >= 4) {
                        int segLen = ((unsigned char)sosData[2] << 8) | (unsigned char)sosData[3];
                        sosHeaderLen = 2 + segLen; // FF DA + payload
                    }
                    stuffedSos += sosData.left(sosHeaderLen);
                    for (int si = sosHeaderLen; si < sosData.size(); ++si) {
                        unsigned char b = (unsigned char)sosData[si];
                        stuffedSos += (char)b;
                        if (b == 0xFF) {
                            // Only insert stuffing 0x00 if next byte is not already
                            // a valid stuffed zero, restart marker (D0-D7), or EOI (D9).
                            unsigned char next = (si + 1 < sosData.size())
                                                 ? (unsigned char)sosData[si + 1] : 0x00;
                            if (next != 0x00 &&
                                !(next >= 0xD0 && next <= 0xD7) &&
                                next != 0xD9) {
                                stuffedSos += (char)0x00;
                            }
                        }
                    }

                    // Rebuild JPEG with byte-stuffed scan data
                    QByteArray jpeg2;
                    jpeg2 += "\xFF\xD8";
                    jpeg2 += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data)));
                    jpeg2 += makeJpegMarker(0xDB, dqt0);
                    jpeg2 += makeJpegMarker(0xDB, dqt1);
                    jpeg2 += makeJpegMarker(0xC0, sof0Data);
                    jpeg2 += makeJpegMarker(0xC4, dhtDcLum);
                    jpeg2 += makeJpegMarker(0xC4, dhtDcChr);
                    jpeg2 += makeJpegMarker(0xC4, dhtAcLum);
                    jpeg2 += makeJpegMarker(0xC4, dhtAcChr);
                    jpeg2 += stuffedSos;
                    jpeg2 += "\xFF\xD9";

                    // Decode → scale → save as PNG.
                    // Try byte-stuffed version first, then original as last resort.
                    QImage qimg;
                    bool decoded = qimg.loadFromData(jpeg2, "JPEG");
                    if (!decoded) decoded = qimg.loadFromData(jpeg, "JPEG");

                    if (decoded) {
                        // Scale small thumbnails up so they're visible in the chat bubble.
                        // Original is 24×24 — far too small for a chat photo placeholder.
                        if (qimg.width() < 120 || qimg.height() < 120) {
                            qimg = qimg.scaled(240, 240,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
                        }
                        qimg.save(tmpPath, "PNG");
                        qDebug() << "[Zalo] decoded Zalo thumb" << W << "x" << H
                                 << "→ PNG msgId=" << msgId << tmpPath;
                    } else {
                        // Decode failed completely — write a solid blue 240×240 PNG
                        // placeholder so the chat bubble shows *something* while the
                        // full-res fetch (cmd=510) is in flight.
                        QImage placeholder(240, 240, QImage::Format_RGB32);
                        placeholder.fill(QColor(37, 117, 252)); // Zalo blue #2575fc
                        placeholder.save(tmpPath, "PNG");
                        qDebug() << "[Zalo] thumb decode failed, saved placeholder msgId=" << msgId;
                    }
                    QString filePath = "file://" + tmpPath;
                    m_avatarCache[url] = filePath;
                    int imgW = qimg.width();
                    int imgH = qimg.height();
                    if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                        const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                        sqlite3_stmt *stmt2 = 0;
                        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt2, 0) == SQLITE_OK) {
                            sqlite3_bind_text(stmt2, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt2,  2, imgW);
                            sqlite3_bind_int(stmt2,  3, imgH);
                            sqlite3_bind_text(stmt2, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt2);
                            sqlite3_finalize(stmt2);
                        }
                    }
                    emit imageMsgReady(msgId, filePath, imgW, imgH);
                    return;
                }
                qDebug() << "[Zalo] downloadImageMessage: unrecognised format, msgId=" << msgId
                         << "first bytes=" << imgData.left(4).toHex();
                return;
            } // end if(ext.isEmpty())

            // Hardcoded "/tmp/" — same reasoning as msgthumb_ above: QDir::tempPath()
            // does not survive an app restart on this device, plain "/tmp/" does.
            QString tmpPath = "/tmp/msgimg_" +
                              QString::number(qHash(url)) + "." + ext;
            QFile f(tmpPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
                QString filePath = "file://" + tmpPath;
                m_avatarCache[url] = filePath;
                QSize dim = imageDimensions(filePath);
                // Persist to DB
                if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                    const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                    sqlite3_stmt *stmt = 0;
                    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int(stmt,  2, dim.width());
                        sqlite3_bind_int(stmt,  3, dim.height());
                        sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
                qDebug() << "[Zalo] downloadImageMessage base64 decoded msgId" << msgId << "->" << tmpPath;
                emit imageMsgReady(msgId, filePath, dim.width(), dim.height());
                return;
            }
        } // end if(!imgData.isEmpty())
        qDebug() << "[Zalo] downloadImageMessage: non-http url and base64 decode failed, msgId=" << msgId;
        return;
    }

    if (m_avatarCache.contains(url)) {
        QSize sz = imageDimensions(m_avatarCache[url]);
        emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
        return;
    }

    // Persistent check: this msgId may already have a downloaded image from a
    // previous session (logout/login, app restart, etc.) — dbLoadMessages()
    // already returns localImage for display, but downloadImageMessage() can
    // also get called directly (e.g. re-sync, search jump-to-message) without
    // going through that path first. If the file is still on disk, reuse it
    // instead of re-fetching over the network.
    if (!msgId.isEmpty() && m_db) {
        const char *sqlChk = "SELECT localImage, imgWidth, imgHeight FROM messages WHERE msgId=?";
        sqlite3_stmt *chk = 0;
        if (sqlite3_prepare_v2(m_db, sqlChk, -1, &chk, 0) == SQLITE_OK) {
            sqlite3_bind_text(chk, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(chk) == SQLITE_ROW) {
                QString existingPath = QString::fromUtf8((const char*)sqlite3_column_text(chk, 0));
                int existingW = sqlite3_column_int(chk, 1);
                int existingH = sqlite3_column_int(chk, 2);
                QString fsPath = existingPath;
                if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
                if (!fsPath.isEmpty() && QFile::exists(fsPath)) {
                    sqlite3_finalize(chk);
                    m_avatarCache[url] = existingPath;
                    emit imageMsgReady(msgId, existingPath, existingW, existingH);
                    return;
                }
            }
            sqlite3_finalize(chk);
        }
    }

    if (m_pendingAvatars.contains(url)) return;
    m_pendingAvatars.insert(url);

    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    req.setRawHeader("Accept", "image/webp,image/apng,image/*,*/*;q=0.8");
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("imgUrl",   url);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onImageMsgDownloaded()));
}

void ZaloService::onImageMsgDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString msgId   = reply->property("msgId").toString();
    QString url     = reply->property("imgUrl").toString();
    QString threadId = reply->property("threadId").toString();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_pendingAvatars.remove(url);

    if (data.isEmpty()) {
        qDebug() << "[Zalo] downloadImageMessage empty for msgId" << msgId;
        return;
    }

    QString ext = "jpg";
    QByteArray ct = reply->rawHeader("Content-Type");
    if (ct.contains("png"))  ext = "png";
    if (ct.contains("gif"))  ext = "gif";
    if (ct.contains("webp")) ext = "webp";

    // BB10's ImageView does not support WebP. Transcode any WebP (or
    // unrecognised) image to PNG via QImage so it always displays correctly.
    QByteArray finalData = data;
    if (ext == "webp" || ext == "jpg") {
        QImage img;
        if (img.loadFromData(data)) {
            QByteArray pngBuf;
            QBuffer buf(&pngBuf);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "PNG")) {
                finalData = pngBuf;
                ext = "png";
                qDebug() << "[Zalo] onImageMsgDownloaded: transcoded" << ct << "-> PNG for msgId" << msgId;
            }
        }
    }

    // Fixed filename per-message (by msgId, NOT by md5(url)): the URL Zalo
    // returns for the same photo can change between fetches (signed CDN URLs,
    // query params, etc.) even though the underlying image hasn't. Keying the
    // filename off msgId means re-fetching the same message always overwrites
    // the same file instead of leaving the old one behind as an orphan, and
    // lets the QFile::exists() check above in downloadImageMessage() reliably
    // recognise "we already have this one" on the next call.
    //
    // Hardcoded "/tmp/" (NOT QDir::tempPath()): on this BB10 device,
    // QDir::tempPath() resolves to a per-app-launch scratch directory that the
    // OS wipes on every app restart, whereas plain "/tmp/" is the same
    // device-wide, persistent location avatars already use successfully (see
    // avatar_meta — confirmed to survive restarts in the field). Using the
    // same persistent root here is what makes downloaded chat photos actually
    // survive logout/login and app restarts instead of vanishing every time.
    QString stableKey = msgId.isEmpty() ? md5Hex(url) : msgId;
    QString tmpPath = "/tmp/zalo_img_" + stableKey + "." + ext;
    QFile f(tmpPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(finalData); f.close(); }
    QString filePath = "file://" + tmpPath;
    m_avatarCache[url] = filePath;
    QSize dim = imageDimensions(filePath);

    // Persist localImage in DB so it survives chat reopen
    if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
        const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt,  2, dim.width());
            sqlite3_bind_int(stmt,  3, dim.height());
            sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    emit imageMsgReady(msgId, filePath, dim.width(), dim.height());
}

void ZaloService::onQRExpired()
{
    if (!m_loggedIn) {
        emit qrExpired();
        step1_loadLoginPage();
    }
}

void ZaloService::onListenTimer()
{
    if (!m_loggedIn) return;
    // WS keepalive ping (cmd=2 subCmd=1) theo zca-js
    if (m_wsConnected) {
        sendWsPing();
        qDebug() << "[Zalo WS] ping sent";

        if (!m_activeThreadIsGroup && !m_activeThreadId.isEmpty()) {
            QString req510 = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                             .arg(m_lastPollMsgId.isEmpty() ? "0" : m_lastPollMsgId)
                             .arg(m_activeThreadId);
            sendWsRequest(510, 1, req510);
            qDebug() << "[Zalo WS] DM incremental poll cmd=510 toid=" << m_activeThreadId
                     << "lastId=" << m_lastPollMsgId;
        }
    } else {
        if (!m_wsReconnectTimer->isActive())
            m_wsReconnectTimer->start(2000);
    }
    if (!m_activeThreadIsGroup || m_activeThreadId.isEmpty()) return;
    QVariantMap params;
    params["zpw_ver"]  = QString::number(API_VERSION);
    params["zpw_type"] = QString::number(API_TYPE);
    QString urlStr = buildRawUrl(m_chatServiceUrl + "/api/message/getrecentgroup", params);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onListenDone()));
}

void ZaloService::onListenDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;

    qDebug() << "[Zalo] listenTimer: poll OK";

    // DM: WS cmd=501 real-time, không cần poll HTTP
    if (!m_activeThreadId.isEmpty() && m_activeThreadIsGroup) {
        QVariantMap innerParams;
        innerParams["grid"]   = m_activeThreadId;
        innerParams["count"]  = 20;
        innerParams["lastId"] = m_lastPollMsgId.isEmpty() ? 0 : m_lastPollMsgId.toLongLong();

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
        QString urlStr = m_groupServiceUrl + "/api/group/history"
                       + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE)
                       + "&params=" + QUrl::toPercentEncoding(encParams);

        QNetworkReply *pollReply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
        pollReply->setProperty("threadId", m_activeThreadId);
        pollReply->setProperty("isGroup",  true);
        connect(pollReply, SIGNAL(finished()), this, SLOT(onPollMsgDone()));
    }
}

void ZaloService::setActiveThread(const QString &threadId, bool isGroup)
{
    bool changed = (m_activeThreadId != threadId);
    m_activeThreadId      = threadId;
    m_activeThreadIsGroup = isGroup;
    if (changed) {
        // Thread mới → reset poll state và clear stale DM request queue
        m_lastPollMsgId.clear();
        m_seenMsgIds.clear();
        m_pending510Toid.clear();
    }
    qDebug() << "[Zalo] setActiveThread:" << threadId << "isGroup:" << isGroup << "changed:" << changed;
}

void ZaloService::sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup)
{
    bb::platform::Notification *notif = new bb::platform::Notification(this);
    notif->setTitle(title);
    notif->setBody(body);

    bb::system::InvokeRequest req;
    // Must match <invoke-target id> in bar-descriptor.xml
    req.setTarget("com.BerryLife.Zalo10.invoke");
    req.setAction("bb.action.OPEN");
    req.setMimeType("text/plain");
    // Encode: "threadId|1" for group, "threadId|0" for DM
    QString data = threadId + "|" + (isGroup ? "1" : "0");
    req.setData(data.toUtf8());
    notif->setInvokeRequest(req);

    notif->notify();
    qDebug() << "[Zalo] Hub notification sent:" << title << body.left(40) << "data=" << data;
}

void ZaloService::clearActiveThread()
{
    qDebug() << "[Zalo] clearActiveThread (was:" << m_activeThreadId << ")";
    m_activeThreadId.clear();
    m_lastPollMsgId.clear();
    m_seenMsgIds.clear();
    m_pending510Toid.clear();
}

void ZaloService::onPollMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid    = reply->property("threadId").toString();
    bool isGroup   = reply->property("isGroup").toBool();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (tid != m_activeThreadId) return;

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap d;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        d = outer["data"].toMap();
    else
        d = outer;

    QVariantList rawMsgs = isGroup ? d["groupMsgs"].toList() : d["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d["data"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d["msgList"].toList();
    qDebug() << "[Zalo Poll] rawMsgs count:" << rawMsgs.size() << "lastPollMsgId=" << m_lastPollMsgId;

    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();
        QString msgId = m["msgId"].toString();
        if (msgId.isEmpty()) continue;

        if (m_seenMsgIds.contains(msgId)) continue;
        m_seenMsgIds.insert(msgId);

        qint64 newNum = msgId.toLongLong();
        qint64 curNum = m_lastPollMsgId.toLongLong();
        if (newNum > curNum) m_lastPollMsgId = msgId;

        QVariantMap out;
        out["msgId"]    = msgId;
        out["content"]  = m["content"].toString();
        out["senderId"] = m["uidFrom"].toString();
        out["dName"]    = m["dName"].toString();
        out["ts"]       = m["ts"].toString();
        out["isGroup"]  = isGroup;
        out["isMine"]   = (m["uidFrom"].toString() == m_uid);
        out["msgType"]  = m["msgType"].toInt();

        dbSaveMessage(out, tid);
        emit newMessage(tid, out);
    }
}

