#include "core/messages/MessagesCommon.hpp"

// Uses the chunked pipeline like sendVideo(): a single POST of a large file (e.g. 30MB) timed out
// or was rejected, and chunks give real progress. isFile=true routes progress to
// fileUploadProgress.
void ZaloService::sendFile(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Msg] sendFile: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString filename  = path.section('/', -1);
    QString clientId  = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString md5Checksum = QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toHex();

    const qint64 CHUNK_SIZE = 512 * 1024;
    int totalChunks = (int)((fileData.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    if (totalChunks < 1) totalChunks = 1;

    QVariantMap ctx;
    ctx["threadId"]    = threadId;
    ctx["isGroup"]     = isGroup;
    ctx["localPath"]   = "file://" + path;
    ctx["clientId"]    = clientId;
    ctx["fileName"]    = filename;
    ctx["fileSize"]    = (qint64)fileData.size();
    ctx["checksum"]    = md5Checksum;
    ctx["totalChunks"] = totalChunks;
    ctx["chunkIndex"]  = 1;
    ctx["fileData"]    = fileData;
    ctx["isFile"]      = true;
    m_pendingVideoChunkUpload[clientId] = ctx;

    emit fileUploadProgress(threadId, 0);
    sendVideoChunk(clientId);
}

void ZaloService::sendVideo(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Msg] sendVideo: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString filename = path.section('/', -1);
    qint64  ts        = QDateTime::currentMSecsSinceEpoch();
    QString clientId  = QString::number(ts);
    // checksum/checksumSha must be real MD5 values: with empty strings the server acked
    // asyncfile/msg (error_code=0) but never created the message.
    QString md5Checksum = QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toHex();

    // The server rejects any chunk over 512K (error_code 201). Upload <=512K chunks sequentially
    // via sendVideoChunk(); each success also gives a progress percentage. (The old single POST
    // silently failed for nearly every video.)
    const qint64 CHUNK_SIZE = 512 * 1024;
    int totalChunks = (int)((fileData.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    if (totalChunks < 1) totalChunks = 1;

    QVariantMap ctx;
    ctx["threadId"]    = threadId;
    ctx["isGroup"]     = isGroup;
    ctx["localPath"]   = "file://" + path;
    ctx["clientId"]    = clientId;
    ctx["fileName"]    = filename;
    ctx["fileSize"]    = (qint64)fileData.size();
    ctx["checksum"]    = md5Checksum;
    ctx["totalChunks"] = totalChunks;
    ctx["chunkIndex"]  = 1;
    ctx["fileData"]    = fileData;
    ctx["isFile"]      = false;
    m_pendingVideoChunkUpload[clientId] = ctx;

    emit videoUploadProgress(threadId, 0);
    sendVideoChunk(clientId);
}

void ZaloService::sendVideoChunk(const QString &clientId)
{
    if (!m_pendingVideoChunkUpload.contains(clientId)) return;
    QVariantMap ctx     = m_pendingVideoChunkUpload[clientId];
    QString threadId    = ctx["threadId"].toString();
    bool    isGroup     = ctx["isGroup"].toBool();
    QString filename    = ctx["fileName"].toString();
    QByteArray fileData = ctx["fileData"].toByteArray();
    int totalChunks     = ctx["totalChunks"].toInt();
    int chunkIndex      = ctx["chunkIndex"].toInt();

    const qint64 CHUNK_SIZE = 512 * 1024;
    qint64 offset  = (qint64)(chunkIndex - 1) * CHUNK_SIZE;
    qint64 thisLen = qMin((qint64)fileData.size() - offset, CHUNK_SIZE);
    QByteArray chunkData = fileData.mid(offset, thisLen);

    QString boundary = "----ZaloVideoBoundary" + clientId + "_" + QString::number(chunkIndex);

    QVariantMap p;
    if (isGroup) p["grid"] = threadId;
    else         p["toid"] = threadId;
    p["totalChunk"] = totalChunks;
    p["fileName"]   = filename;
    p["clientId"]   = clientId.toLongLong();
    p["totalSize"]  = (int)fileData.size();
    p["imei"]       = m_imei;
    p["isE2EE"]     = 0;
    p["jxl"]        = 0;
    p["chunkId"]    = chunkIndex;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString upEndpoint = isGroup ? "group" : "message";
    QString urlStr = fileBase + "/api/" + upEndpoint + "/asyncfile/upload"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0"
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QByteArray body;
    body += ("--" + boundary + "\r\n").toUtf8();
    body += ("Content-Disposition: form-data; name=\"chunkContent\"; filename=\"" + filename + "\"\r\n").toUtf8();
    body += QByteArray("Content-Type: application/octet-stream\r\n\r\n");
    body += chunkData + "\r\n";
    body += ("--" + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("clientId", clientId);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendVideoChunkUploadDone()));
}

void ZaloService::onSendVideoChunkUploadDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString clientId = reply->property("clientId").toString();
    bool ok          = (reply->error() == QNetworkReply::NoError);
    QByteArray raw   = reply->readAll();
    reply->deleteLater();

    if (!m_pendingVideoChunkUpload.contains(clientId)) return;
    QVariantMap ctx  = m_pendingVideoChunkUpload[clientId];
    QString threadId = ctx["threadId"].toString();
    bool    isFile   = ctx["isFile"].toBool();

    if (!ok) {
        qWarning() << "[Msg]" << (isFile ? "sendFile" : "sendVideo") << "upload chunk: network error" << reply->errorString();
        m_pendingVideoChunkUpload.remove(clientId);
        emit messageSent(false, threadId);
        return;
    }

    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qWarning() << "[Msg]" << (isFile ? "sendFile" : "sendVideo") << "upload chunk error:" << outer["error_message"].toString();
        m_pendingVideoChunkUpload.remove(clientId);
        emit messageSent(false, threadId);
        return;
    }

    int chunkIndex  = ctx["chunkIndex"].toInt();
    int totalChunks = ctx["totalChunks"].toInt();
    int pct = totalChunks > 0 ? (int)((qint64)chunkIndex * 100 / totalChunks) : 100;
    if (isFile) emit fileUploadProgress(threadId, pct);
    else        emit videoUploadProgress(threadId, pct);

    if (chunkIndex < totalChunks) {
        ctx["chunkIndex"] = chunkIndex + 1;
        m_pendingVideoChunkUpload[clientId] = ctx;
        sendVideoChunk(clientId);
        return;
    }

    QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());

    QVariantMap decOuter = jsonToMap(decStr.toUtf8());
    QVariantMap uploadData;
    QVariant dataVariant = decOuter["data"];
    if (dataVariant.type() == QVariant::Map) uploadData = dataVariant.toMap();
    else uploadData = jsonToMap(dataVariant.toString().toUtf8());

    QString fileId = uploadData["fileId"].toString();
    if (fileId.isEmpty()) fileId = uploadData["photoId"].toString();
    if (fileId.isEmpty() || fileId == "-1") {
        qWarning() << "[Msg]" << (isFile ? "sendFile" : "sendVideo") << "upload OK but no valid fileId";
        m_pendingVideoChunkUpload.remove(clientId);
        emit messageSent(false, threadId);
        return;
    }

    QVariantMap pending;
    pending["threadId"]  = threadId;
    pending["isGroup"]   = ctx["isGroup"].toBool();
    pending["localPath"] = ctx["localPath"].toString();
    pending["clientId"]  = clientId;
    pending["fileName"]  = ctx["fileName"].toString();
    pending["fileSize"]  = ctx["fileSize"].toLongLong();
    pending["checksum"]  = ctx["checksum"].toString();
    pending["isFile"]    = isFile;
    m_pendingVideoUpload[fileId] = pending;
    m_pendingVideoChunkUpload.remove(clientId);

}

