#include "core/messages/MessagesCommon.hpp"

void ZaloService::fetchMessages(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn) return;

    if (isGroup) {
        // The HTTP /api/group/history endpoint was removed by Zalo; group history comes over WS
        // cmd=511 (zca-js requestOldMessages). The reply is handled by handleWsGroupHistoryReply().
        m_pending511Gid = threadId;
        // Nothing stored for this group yet, so there is no local message id to reconcile the WS history
        // against: load the newest page from the cloud message service instead.
        if (dbOldestMsgId(threadId).isEmpty() && m_cloudCursorId.value(threadId).isEmpty())
            fetchGroupCloudOlder(threadId);
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

// ---------------------------------------------------------------------------------------------
// Group history from Zalo's cloud message service (zca-js getCloudMessage, PR #397). Zalo removed
// the old /api/group/history endpoint, so this is the way to page back past what WS cmd=511
// returns. One call = one page (up to 50 messages), walking back with lastMsgId / isOld.
// ---------------------------------------------------------------------------------------------
namespace {
bool cloudMsgTsLess(const QVariant &a, const QVariant &b)
{
    return a.toMap().value("ts").toString().toLongLong() < b.toMap().value("ts").toString().toLongLong();
}
}

QString ZaloService::dbOldestMsgId(const QString &threadId) const
{
    if (!m_db || threadId.isEmpty()) return QString();
    const char *sql =
        "SELECT msgId FROM messages WHERE threadId=? AND msgId!='' AND msgId NOT LIKE 'local_%' "
        "ORDER BY CAST(ts AS INTEGER) ASC LIMIT 1;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return QString();
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    QString id;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        id = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return id;
}

void ZaloService::fetchGroupCloudOlder(const QString &groupId)
{
    if (groupId.isEmpty()) return;
    if (!m_loggedIn || m_groupCloudMsgServiceUrl.isEmpty()) {
        qDebug() << "[Cloud] no zpwServiceMap.group_cloud_message url (re-login to refresh it)";
        emit groupCloudOlderDone(groupId, 0, false);
        return;
    }

    // Cursor: the previous cloud page of this session, else the oldest message we already have.
    // No local message at all = first load, which asks for the newest page (globalMsgId 0, src 1).
    QString cursor = m_cloudCursorId.value(groupId);
    bool isOld = m_cloudCursorIsOld.value(groupId, false);
    if (cursor.isEmpty()) {
        cursor = dbOldestMsgId(groupId);
        isOld = false;
    }
    const bool fromLatest = cursor.isEmpty();
    if (fromLatest) cursor = "0";

    QString gid = groupId;
    if (gid.startsWith("g")) gid.remove(0, 1);

    QVariantMap innerParams;
    innerParams["groupId"]     = gid;
    innerParams["globalMsgId"] = cursor;
    innerParams["count"]       = 50;
    innerParams["msgIds"]      = QVariantList();
    innerParams["imei"]        = m_imei;
    innerParams["src"]         = fromLatest ? 1 : 3;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_groupCloudMsgServiceUrl + (isOld ? "/api/cm/getoldv2" : "/api/cm/getrecentv2")
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams)
                   + "&nretry=0";

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("groupId", groupId);
    reply->setProperty("cursor", cursor);
    reply->setProperty("fromLatest", fromLatest);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupCloudMsgDone()));
}

void ZaloService::onGroupCloudMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString groupId    = reply->property("groupId").toString();
    const QString cursor     = reply->property("cursor").toString();
    const bool    fromLatest = reply->property("fromLatest").toBool();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // Every outcome ends in groupCloudOlderDone so the chat view never waits forever.
    QVariantMap root = jsonToMap(raw);
    if (raw.isEmpty() || root["error_code"].toInt() != 0) {
        // -69 "Stop retry" = nothing more to load.
        qDebug() << "[Cloud] page failed: error_code=" << root["error_code"].toInt();
        emit groupCloudOlderDone(groupId, 0, false);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());

    // zca-js: result.data may be an object OR a JSON string that still has to be parsed.
    QVariant dv = outer["data"];
    QVariantMap d;
    if (dv.type() == QVariant::Map)         d = dv.toMap();
    else if (dv.type() == QVariant::String) d = jsonToMap(dv.toString().toUtf8());
    if (d.isEmpty()) d = outer;
    qDebug() << "[Cloud] reply: inner error_code=" << outer["error_code"].toInt()
             << "msg=" << outer["error_message"].toString()
             << "dataType=" << dv.typeName() << "keys=" << d.keys();
    if (outer["error_code"].toInt() != 0) {
        emit groupCloudOlderDone(groupId, 0, false);
        return;
    }

    // groupMsgs is normally an array, but accept a JSON string too (the page itself came as one).
    QVariant gmv = d["groupMsgs"];
    QVariantList rawMsgs;
    if (gmv.type() == QVariant::String) rawMsgs = jsonToList(gmv.toString().toUtf8());
    else                                rawMsgs = gmv.toList();
    // Ids and flags only, no message content.
    qDebug() << "[Cloud] fields: groupMsgsType=" << gmv.typeName() << "n=" << rawMsgs.size()
             << "error=" << d["error"].toString() << "hasMore=" << d["hasMore"].toString()
             << "isOld=" << d["isOld"].toString() << "isFiltered=" << d["isFiltered"].toString()
             << "byPhase=" << d["isFilteredByPhase"].toString()
             << "byTimeJoin=" << d["isFilteredByTimeJoin"].toString()
             << "tsJoin=" << d["tsJoinGroup"].toString()
             << "last=" << d["lastMsgId"].toString() << "min=" << d["minMsgId"].toString()
             << "max=" << d["maxMsgId"].toString() << "cursor=" << cursor;
    QString lastId  = d["lastMsgId"].toString();
    bool hasMore    = (d["hasMore"].toInt() == 1);
    bool nextIsOld  = (d["isOld"].toInt() == 1);
    if (rawMsgs.isEmpty() && d["isFiltered"].toInt() == 1) hasMore = false;
    if (lastId.isEmpty() || lastId == cursor) hasMore = false;
    qDebug() << "[Cloud] page: msgs=" << rawMsgs.size() << "hasMore=" << hasMore << "isOld=" << nextIsOld;

    QVariantList msgs = rawMsgs.isEmpty() ? QVariantList() : saveGroupHistory(rawMsgs, groupId, true);

    if (hasMore) {
        m_cloudCursorId[groupId]    = lastId;
        m_cloudCursorIsOld[groupId] = nextIsOld;
    }

    // First load of a group with no stored messages: show the page straight away (oldest first).
    if (fromLatest && !msgs.isEmpty()) {
        qSort(msgs.begin(), msgs.end(), cloudMsgTsLess);
        emit messagesReady(groupId, msgs);
    }
    emit groupCloudOlderDone(groupId, msgs.size(), hasMore);
}
