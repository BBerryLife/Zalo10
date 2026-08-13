#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include "HubIntegration.hpp"
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
#include <bb/platform/NotificationDialog>
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

        // chat.undo = thông báo thu hồi, không phải tin nhắn thật. Patch tin
        // gốc nếu nó nằm trước đó trong cùng batch history, lưu recall vào
        // SQLite dù thế nào, bỏ qua không thêm event này thành bubble riêng.
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

        // chat.delete = thông báo "xóa cho tôi". Cùng guard self-only như
        // mọi nơi khác — Zalo gửi cho cả 2 phía nhưng chỉ nên xóa màn hình
        // của người thực sự bấm xóa.
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
        // Zalo history cũng có thể gửi msgType dạng chuỗi ("share.file")
        // thay vì số, giống WS real-time (xem ZaloService_WebSocket.cpp) —
        // fallback theo tên chuỗi khi toInt() không parse được số.
        if (mt == 0) {
            QString mtStr = m["msgType"].toString().toLower();
            if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                mt = 3;
        }
        QString rawContent = m["content"].toString();
        if (mt == 2) {
            rawContent = normalizePhotoContent(m, rawContent);
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

// Fetch URL ảnh full-res qua WS cmd=510 history. Gọi khi tin nhắn real-time
// qua WS chỉ mang previewThumb protobuf (không có URL HTTP). Gửi cmd=510
// với lastId=(msgId-1) để server trả về đúng msgId đó và các tin mới hơn,
// cho phép trích ra normalUrl/hdUrl CDN thật từ field content.
void ZaloService::fetchPhotoViaWs510(const QString &msgId, const QString &threadId)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;
    if (!m_wsConnected || !m_webSocket) {
        qDebug() << "[Zalo] fetchPhotoViaWs510: WS not connected, skipping msgId=" << msgId;
        return;
    }

    // Track msgId -> threadId so cmd=510 handler knows this is a photo fetch
    m_pendingPhotoMsgIds[msgId] = threadId;

    // Xin lastId = (msgId - 1) để server trả về đúng msgId trong response —
    // gửi lastId=msgId sẽ xin tin nhắn CŨ HƠN nó, trả về rỗng nếu msgId là
    // tin mới nhất trong thread.
    qint64 lastIdNum = msgId.toLongLong() - 1;
    QString lastIdStr = lastIdNum > 0 ? QString::number(lastIdNum) : "0";
    QString req = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                  .arg(lastIdStr).arg(threadId);
    sendWsRequest(510, 1, req);
    qDebug() << "[Zalo] fetchPhotoViaWs510: sent WS cmd=510 msgId=" << msgId
             << "lastId=" << lastIdStr << "toid=" << threadId;
}

// Fetch URL HTTP của ảnh khi WS cmd=510 vẫn chỉ trả về blob protobuf.
//
// Zalo không lộ endpoint /api/message/getmsg (trả 404). Thử lần lượt các
// endpoint đã biết, retry khi gặp 404/lỗi API:
//   [0] file service:  file[0]/api/message/getmsg        (biến thể file service)
//   [1] chat service:  chat[0]/api/message/list           (history DM, giống group/history)
//   [2] chat service:  chat[0]/api/message/getmsgv2       (biến thể v2)
//   [3] chat service:  chat[0]/api/message/getconversation
//
// Params: {toid, msgId, imei, count} — mỗi endpoint dùng 1 tập con.
//
static QStringList photoFetchEndpoints(const QString &chatUrl, const QString &fileUrl)
{
    // Dựng base file service: suy ra từ chatUrl nếu fileUrl rỗng
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
    // clientId chính là cái đã gửi làm cliMsgId; giữ lại để onSendMsgDone
    // lưu vào row DB (cần cho deleteMessage/undo về sau).
    reply->setProperty("cliMsgId", msgData["clientId"].toString());
    connect(reply, SIGNAL(finished()), this, SLOT(onSendMsgDone()));
}

// Gửi reply/quote. Cùng shape body như sendMessage() thường nhưng thêm
// field qmsg* và post tới .../quote thay vì .../sms|sendmsg. qmsgAttach
// chỉ gửi cho thread nhóm; với 1-1 thì bỏ hẳn thay vì gửi rỗng.
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
        // Group quote attach: với quote text thường, chỉ đơn giản wrap lại
        // text được quote; quote ảnh chưa được nối dây ở UI (Reply được
        // offer trên hold-menu cho mọi tin nhắn, nhưng preview/attach dưới
        // đây chỉ cover text — khớp với QML hiện tại, chỉ build quote
        // payload từ ListItemData.content).
        QVariantMap attach;
        attach["title"] = quoteContent;
        msgData["qmsgAttach"] = QString::fromUtf8(mapToJson(attach));
        // group không gửi imei (theo zca-js)
    } else {
        msgData["toid"] = threadId;
        msgData["imei"] = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(msgData)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    // Endpoint quote = base + "/quote" thay vì "/sms" (1-1) hay "/sendmsg" (nhóm)
    QString base = isGroup ? m_groupServiceUrl + "/api/group/quote"
                           : m_chatServiceUrl  + "/api/message/quote";

    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest sendReq = buildRequest(urlStr, "https://chat.zalo.me/");
    sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] sendMessageQuote POST" << urlStr << "isGroup:" << isGroup << "qmsgId:" << quoteMsgId;
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