void ZaloService::handleFileUploadDone(const QString &fileId, const QString &fileUrl)
{
    if (!m_pendingVideoUpload.contains(fileId)) return;
    QVariantMap pending = m_pendingVideoUpload.take(fileId);

    QString tid        = pending["threadId"].toString();
    bool    isGroup     = pending["isGroup"].toBool();
    QString localPath  = pending["localPath"].toString();
    QString clientId   = pending["clientId"].toString();
    QString fileName   = pending["fileName"].toString();
    qint64  fileSize   = pending["fileSize"].toLongLong();
    QString checksum   = pending["checksum"].toString();
    bool    isFile     = pending["isFile"].toBool();
    QString ext        = fileName.section('.', -1).toLower();

    QVariantMap mp;
    mp["fileId"]      = fileId;
    mp["checksum"]    = checksum;
    mp["checksumSha"] = "";
    mp["extention"]   = ext;
    mp["totalSize"]   = QString::number(fileSize);
    mp["fileName"]    = fileName;
    mp["clientId"]    = clientId;
    mp["fType"]       = 1;
    mp["fileCount"]   = 0;
    mp["fdata"]       = "{}";
    mp["fileUrl"]     = fileUrl;
    mp["zsource"]     = -1;
    mp["ttl"]         = 0;
    if (isGroup) mp["grid"] = tid;
    else         mp["toid"] = tid;

    QString encMsg = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(mp)));
    QByteArray body2 = "params=" + QUrl::toPercentEncoding(encMsg);

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString sendEndpoint = isGroup ? "group" : "message";
    QString msgUrl = fileBase + "/api/" + sendEndpoint + "/asyncfile/msg"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0";

    QNetworkRequest req2 = buildRequest(msgUrl, "https://chat.zalo.me/");
    req2.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QString fNameEsc = fileName;
    fNameEsc.replace("\\", "\\\\").replace("\"", "\\\"");
    QString contentJson = QString("{\"fileName\":\"%1\",\"href\":\"%2\",\"fileSize\":%3}")
                               .arg(fNameEsc).arg(fileUrl).arg(fileSize);

    QNetworkReply *r2 = m_manager->post(req2, body2);
    r2->setProperty("threadId",    tid);
    r2->setProperty("isGroup",     isGroup);
    r2->setProperty("contentJson", contentJson);
    r2->setProperty("clientId",    clientId);
    r2->setProperty("isFile",      isFile);
    connect(r2, SIGNAL(finished()), this, SLOT(onSendVideoMsgDone()));
}

