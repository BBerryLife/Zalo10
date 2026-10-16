#include "core/ZaloService.hpp"
#include "utils/ZaloServiceUtils.hpp"
#include "core/HubIntegration.hpp"
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/platform/NotificationDialog>
#include <bb/system/InvokeRequest>
#include <bb/system/InvokeManager>

#include <QSettings>
#include <QTimer>
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

static const int GROUP_SYNC_NEXT_DELAY_MS = 250;

void ZaloService::fetchMessages(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn) return;

    if (isGroup) {
        QVariantMap innerParams;
        innerParams["grid"]  = threadId;
        innerParams["count"] = 50;

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
        QString urlStr = m_groupServiceUrl + "/api/group/history"
                       + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE)
                       + "&params=" + QUrl::toPercentEncoding(encParams);

        QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
        reply->setProperty("threadId", threadId);
        reply->setProperty("isGroup",  true);
        connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDone()));
    } else {
        // DMs use WS cmd=510 requestOldMessages (zca-js listen.js); HTTP /api/message/getmsglist doesn't
        // exist (404).
        m_pending510Toid = threadId;
        if (!m_wsConnected || !m_webSocket) {
            if (!m_zpwWsUrls.isEmpty()) connectWebSocket();
            return;
        }
        QString lastId = m_threadLastMsgId.value(threadId, "0");
        QString req510 = QString("{\"first\":true,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                         .arg(lastId).arg(threadId);
        sendWsRequest(510, 1, req510);
    }
}

