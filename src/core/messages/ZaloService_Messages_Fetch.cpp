#include "core/messages/MessagesCommon.hpp"

void ZaloService::fetchMessages(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn) return;

    if (isGroup) {
        // The HTTP /api/group/history endpoint was removed by Zalo; group history comes over WS
        // cmd=511 (zca-js requestOldMessages). The reply is handled by handleWsGroupHistoryReply().
        m_pending511Gid = threadId;
        if (!m_wsConnected || !m_webSocket) {
            if (!m_zpwWsUrls.isEmpty()) connectWebSocket();
            return;
        }
        QString lastId = m_threadLastMsgId.value(threadId, "0");
        QString req511 = QString("{\"first\":true,\"lastId\":\"%1\",\"grid\":\"%2\",\"preIds\":[]}")
                         .arg(lastId).arg(threadId);
        sendWsRequest(511, 1, req511);
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

// Parses, saves and returns one group's history messages. syncGroup=true is the bulk/background
// variant: no sticker downloads, no active-thread poll bookkeeping.
QVariantList ZaloService::saveGroupHistory(const QVariantList &rawMsgs, const QString &tid, bool syncGroup)
{
    QVariantList msgs;
    const bool isGroup = true;

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
        if (uidFrom == "0") uidFrom = m_uid;   // our own message echoed from another device
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
    return msgs;
}
