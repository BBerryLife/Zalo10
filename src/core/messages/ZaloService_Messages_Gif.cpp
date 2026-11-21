#include "core/messages/MessagesCommon.hpp"

// Sticker sending. Stickers in the Shop packs are plain image files, not Zalo catalog stickers, so
// they go out as attachments: jpg through the existing photo path, everything else through the `gif`
// endpoint exactly like zca-js sendMessage() does (upthumb for the preview image, then
// POST <file service>/api/{message|group}/gif with the md5 checksum).

// GIF header: "GIF89a" then width and height as little-endian 16-bit values. Read directly because
// QImage may not have a GIF plugin on this device.
static QSize gifDimensionsFromData(const QByteArray &d)
{
    if (d.size() < 10 || !d.startsWith("GIF8")) return QSize();
    int w = (unsigned char)d[6] | ((unsigned char)d[7] << 8);
    int h = (unsigned char)d[8] | ((unsigned char)d[9] << 8);
    return QSize(w, h);
}

void ZaloService::sendSticker(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;
    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    if (path.isEmpty() || !QFile::exists(path)) {
        qWarning() << "[Sticker] sendSticker: file not found" << path;
        emit messageSent(false, threadId);
        return;
    }
    // photo_original re-encodes everything to JPEG, which turns transparent PNG/WebP stickers into
    // white boxes. The `gif` endpoint stores the file as it is, so every format that can carry
    // transparency goes through it; only JPEG (no alpha anyway) keeps the normal photo path.
    QString lower = path.toLower();
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) sendPhoto(threadId, path, isGroup, QString());
    else                                                   sendGif(threadId, path, isGroup);
}

void ZaloService::sendGif(const QString &threadId, const QString &path, bool isGroup)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Sticker] sendGif: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    qint64  ts       = QDateTime::currentMSecsSinceEpoch();
    QString clientId = QString::number(ts);
    QString boundary = "----ZaloBoundary" + clientId;

    QVariantMap pendingInfo;
    pendingInfo["localPath"] = "file://" + path;
    pendingInfo["fileSize"]  = (qint64)fileData.size();
    pendingInfo["fileName"]  = path.section('/', -1);
    m_pendingSentPhotoInfo[clientId] = pendingInfo;

    // Step 1: upthumb. zca-js uploads the GIF bytes themselves as the thumbnail ("blob", image/png)
    // and always uses the 1-1 file service URL for it, group or not.
    QVariantMap p;
    p["clientId"] = clientId;
    p["imei"]     = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString urlStr = fileBase + "/api/message/upthumb"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QByteArray body;
    body += (QString("--") + boundary + "\r\n").toUtf8();
    body += QByteArray("Content-Disposition: form-data; name=\"fileContent\"; filename=\"blob\"\r\n");
    body += QByteArray("Content-Type: image/png\r\n\r\n");
    body += fileData;
    body += "\r\n";
    body += (QString("--") + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId",  threadId);
    reply->setProperty("localPath", path);
    reply->setProperty("isGroup",   isGroup);
    reply->setProperty("clientId",  clientId);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendGifThumbDone()));
}