void ZaloService::onFetchMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid    = reply->property("threadId").toString();
    bool syncGroup = reply->property("syncGroup").toBool();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (syncGroup && (raw.contains("429 Too Many Requests") || raw.contains("<html"))) {
        qWarning() << "[Sync] group history unavailable (HTML/404/429 response), stopping group sync";
        m_sync.groupQueue.clear();
        QTimer::singleShot(0, this, SLOT(onSyncGroupNext()));
        return;
    }
    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Msg] fetchMessages error:" << root["error_message"].toString();
        if (syncGroup) {
            QTimer::singleShot(GROUP_SYNC_NEXT_DELAY_MS, this, SLOT(onSyncGroupNext()));
            return;
        }
        emit messagesReady(tid, QVariantList());
        return;
    }

    QVariantList msgs;
    bool isGroup = reply->property("isGroup").toBool();

    QString dec2 = aesDecryptBase64(m_secretKey, root["data"].toString());

    QVariantMap outer2 = jsonToMap(dec2.toUtf8());
    QVariantMap d2;
    if (outer2.contains("data") && outer2["data"].type() == QVariant::Map)
        d2 = outer2["data"].toMap();
    else
        d2 = outer2;

    QVariantList rawMsgs = isGroup ? d2["groupMsgs"].toList() : d2["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["data"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["msgList"].toList();

    qint64 maxMsgNum = -1;
    QString newestMsgId;

    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();

        if (isGroupPollNotice(m)) continue;

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

        QString delMsgIdG, deleterUidG;
        if (extractDeleteInfo(m, delMsgIdG, deleterUidG)) {
            if (deleterUidG == m_uid) {
                markMessageDeletedForMe(tid, delMsgIdG);
                for (int pj = 0; pj < msgs.size(); ++pj) {
                    if (msgs[pj].toMap()["msgId"].toString() == delMsgIdG) {
                        msgs.removeAt(pj);
                        break;
                    }
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
        int mt = m["msgType"].toInt();
        // History can also send msgType as a string ("share.file", "chat.recommended") like the realtime
        // WS: fall back to name matching when toInt() fails.
        if (mt == 0) {
            QString mtStr = m["msgType"].toString().toLower();
            if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                mt = 3;
            else if (mtStr.contains("chat.recommended")) {
                QVariantMap peekMap = m["content"].toMap();
                if (peekMap.isEmpty()) {
                    QString peekStr = m["content"].toString();
                    if (!peekStr.isEmpty() && peekStr.trimmed().startsWith("{"))
                        peekMap = jsonToMap(peekStr.toUtf8());
                }
                QString peekAction = peekMap["action"].toString();
                mt = peekAction.contains("link") ? 6 : 4;
            }
            else if (mtStr.contains("chat.sticker"))
                mt = 5;
        }
        QString rawContent = m["content"].toString();
        if (mt == 2) {
            rawContent = normalizePhotoContent(m, rawContent);
        } else if (mt == 5) {
            QVariantMap sm = m["content"].toMap();
            if (sm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
                sm = jsonToMap(rawContent.toUtf8());
            qint64 stickerId = sm["id"].toString().toLongLong();
            if (stickerId == 0) stickerId = sm["id"].toLongLong();
            rawContent = QString("{\"stickerId\":%1}").arg(stickerId);
            if (stickerId > 0 && !syncGroup) downloadSticker(QString::number(stickerId));
        } else if (mt == 6) {
            QVariantMap lm = m["content"].toMap();
            if (lm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
                lm = jsonToMap(rawContent.toUtf8());
            QString lTitle = lm["title"].toString();
            QString lDesc  = lm["description"].toString();
            QString lHref  = lm["href"].toString();
            QString lThumb = lm["thumb"].toString();
            QString esc = lTitle; esc.replace("\\", "\\\\").replace("\"", "\\\"");
            QString descEsc = lDesc; descEsc.replace("\\", "\\\\").replace("\"", "\\\"");
            QString hrefEsc = lHref; hrefEsc.replace("\\", "\\\\").replace("\"", "\\\"");
            QString thumbEsc = lThumb; thumbEsc.replace("\\", "\\\\").replace("\"", "\\\"");
            rawContent = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                             .arg(esc).arg(descEsc).arg(hrefEsc).arg(thumbEsc);
        } else if (mt == 4) {
            QVariantMap rm = m["content"].toMap();
            if (rm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
                rm = jsonToMap(rawContent.toUtf8());
            QString action = rm["action"].toString();
            QVariant paramsV = rm["params"];
            QVariantMap paramsMap = (paramsV.type() == QVariant::String)
                ? jsonToMap(paramsV.toString().toUtf8())
                : paramsV.toMap();
            QString callResult = action.contains("misscall") ? "missed" : "ended";
            QString callKind   = (paramsMap["calltype"].toInt() == 1) ? "video" : "voice";
            qint64  duration   = paramsMap["duration"].toString().toLongLong();
            rawContent = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}")
                             .arg(callResult).arg(callKind).arg(duration);
        } else if (mt == 3) {
            QVariantMap fm = m["content"].toMap();
            if (fm.isEmpty() && !rawContent.isEmpty() && rawContent.trimmed().startsWith("{"))
                fm = jsonToMap(rawContent.toUtf8());
            QString fTitle = fm["title"].toString();
            QString fHref  = fm["href"].toString();
            qint64  fSize  = 0;
            QVariant paramsV = fm["params"];
            QVariantMap paramsMap = (paramsV.type() == QVariant::String)
                ? jsonToMap(paramsV.toString().toUtf8())
                : paramsV.toMap();
            if (!paramsMap.isEmpty())
                fSize = paramsMap["fileSize"].toString().toLongLong();
            if (!fHref.isEmpty()) {
                QString fTitleEsc = fTitle;
                fTitleEsc.replace("\\", "\\\\").replace("\"", "\\\"");
                rawContent = QString("{\"fileName\":\"%1\",\"href\":\"%2\"").arg(fTitleEsc).arg(fHref);
                if (fSize > 0) rawContent += QString(",\"fileSize\":%1").arg(fSize);
                rawContent += "}";
            }
        }
        out["msgType"]  = mt;
        out["content"]  = rawContent;
        msgs.append(out);

        if (!msgId.isEmpty()) {
            qint64 num = msgId.toLongLong();
            if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
        }
    }
    if (!newestMsgId.isEmpty() && !syncGroup)
        m_lastPollMsgId = newestMsgId;

    if (!syncGroup) {
        m_seenMsgIds.clear();
        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap mm = msgs[i].toMap();
            if (mm["isMine"].toBool()) {
                QString mid = mm["msgId"].toString();
                if (!mid.isEmpty()) m_seenMsgIds.insert(mid);
            }
        }
    }

    bool syncTx = (syncGroup && m_db && sqlite3_exec(m_db, "BEGIN", 0, 0, 0) == SQLITE_OK);
    for (int i = 0; i < msgs.size(); ++i)
        dbSaveMessage(msgs[i].toMap(), tid);
    if (syncTx) sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

    if (syncGroup) {
        m_sync.groupMsgs += msgs.size();
        if (tid == m_activeThreadId)
            emit messagesReady(tid, msgs);
        QTimer::singleShot(GROUP_SYNC_NEXT_DELAY_MS, this, SLOT(onSyncGroupNext()));
        return;
    }

    emit messagesReady(tid, msgs);
}

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

    // Inline base64 images (previewThumb from realtime photo messages) start with base64 data,
    // not "http".
    if (!url.startsWith("http")) {
        if (m_avatarCache.contains(url)) {
            QSize sz = imageDimensions(m_avatarCache[url]);
            emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
            return;
        }
        QByteArray imgData = QByteArray::fromBase64(url.toUtf8());
        if (!imgData.isEmpty()) {
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
                if (imgData.size() > 6 &&
                    (unsigned char)imgData[0] == 0x03 &&
                    (unsigned char)imgData[1] == 0x00 &&
                    (unsigned char)imgData[5] == 0xFF &&
                    (unsigned char)imgData[6] == 0xDA)
                {
                    int W = (unsigned char)imgData[2];
                    int H = (unsigned char)imgData[4];
                    QByteArray sosData = imgData.mid(5);

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

                    QByteArray dqt0; dqt0 += (char)0x00;
                    dqt0 += QByteArray((const char*)lumaQ, 64);
                    QByteArray dqt1; dqt1 += (char)0x01;
                    dqt1 += QByteArray((const char*)chromaQ, 64);

                    // Build bytes with explicit (char) casts, NOT string literals like "\x01\x22\x00":
                    // QByteArray::operator+=(const char*) stops at the first \x00 and silently drops the Y
                    // component's qtable byte, corrupting every following marker.
                    QByteArray sof0Data;
                    sof0Data += (char)8;
                    sof0Data += (char)((H >> 8) & 0xFF);
                    sof0Data += (char)(H & 0xFF);
                    sof0Data += (char)((W >> 8) & 0xFF);
                    sof0Data += (char)(W & 0xFF);
                    sof0Data += (char)3;
                    sof0Data += (char)0x01;
                    sof0Data += (char)0x22;
                    sof0Data += (char)0x00;
                    sof0Data += (char)0x02;
                    sof0Data += (char)0x11;
                    sof0Data += (char)0x01;
                    sof0Data += (char)0x03;
                    sof0Data += (char)0x11;
                    sof0Data += (char)0x01;

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
                    jpeg += "\xFF\xD8";

                    static const char app0Data[] = {
                        'J','F','I','F','\x00',
                        '\x01','\x01',
                        '\x00',
                        '\x00','\x01','\x00','\x01',
                        '\x00','\x00'
                    };
                    jpeg += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data)));

                    jpeg += makeJpegMarker(0xDB, dqt0);
                    jpeg += makeJpegMarker(0xDB, dqt1);
                    jpeg += makeJpegMarker(0xC0, sof0Data);
                    jpeg += makeJpegMarker(0xC4, dhtDcLum);
                    jpeg += makeJpegMarker(0xC4, dhtDcChr);
                    jpeg += makeJpegMarker(0xC4, dhtAcLum);
                    jpeg += makeJpegMarker(0xC4, dhtAcChr);
                    jpeg += sosData;
                    jpeg += "\xFF\xD9";

                    // msgId as file name: a unique path per message avoids BB10's stale image cache. Always PNG
                    // (BB10's ImageView is more reliable with it). Literal "/tmp/", not QDir::tempPath() (wiped
                    // on restart).
                    QString tmpPath = "/tmp/msgthumb_" +
                                      msgId + ".png";
                    QFile::remove(tmpPath);

                    // Byte-stuff scan data: every 0xFF in entropy-coded data must be followed by 0x00. Zalo's raw
                    // SOS data has bare 0xFF bytes that caused "Bogus marker length" in BB10's libjpeg.
                    QByteArray stuffedSos;
                    int sosHeaderLen = 0;
                    if (sosData.size() >= 4) {
                        int segLen = ((unsigned char)sosData[2] << 8) | (unsigned char)sosData[3];
                        sosHeaderLen = 2 + segLen;
                    }
                    stuffedSos += sosData.left(sosHeaderLen);
                    for (int si = sosHeaderLen; si < sosData.size(); ++si) {
                        unsigned char b = (unsigned char)sosData[si];
                        stuffedSos += (char)b;
                        if (b == 0xFF) {
                            // Skip stuffing if the next byte is already 0x00, a restart marker (D0-D7) or EOI (D9).
                            unsigned char next = (si + 1 < sosData.size())
                                                 ? (unsigned char)sosData[si + 1] : 0x00;
                            if (next != 0x00 &&
                                !(next >= 0xD0 && next <= 0xD7) &&
                                next != 0xD9) {
                                stuffedSos += (char)0x00;
                            }
                        }
                    }

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

                    QImage qimg;
                    bool decoded = qimg.loadFromData(jpeg2, "JPEG");
                    if (!decoded) decoded = qimg.loadFromData(jpeg, "JPEG");

                    if (decoded) {
                        if (qimg.width() < 120 || qimg.height() < 120) {
                            qimg = qimg.scaled(240, 240,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
                        }
                        qimg.save(tmpPath, "PNG");
                    } else {
                        QImage placeholder(240, 240, QImage::Format_RGB32);
                        placeholder.fill(QColor(37, 117, 252));
                        placeholder.save(tmpPath, "PNG");
                        qWarning() << "[Msg] thumb decode failed, saved placeholder msgId=" << msgId;
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
                qWarning() << "[Msg] image: unrecognised format, msgId=" << msgId;
                return;
            }

            QString tmpPath = "/tmp/msgimg_" +
                              QString::number(qHash(url)) + "." + ext;
            QFile f(tmpPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
                QString filePath = "file://" + tmpPath;
                m_avatarCache[url] = filePath;
                QSize dim = imageDimensions(filePath);
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
                return;
            }
        }
        qWarning() << "[Msg] downloadImageMessage: non-http url and base64 decode failed, msgId=" << msgId;
        return;
    }

    if (m_avatarCache.contains(url)) {
        QSize sz = imageDimensions(m_avatarCache[url]);
        emit imageMsgReady(msgId, m_avatarCache[url], sz.width(), sz.height());
        return;
    }

    // Reuse an image already on disk (from a previous session): downloadImageMessage() can be called
    // directly (resync, jump-to-message) without going through dbLoadMessages().
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
        return;
    }

    QString ext = "jpg";
    QByteArray ct = reply->rawHeader("Content-Type");
    if (ct.contains("png"))  ext = "png";
    if (ct.contains("gif"))  ext = "gif";
    if (ct.contains("webp")) ext = "webp";

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
            }
        }
    }

    // File name per message (msgId, not md5(url)): Zalo's CDN URLs for the same image change between
    // fetches, and msgId makes refetches overwrite the same file and keeps the QFile::exists()
    // check working. Literal "/tmp/" (not QDir::tempPath()): the persistent root that survives
    // restart and relogin.
    QString stableKey = msgId.isEmpty() ? md5Hex(url) : msgId;
    QString tmpPath = "/tmp/zalo_img_" + stableKey + "." + ext;
    QFile f(tmpPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(finalData); f.close(); }
    QString filePath = "file://" + tmpPath;
    m_avatarCache[url] = filePath;
    QSize dim = imageDimensions(filePath);

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
    if (m_wsConnected) {
        sendWsPing();

        if (!m_activeThreadIsGroup && !m_activeThreadId.isEmpty()) {
            QString req510 = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                             .arg(m_lastPollMsgId.isEmpty() ? "0" : m_lastPollMsgId)
                             .arg(m_activeThreadId);
            sendWsRequest(510, 1, req510);
        }
    } else {
        if (!m_wsReconnectTimer->isActive())
            m_wsReconnectTimer->start(wsNextReconnectDelayMs());
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
        m_lastPollMsgId.clear();
        m_seenMsgIds.clear();
        m_pending510Toid.clear();
    }
    m_hub->markThreadRead(threadId);
    setThreadUnread(threadId, false);
    emit threadRead(threadId);
}