// ─── forwardMessage ─────────────────────────────────────────────────────
// Xem doc comment ở khai báo Q_INVOKABLE trong ZaloService.hpp để biết đầy
// đủ lý do (host file-service, tái dùng nguyên văn content, batch cùng 1 loại).
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
    // reference đánh dấu đây là forward thật (không phải tin mới gõ tình
    // cờ trùng text) — xem doc comment của hàm này trong ZaloService.hpp.
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
        params["imei"]  = m_imei; // chỉ nhánh 1-1 (User-thread) gửi imei, giống sendMessage() ở trên
    }
    params["ttl"]      = 0;
    params["msgType"]  = "1";
    params["totalIds"] = threadIds.size();
    params["msgInfo"]  = QString::fromUtf8(variantToJsonCompact(msgInfo));
    // Gửi chuỗi "null" khi không có reference — tương đương JSON.stringify(null).
    params["decorLog"] = hasReference ? QString::fromUtf8(variantToJsonCompact(decorLog))
                                       : QString("null");

    // variantToJsonCompact() (không phải mapToJson()) vì params.grids/toIds
    // là mảng OBJECT — xem comment của hàm đó về lý do nhánh
    // QVariant::List của mapToJson() không dùng được cho shape này.
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    // Cả 2 nhánh đều gọi host FILE service, không phải m_groupServiceUrl/
    // m_chatServiceUrl như sendMessage() thường.
    QString base = isGroup ? m_fileServiceUrl + "/api/group/mforward"
                           : m_fileServiceUrl + "/api/message/mforward";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] forwardMessage POST" << urlStr << "isGroup:" << isGroup << "targets:" << threadIds.size();
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

    qDebug() << "[Zalo] forwardMessage raw (first200):" << raw.left(200);

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
    qDebug() << "[Zalo] sendMessageQuote response:" << raw.left(200);

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
                qDebug() << "[Zalo] sendMessageQuote: WS already delivered msgId=" << msgId << ", skipping duplicate";
            }
        } else {
            hasError = true;
            qDebug() << "[Zalo] sendMessageQuote error_code:" << outer["error_code"].toInt()
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

// ---- Delete & Recall (ported from zca-js deleteMessage.ts / undo.ts) ---------

void ZaloService::deleteMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                 const QString &cliMsgId, const QString &senderId, bool onlyMe)
{
    if (!m_loggedIn) return;

    // Tin nhắn của CHÍNH MÌNH chỉ có thể xóa cho mọi người qua undo/recall,
    // không qua "delete" — server từ chối tổ hợp đó, nên chặn sớm với lỗi
    // rõ ràng thay vì round-trip lên server mới biết.
    bool isSelf = (senderId == m_uid);
    if (isSelf && !onlyMe) {
        emit messageDeleted(threadId, msgId, false, "To delete your message for everyone, use Recall instead");
        return;
    }
    // "Xóa cho mọi người" chỉ áp dụng nhóm; với chat 1-1 chỉ người gửi mới
    // xóa được cho cả 2 phía (qua Recall).
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

    qDebug() << "[Zalo] deleteMessage POST" << urlStr << "msgId=" << msgId << "onlyMe=" << onlyMe;
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
    qDebug() << "[Zalo] deleteMessage response:" << raw.left(200);

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            // "Xóa cho tôi" phải biến mất hoàn toàn khỏi màn hình của mình,
            // không có placeholder — khác tag "(tin nhắn đã bị thu hồi)" của
            // Recall. markMessageDeletedForMe làm hard DELETE, đúng nghĩa
            // "xóa cho tôi" thay vì chỉ UPDATE tag như Recall.
            markMessageDeletedForMe(tid, msgId);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qDebug() << "[Zalo] deleteMessage error_code:" << outer["error_code"].toInt() << errMsg;
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

    qDebug() << "[Zalo] recallMessage POST" << urlStr << "msgId=" << msgId;
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
    qDebug() << "[Zalo] recallMessage response:" << raw.left(200);

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            // Dùng lại đúng logic update tại chỗ như path chat.undo đến
            // (markMessageRecalled), để bubble tự thu hồi trông giống hệt
            // bubble bị thu hồi qua thông báo WS, và nếu echo WS của chính
            // hành động này có tới thì chỉ là update lại vô hại.
            markMessageRecalled(tid, msgId);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qDebug() << "[Zalo] recallMessage error_code:" << outer["error_code"].toInt() << errMsg;
        }
    } else {
        errMsg = reply->errorString();
    }
    emit messageRecalledDone(tid, msgId, !hasError, errMsg);
}