void ZaloService::onSendGifThumbDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok          = (reply->error() == QNetworkReply::NoError);
    QString tid      = reply->property("threadId").toString();
    QString path     = reply->property("localPath").toString();
    bool isGroup     = reply->property("isGroup").toBool();
    QString clientId = reply->property("clientId").toString();
    QByteArray raw   = reply->readAll();
    reply->deleteLater();

    QString thumbUrl;
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap decOuter = jsonToMap(decStr.toUtf8());
            QVariant dv = decOuter["data"];
            QVariantMap up = (dv.type() == QVariant::Map) ? dv.toMap() : jsonToMap(dv.toString().toUtf8());
            thumbUrl = up["url"].toString();
            if (thumbUrl.isEmpty()) thumbUrl = up["hdUrl"].toString();
        } else {
            qWarning() << "[Sticker] upthumb error:" << outer["error_message"].toString();
        }
    }
    QFile file(path);
    if (thumbUrl.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Sticker] sendGif: upthumb failed";
        m_pendingSentPhotoInfo.remove(clientId);
        emit messageSent(false, tid);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    // Step 2: the gif message itself.
    QString fileName = path.section('/', -1);
    QSize dim = gifDimensionsFromData(fileData);
    if (dim.width() <= 0) dim = imageDimensions(path);

    QVariantMap p;
    p["clientId"]   = clientId;   // same id as the pending placeholder so the WS echo can match it
    p["fileName"]   = fileName;
    p["totalSize"]  = (int)fileData.size();
    p["width"]      = dim.width();
    p["height"]     = dim.height();
    p["msg"]        = QString();
    p["type"]       = 1;
    p["ttl"]        = 0;
    if (isGroup) { p["visibility"] = 0; p["grid"] = tid; }
    else         { p["toid"] = tid; }
    p["thumb"]      = thumbUrl;
    p["checksum"]   = QString::fromLatin1(QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toHex());
    p["totalChunk"] = 1;
    p["chunkId"]    = 1;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString urlStr = fileBase + "/api/" + (isGroup ? "group" : "message") + "/gif"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0"
                   + "&params=" + QUrl::toPercentEncoding(encParams)
                   + "&type=1";

    QString boundary = "----ZaloBoundary" + clientId + "g";
    QString safeName = fileName;
    safeName.replace("\"", "_");
    QByteArray body;
    body += (QString("--") + boundary + "\r\n").toUtf8();
    body += (QString("Content-Disposition: form-data; name=\"chunkContent\"; filename=\"") + safeName + "\"\r\n").toUtf8();
    body += QByteArray("Content-Type: application/octet-stream\r\n\r\n");
    body += fileData;
    body += "\r\n";
    body += (QString("--") + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);

    QNetworkReply *r2 = m_manager->post(req, body);
    r2->setProperty("threadId",  tid);
    r2->setProperty("localPath", "file://" + path);
    r2->setProperty("isGroup",   isGroup);
    r2->setProperty("clientId",  clientId);
    connect(r2, SIGNAL(finished()), this, SLOT(onSendGifDone()));
}

void ZaloService::onSendGifDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok          = (reply->error() == QNetworkReply::NoError);
    QString tid      = reply->property("threadId").toString();
    QString localPath = reply->property("localPath").toString();
    bool isGroup     = reply->property("isGroup").toBool();
    QString clientId = reply->property("clientId").toString();
    QByteArray raw   = reply->readAll();
    reply->deleteLater();

    QVariantMap pInfo = m_pendingSentPhotoInfo.value(clientId);
    m_pendingSentPhotoInfo.remove(clientId);

    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap decOuter = jsonToMap(dec.toUtf8());
            QVariantMap data = decOuter.contains("data") && decOuter["data"].type() == QVariant::Map
                                   ? decOuter["data"].toMap() : decOuter;
            qint64 msgIdInt = data["msgId"].toLongLong();

            // Like photo_original/send: when no real msgId comes back, leave the "local_img_"
            // placeholder alone and let the WS echo save the row, or it would be stored twice.
            if (msgIdInt != 0) {
                QString msgId = QString::number(msgIdInt);
                QSize dim = imageDimensions(localPath);
                if (m_seenMsgIds.contains(msgId)) {
                    if (!localPath.isEmpty() && m_db) {
                        const char *sql = "UPDATE messages SET localImage=?, imgWidth=?, imgHeight=? WHERE msgId=?";
                        sqlite3_stmt *stmt = 0;
                        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, localPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(stmt,  2, dim.width());
                            sqlite3_bind_int(stmt,  3, dim.height());
                            sqlite3_bind_text(stmt, 4, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt);
                            sqlite3_finalize(stmt);
                        }
                    }
                } else {
                    QString fname = pInfo.value("fileName").toString();
                    fname.replace("\\", "\\\\").replace("\"", "\\\"");
                    QString contentJson = QString("{\"fileSize\":%1,\"fileName\":\"%2\"}")
                                              .arg(pInfo.value("fileSize", 0).toLongLong()).arg(fname);
                    QVariantMap out;
                    out["msgId"]      = msgId;
                    out["cliMsgId"]   = clientId;
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
            }
        } else {
            ok = false;
            qWarning() << "[Sticker] gif send error_code:" << outer["error_code"].toInt()
                       << outer["error_message"].toString();
        }
    }
    emit messageSent(ok, tid);
}