void ZaloService::sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup)
{
    // Don't use bb::platform::Notification here: UDS items (upsertThreadItem()) ALWAYS create a Hub
    // row regardless of notification_state, so a parallel Notification::notify() produced duplicate
    // rows. The UDS item with notification_state=true handles banner/sound/preview on its own.
    m_hub->upsertThreadItem(threadId, isGroup, title, body, QDateTime::currentMSecsSinceEpoch());
}

// Banner via NotificationDialog, separate from sendHubNotification(): Notification banners only
// show while the app is NOT in the foreground, while show() appears immediately. It's a real
// dialog the user must dismiss (BB10 has no auto-expiring toast). Tapping it just foregrounds
// the app; threadId/isGroup are unused (kept symmetrical with sendHubNotification()).
void ZaloService::sendBannerNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup)
{
    Q_UNUSED(threadId);
    Q_UNUSED(isGroup);

    if (!m_appForeground) {
        return;
    }

    bb::platform::NotificationDialog *dlg = new bb::platform::NotificationDialog(this);
    dlg->setTitle(title);
    dlg->setBody(body);

    connect(dlg, SIGNAL(finished(bb::platform::NotificationResult::Type)),
            dlg, SLOT(deleteLater()));

    dlg->show();
}

bool ZaloService::isGroupHubThread(const QString &threadId) const
{
    if (!m_hub) return false;
    return m_hub->isGroupThread(threadId);
}