// ─── Reactions ───────────────────────────────────────────────────────────────
// icon ("like"/"heart"/"haha"/"wow"/"cry"/"angry") -> ký hiệu wire thật
// gửi làm rIcon, và rType (0..5, cùng thứ tự) là chỉ số reaction dạng số —
// xem reactionIconToEmoji()/extractReactionInfo() trong ZaloServiceUtils.hpp.
//
// Bản trước của hàm này gửi msgId/cliMsgId/rType/rIcon dạng flat param ở
// top-level — server chấp nhận (200 OK, không error_code) nhưng không hề
// broadcast cho người kia thấy, đúng triệu chứng "mình react, người kia
// không thấy gì" mà bản này sửa. Server thực ra cần 1 mảng "react_list" mà
// entry của nó là JSON-STRING (không phải object lồng) dưới key "message",
// dạng:
//   { rMsg: [{ gMsgID, cMsgID, msgType }], rIcon, rType, source }
// với gMsgID/cMsgID là SỐ thật (không phải chuỗi số) và field "source" (6
// = reaction bấm từ bubble tin nhắn) mà payload cũ hoàn toàn thiếu.
void ZaloService::reactMessage(const QString &threadId, bool isGroup, const QString &msgId,
                                const QString &cliMsgId, int msgType, int rType, const QString &icon)
{
    if (!m_loggedIn) return;

    // rType giờ được tính TẠI ĐÂY từ icon qua reactionIconToRType(), không
    // lấy từ tham số rType của caller nữa — tham số đó là quy ước slot
    // 0..5 thuần local phía QML (like=0 heart=1 haha=2 wow=3 cry=4 angry=5),
    // chưa từng khớp giá trị reaction-type số thật của Zalo. Log thiết bị
    // thật bắt được 1 lần reaction heart đến cho thấy rType:5, khớp giá trị
    // thật của Zalo — khác hẳn "1" cũ phía QML. Trường hợp gỡ reaction
    // (rType param < 0, tức "-1" từ path remove của msgList.doSendReaction())
    // vẫn đọc từ caller vì đó chỉ là sentinel đơn giản, không phải giá trị
    // số theo từng icon.
    bool removing = (rType < 0);
    int  wireRType = removing ? -1 : reactionIconToRType(icon);
    qint64 clientId = QDateTime::currentMSecsSinceEpoch();

    // gMsgID/cMsgID tự tay build thành text số thô (không qua
    // QVariant::LongLong -> mapToJson(), sẽ cast sang double và âm thầm làm
    // hỏng msgId 64-bit lớn của Zalo, cùng vấn đề quoteBigJsonInts() đã
    // phải xử lý ở chiều nhận). msgId/cliMsgId đến đây dạng QString toàn
    // chữ số, nên nhúng thẳng làm literal số JSON không cần quote được.
    QString rIconOut = removing ? QString() : reactionIconToEmoji(icon);
    QString innerMessage = QString(
        "{\"rMsg\":[{\"gMsgID\":%1,\"cMsgID\":%2,\"msgType\":1}],"
        "\"rIcon\":%3,\"rType\":%4,\"source\":6}")
        .arg(msgId)
        .arg(cliMsgId)
        .arg(jsonQuote(rIconOut))
        .arg(wireRType);

    // Toàn bộ object params ngoài cùng cũng tự tay build, không qua
    // mapToJson() — nhánh QVariant::List của nó chỉ hiểu phần tử scalar
    // (Int/String/Bool/Double), không có case QVariant::Map, nên 1 list
    // chứa map reactListEntry sẽ âm thầm rơi vào elem.toString() trên map
    // (ra rác), không phải JSON thật. Entry duy nhất của react_list cần
    // "message" là JSON-STRING (đã quote qua jsonQuote, vì bản thân
    // innerMessage đã chứa dấu quote lồng) và "clientId" là số trần.
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
        : (isGroup ? m_groupServiceUrl : m_chatServiceUrl); // fallback nếu service map thiếu "reaction"
    QString base = isGroup ? reactionHost + "/api/group/reaction"
                           : reactionHost + "/api/message/reaction";
    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] reactMessage POST" << urlStr << "msgId=" << msgId << "icon=" << icon << "removing=" << removing;
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
    QString icon   = reply->property("icon").toString(); // rỗng nghĩa là call này gỡ reaction
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] reactMessage response:" << raw.left(200);

    QString errMsg;
    if (!hasError) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            if (icon.isEmpty()) dbRemoveReaction(msgId, m_uid);
            else                dbSaveReaction(tid, msgId, m_uid, icon);
            // Cùng lý do "re-apply vô hại nếu echo WS cũng tới" như
            // messageRecalledDone/markMessageRecalled ở trên — QML đã tự
            // apply optimistic rồi, đây chỉ đảm bảo bản DB lưu lại khớp
            // với những gì đang hiện trên màn hình.
            emit reactionUpdated(tid, msgId, m_uid, icon);
        } else {
            hasError = true;
            errMsg = outer["error_message"].toString();
            qDebug() << "[Zalo] reactMessage error_code:" << outer["error_code"].toInt() << errMsg;
        }
    } else {
        errMsg = reply->errorString();
    }
    emit reactMessageDone(tid, msgId, !hasError, errMsg);
}

