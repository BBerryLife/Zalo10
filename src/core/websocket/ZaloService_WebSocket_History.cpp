#include "core/websocket/WebSocketCommon.hpp"

void ZaloService::processOldDmBatch(const QVariantList &rawMsgs, const QString &emitThread, bool bulk)
{
        QVariantList msgs;
        qint64 maxMsgNum = -1;
        QString newestMsgId;

        for (int i = 0; i < rawMsgs.size(); ++i) {
            QVariantMap m = rawMsgs[i].toMap();
            QString msgId      = m["msgId"].toString();
            QString rawUidFrom = m["uidFrom"].toString();

            {
                QString vFrom = rawUidFrom;
                QString vTo   = m["idTo"].toString();
                if (vFrom == "0") vFrom = m_uid;
                if (vTo   == "0") vTo   = m_uid;
                bool belongsHere = (vFrom == emitThread || vTo == emitThread
                                    || (vFrom == m_uid && vTo == emitThread)
                                    || (vTo == m_uid && vFrom == emitThread));
                if (!belongsHere) {
                    continue;
                }
            }
            bool isMine = (rawUidFrom == "0" || rawUidFrom == m_uid);
            QString uidFrom = (rawUidFrom == "0") ? m_uid : rawUidFrom;

            if (isMessageDeletedForMe(msgId)) {
                continue;
            }

            if (isGroupPollNotice(m)) continue;

            // chat.undo is a recall notice, not a message: patch the original if it is earlier in the same
            // history batch, always save the recall to SQLite, and don't add it as a bubble.
            QString recalledIdH = extractRecalledMsgId(m);
            if (!recalledIdH.isEmpty()) {
                markMessageRecalled(emitThread, recalledIdH);
                for (int pj = 0; pj < msgs.size(); ++pj) {
                    QVariantMap pm = msgs[pj].toMap();
                    if (pm["msgId"].toString() == recalledIdH) {
                        pm["content"] = QString();
                        pm["msgType"] = 99;
                        msgs[pj] = pm;
                        break;
                    }
                }
                continue;
            }

            QString delMsgIdH, deleterUidH;
            if (extractDeleteInfo(m, delMsgIdH, deleterUidH)) {
                if (deleterUidH == m_uid) {
                    markMessageDeletedForMe(emitThread, delMsgIdH);
                    for (int pj = 0; pj < msgs.size(); ++pj) {
                        if (msgs[pj].toMap()["msgId"].toString() == delMsgIdH) {
                            msgs.removeAt(pj);
                            break;
                        }
                    }
                }
                continue;
            }

            QString reactMsgIdH, reactUidH, reactIconH;
            if (extractReactionInfo(m, reactMsgIdH, reactUidH, reactIconH)) {
                if (reactIconH.isEmpty()) dbRemoveReaction(reactMsgIdH, reactUidH);
                else                      dbSaveReaction(emitThread, reactMsgIdH, reactUidH, reactIconH);
                emit reactionUpdated(emitThread, reactMsgIdH, reactUidH, reactIconH);
                for (int pj = 0; pj < msgs.size(); ++pj) {
                    if (msgs[pj].toMap()["msgId"].toString() == reactMsgIdH) {
                        msgs.removeAt(pj);
                        break;
                    }
                }
                continue;
            }

            QVariantMap out;
            out["msgId"]    = msgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = false;
            out["isMine"]   = isMine;
            out["msgType"]  = m["msgType"].toInt();

            int mtH = m["msgType"].toInt();
            if (mtH == 0) {
                mtH = m["type"].toInt();
                if (mtH == 0) mtH = m["mt"].toInt();
            }
            if (mtH == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mtH = 2;
                // The history path must map share.file -> mt=3 like the realtime path; otherwise a re-polled
                // video (periodic cmd=510 poll) fell into the nested-content branch (mt=2) and permanently
                // overwrote the correct DB record.
                else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                    mtH = 3;
                else if (mtStr.contains("chat.recommended"))
                {
                    QVariantMap peekMapH = m["content"].toMap();
                    if (peekMapH.isEmpty()) {
                        QString peekStrH = m["content"].toString();
                        if (!peekStrH.isEmpty() && peekStrH.trimmed().startsWith("{"))
                            peekMapH = jsonToMap(peekStrH.toUtf8());
                    }
                    QString peekActionH = peekMapH["action"].toString();
                    mtH = peekActionH.contains("link") ? 6 : 4;
                }
                else if (mtStr.contains("chat.sticker"))
                    mtH = 5;
            }

            QString rawContentH = m["content"].toString();
            if (rawContentH.isEmpty()) {
                QVariantMap cm = m["content"].toMap();
                if (!cm.isEmpty()) {
                    rawContentH = "{";
                    QStringList cmKeys = cm.keys();
                    for (int k = 0; k < cmKeys.size(); ++k) {
                        if (k > 0) rawContentH += ",";
                        QString v = cm[cmKeys[k]].toString();
                        v.replace("\\", "\\\\").replace("\"", "\\\"");
                        rawContentH += "\"" + cmKeys[k] + "\":\"" + v + "\"";
                    }
                    rawContentH += "}";
                    if (mtH == 0) mtH = 2;
                }
            }
            if (mtH == 5) {
                QVariantMap smH = m["content"].toMap();
                if (smH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    smH = jsonToMap(rawContentH.toUtf8());
                qint64 stickerIdH = smH["id"].toString().toLongLong();
                if (stickerIdH == 0) stickerIdH = smH["id"].toLongLong();
                rawContentH = QString("{\"stickerId\":%1}").arg(stickerIdH);
                out["msgType"] = 5;
                if (stickerIdH > 0) downloadSticker(QString::number(stickerIdH));
            }

            if (mtH == 6) {
                QVariantMap lmH = m["content"].toMap();
                if (lmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    lmH = jsonToMap(rawContentH.toUtf8());
                QString lTitleH = lmH["title"].toString();
                QString lDescH  = lmH["description"].toString();
                QString lHrefH  = lmH["href"].toString();
                QString lThumbH = lmH["thumb"].toString();
                QString escH = lTitleH; escH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString descEscH = lDescH; descEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString hrefEscH = lHrefH; hrefEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString thumbEscH = lThumbH; thumbEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                rawContentH = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                                  .arg(escH).arg(descEscH).arg(hrefEscH).arg(thumbEscH);
                out["msgType"] = 6;
            }

            if (mtH == 4) {
                QVariantMap rmH = m["content"].toMap();
                if (rmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    rmH = jsonToMap(rawContentH.toUtf8());
                QString actionH = rmH["action"].toString();
                QVariant paramsVH2 = rmH["params"];
                QVariantMap paramsMapH2 = (paramsVH2.type() == QVariant::String)
                    ? jsonToMap(paramsVH2.toString().toUtf8())
                    : paramsVH2.toMap();
                QString callResultH = actionH.contains("misscall") ? "missed" : "ended";
                QString callKindH   = (paramsMapH2["calltype"].toInt() == 1) ? "video" : "voice";
                qint64  durationH   = paramsMapH2["duration"].toString().toLongLong();
                rawContentH = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}")
                                  .arg(callResultH).arg(callKindH).arg(durationH);
                out["msgType"] = 4;
            }

            if (mtH == 3) {
                QVariantMap fmH = m["content"].toMap();
                if (fmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    fmH = jsonToMap(rawContentH.toUtf8());
                QString fTitleH = fmH["title"].toString();
                QString fHrefH  = fmH["href"].toString();
                qint64  fSizeH  = 0;
                QVariant paramsVH = fmH["params"];
                QVariantMap paramsMapH = (paramsVH.type() == QVariant::String)
                    ? jsonToMap(paramsVH.toString().toUtf8())
                    : paramsVH.toMap();
                if (!paramsMapH.isEmpty())
                    fSizeH = paramsMapH["fileSize"].toString().toLongLong();
                if (!fHrefH.isEmpty()) {
                    QString fTitleEscH = fTitleH;
                    fTitleEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                    rawContentH = QString("{\"fileName\":\"%1\",\"href\":\"%2\"")
                                      .arg(fTitleEscH).arg(fHrefH);
                    if (fSizeH > 0) rawContentH += QString(",\"fileSize\":%1").arg(fSizeH);
                    rawContentH += "}";
                }
            }
            if (mtH == 2 || rawContentH.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContentH);
                if (normalized != rawContentH && !normalized.isEmpty()) {
                    rawContentH = normalized;
                    mtH = 2;
                    out["msgType"] = 2;
                }
            }

            if (!bulk && (mtH == 2 || out["msgType"].toInt() == 2) && !msgId.isEmpty()) {
                QString checkUrl;
                if (!rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContentH.toUtf8());
                    checkUrl = cm["normalUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["hdUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["thumbUrl"].toString();
                }
                if (!checkUrl.isEmpty() && !checkUrl.startsWith("http")) {
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        downloadImageMessage(msgId, checkUrl, emitThread);
                        fetchPhotoViaWs510(msgId, emitThread);
                        fetchPhotoViaHttp(msgId, emitThread);
                    }
                }
            }
            // Same as the realtime path: keep out["msgType"] in sync with the resolved mtH.
            out["msgType"] = mtH;
            out["content"] = rawContentH;
            msgs.append(out);

            if (!msgId.isEmpty()) {
                qint64 num = msgId.toLongLong();
                if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
                if (isMine) m_seenMsgIds.insert(msgId);
            }
        }

        // Bulk sync walks the whole history, so only ever move the incremental-poll cursor forward.
        if (!newestMsgId.isEmpty()
            && (!bulk || newestMsgId.toLongLong() > m_lastPollMsgId.toLongLong()))
            m_lastPollMsgId = newestMsgId;

        QList<QVariantMap> photoUpdates;
        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap mm = msgs[i].toMap();
            QString mid = mm["msgId"].toString();
            if (m_pendingPhotoMsgIds.contains(mid)) {
                QString threadForPhoto = m_pendingPhotoMsgIds.take(mid);
                QString content = mm["content"].toString();
                QString photoUrl;
                if (!content.isEmpty() && content.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(content.toUtf8());
                    photoUrl = cm["normalUrl"].toString();
                    if (photoUrl.isEmpty()) photoUrl = cm["hdUrl"].toString();
                    if (photoUrl.isEmpty()) photoUrl = cm["thumbUrl"].toString();
                }
                if (!photoUrl.isEmpty() && photoUrl.startsWith("http")) {
                    if (m_db) {
                        const char *sql = "UPDATE messages SET content=?, msgType=2 WHERE msgId=?";
                        sqlite3_stmt *stmt = 0;
                        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, mid.toUtf8().constData(),     -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt);
                            sqlite3_finalize(stmt);
                        }
                    }
                    downloadImageMessage(mid, photoUrl, threadForPhoto);
                } else {
                    qWarning() << "[Msg] photo fetch: no HTTP URL, msgId=" << mid;
                }
            }
        }

        for (int i = 0; i < msgs.size(); ++i)
            dbSaveMessage(msgs[i].toMap(), emitThread);
        if (!newestMsgId.isEmpty() && !bulk)
            m_threadLastMsgId[emitThread] = newestMsgId;

        if (!bulk || emitThread == m_activeThreadId)
            emit messagesReady(emitThread, msgs);
}