bool ZaloService::handleHubAction(const QString &action, const QString &threadId)
{
    if (!m_hub || threadId.isEmpty() || !HubIntegration::isItemAction(action)) return false;

    if (action == QLatin1String(HubIntegration::ACTION_MARK_READ)) {
        m_hub->markThreadRead(threadId);
        setThreadUnread(threadId, false);
        emit threadRead(threadId);
    } else if (action == QLatin1String(HubIntegration::ACTION_MARK_UNREAD)) {
        m_hub->markThreadUnread(threadId);
        setThreadUnread(threadId, true);
        emit threadMarkedUnread(threadId);
    } else if (action == QLatin1String(HubIntegration::ACTION_DELETE)) {
        m_hub->removeThreadItem(threadId);
    }
    return true;
}

QStringList ZaloService::unreadThreadIds() const
{
    QSettings s("BerryLife", "Zalo10");
    return s.value("unreadThreads").toStringList();
}

void ZaloService::setThreadUnread(const QString &threadId, bool unread)
{
    if (threadId.isEmpty()) return;
    QSettings s("BerryLife", "Zalo10");
    QStringList ids = s.value("unreadThreads").toStringList();
    bool has = ids.contains(threadId);
    if (unread && !has)      ids.append(threadId);
    else if (!unread && has) ids.removeAll(threadId);
    else return;
    s.setValue("unreadThreads", ids);
}

void ZaloService::clearActiveThread()
{
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
        out["cliMsgId"] = m["cliMsgId"].toString();
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