// ─── Send Photo ──────────────────────────────────────────────────────────────
// 2 bước: 1) upload lên file[0]/api/{message|group}/photo_original/upload
//         2) gửi tin nhắn qua {chat|group}/api/{message|group}/photo
// Copy ảnh từ picker (có thể nằm ở vị trí tạm/sandbox, vd path share-card
// của Camera) vào cache persistent "/tmp/zalo_img_local_<ts>.<ext>". Dùng
// chung prefix "zalo_img_" với file cache của downloadImageMessage() nên
// clearCache() tự nhận qua cacheFilePatterns() — không gì khác xóa nó,
// kể cả đóng/mở lại app.
QString ZaloService::cacheLocalImage(const QString &sourcePath)
{
    QString path = sourcePath;
    if (path.startsWith("file://")) path = path.mid(7);
    if (path.isEmpty() || !QFile::exists(path)) {
        qDebug() << "[Zalo] cacheLocalImage: source missing" << path;
        return sourcePath;
    }

    QString ext = path.section('.', -1).toLower();
    if (ext.isEmpty() || ext.length() > 4) ext = "jpg";
    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QString destPath = "/tmp/zalo_img_local_" + QString::number(ts) + "." + ext;

    if (!QFile::copy(path, destPath)) {
        qDebug() << "[Zalo] cacheLocalImage: copy failed" << path << "->" << destPath
                  << "- falling back to original path";
        return path;
    }
    qDebug() << "[Zalo] cacheLocalImage:" << path << "->" << destPath;
    return destPath;
}

// Xoá 1 file cục bộ — dùng bởi VoiceNoteSheet (Discard/Cancel một bản ghi
// .m4a chưa gửi) và ContactPickerBridge (Cancel một .vcf vừa build xong
// trước khi kịp gửi). Không log lỗi/không emit gì nếu file không tồn tại —
// Discard có thể gọi hàm này ngay cả khi bước ghi/build trước đó thất bại,
// đó là tình huống bình thường chứ không phải lỗi.
void ZaloService::deleteLocalFile(const QString &path)
{
    QString p = path;
    if (p.startsWith("file://")) p = p.mid(7);
    if (p.isEmpty()) return;
    if (QFile::exists(p)) {
        QFile::remove(p);
        qDebug() << "[Zalo] deleteLocalFile:" << p;
    }
}

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

    // clientId cũng chính là param "clientId"/"cliMsgId" gửi cho Zalo bên
    // dưới, được server echo lại nguyên vẹn qua WS cmd=501. Lưu info file
    // local ở đây — trước cả khi call mạng upload bắt đầu — để có sẵn dữ
    // liệu bất kể echo WS đó về nhanh cỡ nào.
    QString clientId = QString::number(ts);
    QVariantMap pendingInfo;
    pendingInfo["localPath"] = "file://" + path;
    pendingInfo["fileSize"]  = (qint64)fileData.size();
    pendingInfo["fileName"]  = filename;
    m_pendingSentPhotoInfo[clientId] = pendingInfo;

    // Params trong query string (đã AES-encrypt)
    // Lưu ý: imei nằm trong multipart body, KHÔNG nằm trong AES params
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
    qDebug() << "[Zalo] sendPhoto upload response:" << raw.left(300);

    if (!ok) { m_pendingSentPhotoInfo.remove(clientId); emit messageSent(false, tid); return; }

    // Upload response: {"error_code":0,"data":"AES_ENCRYPTED"} — same pattern as other APIs
    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo] sendPhoto upload error:" << outer["error_message"].toString();
        m_pendingSentPhotoInfo.remove(clientId);
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
        m_pendingSentPhotoInfo.remove(clientId);
        emit messageSent(false, tid);
        return;
    }

    // Bước 2: gửi tin nhắn ảnh
    // params: photoId, clientId, desc, width, height, toid|grid,
    //         rawUrl=normalUrl, hdUrl, thumbUrl, hdSize=totalSize,
    //         oriUrl (chỉ nhóm), normalUrl (chỉ DM),
    //         zsource=-1, ttl=0, jcp
    QSize photoDim = imageDimensions(localPath);
    QVariantMap mp;
    mp["photoId"]   = photoId;
    // Dùng lại đúng clientId đã sinh trong sendPhoto() (thay vì timestamp
    // mới) để khớp key trong m_pendingSentPhotoInfo và cliMsgId của echo WS cmd=501.
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
    // fileSize/fileName are pulled from the info stashed at send-time (see
    // m_pendingSentPhotoInfo in sendPhoto()) so the filename/filesize row in the
    // photo bubble has real data instead of being derived from the CDN hash URL.
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

    qDebug() << "[Zalo] sendPhoto send-msg POST" << msgUrl.left(100);
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
    qDebug() << "[Zalo] sendPhoto send-msg response:" << raw.left(300);

    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap data = jsonToMap(dec.toUtf8());
            qint64 msgIdInt = data["msgId"].toLongLong();

            if (msgIdInt == 0) {
                // photo_original/send doesn't return a real server msgId — only
                // a locally-fabricated one would be available here, and saving
                // that to the DB creates a SECOND, permanent row once the WS
                // echo (cmd=501) lands a moment later with the real msgId,
                // since dbSaveMessage() keys strictly on msgId. Skip persisting
                // from this HTTP confirm entirely: leave the QML "local_img_"
                // placeholder as-is (still showing "Sending...") and let the
                // WS echo be the single source of truth that both saves the
                // row and replaces the placeholder in place.
                qDebug() << "[Zalo] sendPhoto: no real msgId in send-msg response, "
                            "deferring save to WS echo to avoid a duplicate row";
            } else {
            QString msgId = QString::number(msgIdInt);
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
            // We already resolved localImage via this HTTP confirm path — no need for
            // the WS echo to fall back on a CDN download for this photo anymore.
            if (!clientId.isEmpty()) m_pendingSentPhotoInfo.remove(clientId);
            }
        } else {
            ok = false;
            qDebug() << "[Zalo] sendPhoto send-msg error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
            if (!clientId.isEmpty()) m_pendingSentPhotoInfo.remove(clientId);
        }
    } else if (!clientId.isEmpty()) {
        m_pendingSentPhotoInfo.remove(clientId);
    }
    emit messageSent(ok, tid);
}