void ZaloService::onSendVideoMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok             = (reply->error() == QNetworkReply::NoError);
    QString tid         = reply->property("threadId").toString();
    bool isGroup        = reply->property("isGroup").toBool();
    QString contentJson = reply->property("contentJson").toString();
    QString clientId    = reply->property("clientId").toString();
    bool isFile         = reply->property("isFile").toBool();
    QByteArray raw      = reply->readAll();
    reply->deleteLater();

    if (!ok) { emit messageSent(false, tid); return; }

    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qWarning() << "[Msg]" << (isFile ? "sendFile" : "sendVideo") << "send-msg error_code:" << outer["error_code"].toInt()
                 << outer["error_message"].toString();
        emit messageSent(false, tid);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
    QVariantMap data = jsonToMap(dec.toUtf8());
    qint64 msgIdInt = data["msgId"].toLongLong();

    if (msgIdInt == 0) {
        // msgId==0 is treated as a failure: a fabricated "sent" claimed success while the server never
        // created the message (later cmd=510 polls returned nothing). Failing cleans up the
        // local_video_/local_file_ placeholder (onMessageSent in ChatView.qml) so the user can retry.
        // The root cause is not fully understood.
        qWarning() << "[Msg]" << (isFile ? "sendFile" : "sendVideo") << "no msgId in response, reporting failure";
        emit messageSent(false, tid);
        return;
    }

    QString msgId = QString::number(msgIdInt);
    if (!m_seenMsgIds.contains(msgId)) {
        QVariantMap out;
        out["msgId"]    = msgId;
        out["cliMsgId"] = clientId;
        out["content"]  = contentJson;
        out["msgType"]  = 3;
        out["isMine"]   = true;
        out["isGroup"]  = isGroup;
        out["senderId"] = m_uid;
        out["dName"]    = m_displayName;
        out["ts"]       = QString::number(QDateTime::currentMSecsSinceEpoch());
        m_seenMsgIds.insert(msgId);
        dbSaveMessage(out, tid);
        emit newMessage(tid, out);
    }
    emit messageSent(true, tid);
}
