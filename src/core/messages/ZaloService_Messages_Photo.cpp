#include "core/messages/MessagesCommon.hpp"

QString ZaloService::cacheLocalImage(const QString &sourcePath)
{
    QString path = sourcePath;
    if (path.startsWith("file://")) path = path.mid(7);
    if (path.isEmpty() || !QFile::exists(path)) {
        return sourcePath;
    }

    QString ext = path.section('.', -1).toLower();
    if (ext.isEmpty() || ext.length() > 4) ext = "jpg";
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QString destPath = "/tmp/zalo_img_local_" + QString::number(ts) + "." + ext;

    if (!QFile::copy(path, destPath)) {
        qWarning() << "[Msg] cacheLocalImage: copy failed" << path << "->" << destPath
                  << "- falling back to original path";
        return path;
    }
    return destPath;
}

void ZaloService::deleteLocalFile(const QString &path)
{
    QString p = path;
    if (p.startsWith("file://")) p = p.mid(7);
    if (p.isEmpty()) return;
    if (QFile::exists(p)) {
        QFile::remove(p);
    }
}

void ZaloService::sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup, const QString &caption)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[Msg] sendPhoto: cannot open" << path;
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

    QString clientId = QString::number(ts);
    QVariantMap pendingInfo;
    pendingInfo["localPath"] = "file://" + path;
    pendingInfo["fileSize"]  = (qint64)fileData.size();
    pendingInfo["fileName"]  = filename;
    m_pendingSentPhotoInfo[clientId] = pendingInfo;

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
    // imei must be a separate multipart field, not part of the AES params.
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

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId",  threadId);
    reply->setProperty("localPath", "file://" + path);
    reply->setProperty("isGroup",   isGroup);
    reply->setProperty("caption",   caption);
    reply->setProperty("clientId",  clientId);
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
    QString clientId = reply->property("clientId").toString();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();

    if (!ok) { m_pendingSentPhotoInfo.remove(clientId); emit messageSent(false, tid); return; }

    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qWarning() << "[Msg] sendPhoto upload error:" << outer["error_message"].toString();
        m_pendingSentPhotoInfo.remove(clientId);
        emit messageSent(false, tid);
        return;
    }
    QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());

    QVariantMap decOuter = jsonToMap(decStr.toUtf8());
    QVariantMap uploadData;
    QVariant dataVariant = decOuter["data"];
    if (dataVariant.type() == QVariant::Map) {
        uploadData = dataVariant.toMap();
    } else {
        uploadData = jsonToMap(dataVariant.toString().toUtf8());
    }

    QString normalUrl = uploadData["normalUrl"].toString();
    QString thumbUrl  = uploadData["thumbUrl"].toString();
    QString hdUrl     = uploadData["hdUrl"].toString();
    QString photoId   = uploadData["photoId"].toString();
    if (thumbUrl.isEmpty()) thumbUrl = hdUrl;
    if (thumbUrl.isEmpty()) thumbUrl = normalUrl;

    if (normalUrl.isEmpty()) {
        qWarning() << "[Msg] sendPhoto: upload OK but no URL returned";
        m_pendingSentPhotoInfo.remove(clientId);
        emit messageSent(false, tid);
        return;
    }

    QSize photoDim = imageDimensions(localPath);
    QVariantMap mp;
    mp["photoId"]   = photoId;
    // Reuse the clientId from sendPhoto() so it matches m_pendingSentPhotoInfo and the cliMsgId of
    // the WS echo.
    mp["clientId"]  = clientId;
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

    QString captionEsc = caption;
    captionEsc.replace("\\", "\\\\").replace("\"", "\\\"")
              .replace("\n", "\\n").replace("\r", "\\r");
    QVariantMap pInfo = m_pendingSentPhotoInfo.value(clientId);
    qint64 origFileSize = pInfo.value("fileSize", 0).toLongLong();
    QString origFileName = pInfo.value("fileName").toString();
    origFileName.replace("\\", "\\\\").replace("\"", "\\\"");

    QString contentJson = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"")
                               .arg(normalUrl).arg(thumbUrl)
                               .arg(hdUrl.isEmpty() ? normalUrl : hdUrl);
    if (origFileSize > 0) contentJson += QString(",\"fileSize\":%1").arg(origFileSize);
    if (!origFileName.isEmpty()) contentJson += QString(",\"fileName\":\"%1\"").arg(origFileName);
    if (!captionEsc.isEmpty()) contentJson += QString(",\"caption\":\"%1\"").arg(captionEsc);
    contentJson += "}";

    QNetworkReply *r2 = m_manager->post(req2, body2);
    r2->setProperty("threadId",    tid);
    r2->setProperty("localPath",   localPath);
    r2->setProperty("isGroup",     isGroup);
    r2->setProperty("contentJson", contentJson);
    r2->setProperty("caption",     caption);
    r2->setProperty("clientId",    clientId);
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
    QString clientId    = reply->property("clientId").toString();
    QByteArray raw      = reply->readAll();
    reply->deleteLater();

    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap data = jsonToMap(dec.toUtf8());
            qint64 msgIdInt = data["msgId"].toLongLong();

            if (msgIdInt == 0) {
                // photo_original/send returns no real server msgId. Saving a locally fabricated one would create
                // a SECOND permanent row when the WS echo (cmd=501) arrives (dbSaveMessage() keys on msgId).
                // So don't persist here: leave the "local_img_" placeholder and let the WS echo save the row
                // and replace it.
            } else {
            QString msgId = QString::number(msgIdInt);
            if (m_seenMsgIds.contains(msgId)) {
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
            if (!clientId.isEmpty()) m_pendingSentPhotoInfo.remove(clientId);
            }
        } else {
            ok = false;
            qWarning() << "[Msg] sendPhoto send-msg error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
            if (!clientId.isEmpty()) m_pendingSentPhotoInfo.remove(clientId);
        }
    } else if (!clientId.isEmpty()) {
        m_pendingSentPhotoInfo.remove(clientId);
    }
    emit messageSent(ok, tid);
}