// ─── sendFile: gửi file đính kèm bất kỳ định dạng nào (doc/docx, ppt/pptx,
// xls/xlsx, txt, pdf, epub, apk, cer, zip/rar/7z, vcf, mp3/flac, m4a, bar,
// và bất kỳ định dạng nào khác người dùng chọn qua FilePicker) ──────────────
// Dùng chung pipeline chunked-upload với sendVideo() (xem ghi chú ở đó) thay
// vì 1 POST duy nhất như trước — file 30MB đọc hết vào 1 request rất dễ
// timeout hoặc bị server từ chối giữa chừng không rõ lý do, còn 512K/chunk
// vừa an toàn hơn vừa cho progress % thật để hiển thị lên bubble. isFile=true
// trong context để sendVideoChunk()/handleFileUploadDone() biết báo progress
// qua fileUploadProgress thay vì videoUploadProgress.
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
    ctx["fileData"]    = fileData; // kept in memory until the last chunk is sent
    ctx["isFile"]      = true;
    m_pendingVideoChunkUpload[clientId] = ctx;

    qDebug() << "[Zalo] sendFile: splitting into" << totalChunks
             << "chunk(s), size:" << fileData.size() << "md5:" << md5Checksum;
    emit fileUploadProgress(threadId, 0);
    sendVideoChunk(clientId);
}

// ─── Send Video (.mp4) ────────────────────────────────────────────────────
// 2 bước, khác sendPhoto() ở bước 1: upload video KHÔNG trả URL ngay trong
// response HTTP — chỉ trả fileId. URL thật (fileUrl) đến sau, async, qua WS
// cmd=601 với control.act_type=="file_done" khớp theo fileId. Bước 2 (gửi
// tin nhắn thật) chỉ gọi được sau khi có fileUrl đó — xem
// ZaloService_WebSocket.cpp::handleFileUploadDone().
void ZaloService::sendVideo(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Zalo] sendVideo: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString filename = path.section('/', -1);
    qint64  ts        = QDateTime::currentMSecsSinceEpoch();
    QString clientId  = QString::number(ts);
    // FIX: checksum/checksumSha were always sent as empty strings in the
    // final asyncfile/msg call. The server ack'd that call with
    // error_code=0 regardless, but a follow-up cmd=510 history poll showed
    // it never actually created the message — an empty checksum on a
    // required-looking field is a plausible reason the server accepts the
    // request shape but silently drops it. Computing a real MD5 costs one
    // pass over data already in memory, so there's no reason not to send one.
    QString md5Checksum = QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toHex();

    // FIX: server rejects any single chunk over 512K with error_code 201
    // ("Dung lượng chunk upload không được vượt quá 512K"). The old code
    // always sent totalChunk=1 with the WHOLE file in one POST — silently
    // failed for anything bigger than 512K (i.e. basically every real
    // video), which is why sendVideo appeared to hang forever on
    // "Sending..." with no feedback. Split into <=512K chunks uploaded
    // sequentially via sendVideoChunk(); each chunk's success also gives us
    // a natural progress percentage to report back to the bubble.
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
    ctx["fileData"]    = fileData; // kept in memory until the last chunk is sent
    ctx["isFile"]      = false;
    m_pendingVideoChunkUpload[clientId] = ctx;

    qDebug() << "[Zalo] sendVideo: splitting into" << totalChunks
             << "chunk(s), size:" << fileData.size() << "md5:" << md5Checksum;
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

    qDebug() << "[Zalo] sendVideo upload chunk" << chunkIndex << "/" << totalChunks
             << "chunkSize:" << chunkData.size();
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
    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "upload chunk response:" << raw.left(300);

    if (!ok) {
        qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "upload chunk: network error" << reply->errorString();
        m_pendingVideoChunkUpload.remove(clientId);
        emit messageSent(false, threadId);
        return;
    }

    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "upload chunk error:" << outer["error_message"].toString();
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

    // Last chunk done — server's response to the FINAL chunk carries the
    // real upload result (fileId), same shape as the old single-shot reply.
    QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());
    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "upload decrypted:" << decStr.left(200);

    QVariantMap decOuter = jsonToMap(decStr.toUtf8());
    QVariantMap uploadData;
    QVariant dataVariant = decOuter["data"];
    if (dataVariant.type() == QVariant::Map) uploadData = dataVariant.toMap();
    else uploadData = jsonToMap(dataVariant.toString().toUtf8());

    QString fileId = uploadData["fileId"].toString();
    if (fileId.isEmpty()) fileId = uploadData["photoId"].toString(); // Zalo dùng chung field ở vài phiên bản API
    if (fileId.isEmpty() || fileId == "-1") {
        qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << ": upload OK but no valid fileId, raw:" << raw.left(200);
        m_pendingVideoChunkUpload.remove(clientId);
        emit messageSent(false, threadId);
        return;
    }

    // Lưu info, chờ WS cmd=601 act_type="file_done" khớp fileId để lấy
    // fileUrl thật rồi mới gửi bước 2 — xem handleFileUploadDone().
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

    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << ": upload done, waiting WS file_done for fileId=" << fileId;
}

// Gọi từ ZaloService_WebSocket.cpp khi WS cmd=601 báo act_type="file_done"
// với fileId khớp 1 video/file đang chờ trong m_pendingVideoUpload. fileUrl
// là URL CDN thật vừa server trả — bước 2: gửi tin nhắn qua asyncfile/msg.
// Type-agnostic — hoạt động giống hệt cho cả video lẫn file tài liệu, chỉ
// khác ở "extention"/mime do server tự suy ra từ fileName.
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
    // FIX: was always "" — see the comment in sendVideo() where this is
    // computed. Left checksumSha empty since we only have MD5 on hand and
    // sending a wrong SHA is worse than sending none.
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

    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "send-msg POST" << msgUrl.left(100);
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
    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "send-msg response:" << raw.left(300);

    if (!ok) { emit messageSent(false, tid); return; }

    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "send-msg error_code:" << outer["error_code"].toInt()
                 << outer["error_message"].toString();
        emit messageSent(false, tid);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
    QVariantMap data = jsonToMap(dec.toUtf8());
    // FIX: was only logging raw.left(300) — that's the OUTER encrypted
    // envelope, always just {"error_code":0,"error_message":"Successful.",
    // "data":"<ciphertext>"} regardless of what actually happened. Log the
    // DECRYPTED inner payload too so a future report of "sent but recipient
    // never got it" has real diagnostic data instead of just msgId==0.
    qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << "send-msg decrypted data:" << dec.left(300);
    qint64 msgIdInt = data["msgId"].toLongLong();

    if (msgIdInt == 0) {
        // FIX: this used to fabricate a local "sent_"-prefixed message and
        // report messageSent(true, tid) unconditionally here, on the theory
        // that the WS self-echo carrying the real msgId just doesn't
        // reliably arrive for video/file sends. That theory was wrong (or
        // at least incomplete): a subsequent cmd=510 history poll on this
        // same thread, run repeatedly for over a minute after this branch
        // fired, kept returning zero messages — meaning the server never
        // actually created the message at all despite this HTTP call
        // reporting error_code=0/"Successful.". Claiming success here was
        // actively misleading (the sender's own app showed "sent" while the
        // recipient's real Zalo app never received anything). Until the
        // real cause of msgId==0 is understood (possibly a missing/wrong
        // field in the asyncfile/msg params below — checksum/checksumSha/
        // fdata are currently placeholders), treat this as a failure so the
        // "local_video_.../local_file_..." placeholder gets cleaned up (see
        // onMessageSent in ChatView.qml) and the person can see it failed
        // and retry, rather than being falsely told it went through.
        qDebug() << "[Zalo]" << (isFile ? "sendFile" : "sendVideo") << ": no real msgId in send-msg response — "
                    "server likely did not create the message (see decrypted "
                    "data above); reporting failure instead of a false success";
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

    // Xử lý ảnh base64 inline (previewThumb từ tin ảnh real-time qua Zalo
    // WS). Loại này bắt đầu bằng data base64, không phải "http".
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
                // Kiểm tra định dạng thumbnail riêng của Zalo:
                // [03][00][W][00][H][FF DA ...data JPEG SOS...]
                // Data SOS cần header JPEG chuẩn chèn phía trước mới decode được.
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
                    // QUAN TRỌNG: build từng byte với cast (char) rõ ràng.
                    // KHÔNG dùng string literal kiểu "\x01\x22\x00" vì
                    // QByteArray::operator+= với const char* dừng ở byte
                    // \x00 đầu tiên, âm thầm làm rớt byte qtable index của
                    // component Y và làm hỏng mọi marker phía sau.
                    QByteArray sof0Data;
                    sof0Data += (char)8;                    // sample precision (bits)
                    sof0Data += (char)((H >> 8) & 0xFF);   // height high byte
                    sof0Data += (char)(H & 0xFF);           // height low byte
                    sof0Data += (char)((W >> 8) & 0xFF);   // width high byte
                    sof0Data += (char)(W & 0xFF);           // width low byte
                    sof0Data += (char)3;                    // num components
                    sof0Data += (char)0x01;                 // Y  component id
                    sof0Data += (char)0x22;                 // Y  sampling: 2x2
                    sof0Data += (char)0x00;                 // Y  qtable index: 0
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

                    // Marker APP0 JFIF — cần thiết để QImageReader của
                    // Qt4 trên BB10 nhận diện đúng. Format: marker(FF E0)
                    // + length(00 10) + "JFIF\0" + version(1.1)
                    //   + density_units(0=không đơn vị) + Xdensity(0,1) + Ydensity(0,1)
                    //   + thumbnail_size(0,0)
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

                    // Dùng msgId làm filename — path riêng cho từng tin
                    // nhắn tránh cache ảnh cũ của BB10. Luôn lưu dạng .png
                    // — ImageView của BB10 ổn định hơn với PNG so với JPEG.
                    // LƯU Ý: hardcode "/tmp/" (không phải QDir::tempPath())
                    // — trên máy BB10 này QDir::tempPath() bị xóa mỗi lần
                    // app restart, còn "/tmp/" thường là chỗ persistent
                    // avatar cũng dùng.
                    QString tmpPath = "/tmp/msgthumb_" +
                                      msgId + ".png";
                    QFile::remove(tmpPath);

                    // Byte-stuff scan data: trong JPEG, byte 0xFF nào trong
                    // entropy-coded data cũng PHẢI có 0x00 theo sau, không
                    // decoder sẽ nhầm thành marker. Data SOS thô của Zalo
                    // có byte 0xFF trần gây lỗi "Bogus marker length" trong
                    // libjpeg của BB10. Escape chúng ở đây.
                    QByteArray stuffedSos;
                    // Header SOS: FF DA + length(2 byte) + payload[length-2]
                    // Giữ header nguyên văn, chỉ stuff phần entropy data sau đó.
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
                            // Chỉ chèn stuffing 0x00 nếu byte kế tiếp chưa
                            // phải là zero đã stuff, restart marker (D0-D7),
                            // hay EOI (D9).
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
                        // Scale thumbnail nhỏ lên cho dễ thấy trong bubble
                        // chat. Ảnh gốc 24×24 — quá nhỏ để làm placeholder ảnh chat.
                        if (qimg.width() < 120 || qimg.height() < 120) {
                            qimg = qimg.scaled(240, 240,
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
                        }
                        qimg.save(tmpPath, "PNG");
                        qDebug() << "[Zalo] decoded Zalo thumb" << W << "x" << H
                                 << "→ PNG msgId=" << msgId << tmpPath;
                    } else {
                        // Decode thất bại hoàn toàn — ghi tạm PNG xanh
                        // 240×240 để bubble hiện gì đó trong lúc chờ fetch
                        // full-res (cmd=510).
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

            // Hardcode "/tmp/" — cùng lý do như msgthumb_ ở trên:
            // QDir::tempPath() không sống qua restart app trên máy này, "/tmp/" thì có.
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

    // Check persistent: msgId này có thể đã có ảnh tải sẵn từ phiên trước
    // (logout/login, app restart...) — dbLoadMessages() đã trả localImage
    // để hiển thị, nhưng downloadImageMessage() cũng có thể được gọi trực
    // tiếp (vd re-sync, search jump-to-message) mà không qua path đó
    // trước. Nếu file vẫn còn trên đĩa thì dùng lại thay vì tải lại qua mạng.
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

    // ImageView của BB10 không hỗ trợ WebP. Transcode mọi ảnh WebP (hoặc
    // không nhận diện được) sang PNG qua QImage để luôn hiển thị đúng.
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

    // Filename cố định theo từng tin nhắn (theo msgId, KHÔNG theo md5(url)):
    // URL Zalo trả về cho cùng 1 ảnh có thể đổi giữa các lần fetch (URL CDN
    // có ký, query param...) dù ảnh gốc không đổi. Đặt key theo msgId nghĩa
    // là fetch lại cùng tin nhắn luôn ghi đè đúng file cũ thay vì để lại
    // file mồ côi, và giúp check QFile::exists() ở downloadImageMessage()
    // nhận ra đúng "đã có ảnh này rồi" ở lần gọi sau.
    //
    // Hardcode "/tmp/" (không phải QDir::tempPath()): trên máy BB10 này,
    // QDir::tempPath() bị xóa mỗi lần app restart, còn "/tmp/" là chỗ
    // persistent avatar cũng dùng thành công. Dùng chung root persistent
    // này là lý do ảnh chat tải về thực sự sống qua được logout/login và restart.
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
        // Dùng chung backoff tăng dần với onWsDisconnected/upgrade-failed
        // — trước đây hardcode 2000ms ở đây, ngắn hơn backoff nơi khác nên
        // vô tình phá backoff, khiến WS vẫn bị retry dồn dập mỗi ~2-8s.
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
    // User đang xem thread này ngay bây giờ -> tắt badge unread trên item
    // tương ứng trong tab Zalo10 của Hub (không xoá item, chỉ đánh dấu đọc).
    m_hub->markThreadRead(threadId);
    qDebug() << "[Zalo] setActiveThread:" << threadId << "isGroup:" << isGroup << "changed:" << changed;
}

void ZaloService::sendHubNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup)
{
    bb::platform::Notification *notif = new bb::platform::Notification(this);
    notif->setTitle(title);
    notif->setBody(body);
    // iconUrl là icon hiện trên instant preview (lock screen). Theo header
    // <bb/platform/Notification>, instant preview mặc định TẮT trừ khi app
    // có account đăng ký trong Hub — m_hub->upsertThreadItem() bên dưới lo
    // phần đó; setIconUrl() ở đây chỉ định icon dùng cho preview đó.
    //
    // Doc của iconUrl yêu cầu "file URI to a public asset" (không phải
    // "asset:///" — scheme đó chỉ dùng trong QML/Cascades resource loading,
    // không phải file URI thật) — dùng chung công thức path với
    // HubIntegration (HubIntegration::hubIconUrl(), dựa trên __progname),
    // để không lặp lại logic đường dẫn ở 2 nơi và luôn đồng bộ nếu công
    // thức đổi.
    notif->setIconUrl(HubIntegration::hubIconUrl());

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

    // Đẩy dòng hội thoại vào tab "Zalo10" riêng trong Hub (kiểu TBBX), tách
    // khỏi mục Notifications chung. Xem HubIntegration.hpp để biết chi tiết.
    // isGroup dùng title đã có sẵn (tên nhóm) làm tên item; DM thì title đã
    // là "Zalo10" theo call site hiện tại (ZaloService_WebSocket.cpp), nên
    // dùng thẳng title + body không cần biến đổi thêm ở đây.
    m_hub->upsertThreadItem(threadId, isGroup, title, body, QDateTime::currentMSecsSinceEpoch());
}

// Banner đầu màn hình qua bb::platform::NotificationDialog.
//
// Tách riêng khỏi sendHubNotification() vì 2 hàm giải quyết vấn đề khác
// nhau: banner/peek của bb::platform::Notification chỉ hiện khi Zalo10
// KHÔNG phải cửa sổ foreground đang active, nên chỉ dùng nó thôi thì tin
// nhắn đến lúc đang mở sẵn app sẽ không có banner nào cả. NotificationDialog
// không bị giới hạn đó: gọi show() hiện ngay lập tức bất kể app/thread nào
// đang mở — gần với hành vi "banner khi đang dùng app" mong muốn hơn — dù
// đây là dialog thật cần người dùng tự đóng, không phải peek tự trượt biến
// mất; API notification của BB10 không có sẵn kiểu "toast" tự hết giờ,
// đây là cái gần nhất tồn tại thật.
//
// Không gắn nút/InvokeRequest có chủ đích: tap vào banner này chỉ nên đưa
// process Zalo10 đang chạy lên foreground, không nhảy tới thread cụ thể
// như tap Hub notification làm — nên không có gì để nút invoke. Tham số
// threadId/isGroup nhận vào chỉ để đối xứng với sendHubNotification() (cùng
// call site gọi cả 2 với cùng argument) chứ dialog đơn giản này không dùng tới.
void ZaloService::sendBannerNotification(const QString &title, const QString &body, const QString &threadId, bool isGroup)
{
    Q_UNUSED(threadId);
    Q_UNUSED(isGroup);

    bb::platform::NotificationDialog *dlg = new bb::platform::NotificationDialog(this);
    dlg->setTitle(title);
    dlg->setBody(body);

    // Không bị leak: parent là `this`/ZaloService ở trên, và deleteLater()
    // khi finished() dọn dẹp sau khi người dùng đóng dialog — cùng pattern
    // lifetime codebase này đã dùng cho các handler QNetworkReply one-shot khác.
    connect(dlg, SIGNAL(finished(bb::platform::NotificationResult::Type)),
            dlg, SLOT(deleteLater()));

    dlg->show();
    qDebug() << "[Zalo] Banner notification shown:" << title << body.left(40);
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

