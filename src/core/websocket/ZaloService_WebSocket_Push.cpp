#include "core/websocket/WebSocketCommon.hpp"

void ZaloService::handleWsChatPush(qint32 cmd, const QByteArray &data)
{
    bool isGroup = (cmd == 521);

    QVariantMap outer = jsonToMap(data);
    QVariantMap d = decodeWsEnvelope(outer, "cmd501");

    QVariantList msgs;
    if (isGroup) {
        msgs = d["groupMsgs"].toList();
        if (msgs.isEmpty()) msgs = d["msgs"].toList();
        if (msgs.isEmpty()) msgs = d["ms"].toList();
    } else {
        msgs = d["msgs"].toList();
        if (msgs.isEmpty()) msgs = d["ms"].toList();
        if (msgs.isEmpty()) msgs = d["groupMsgs"].toList();
    }
    if (msgs.isEmpty()) {
        QVariantMap dd = d["data"].toMap();
        msgs = isGroup ? dd["groupMsgs"].toList() : dd["msgs"].toList();
        if (msgs.isEmpty()) msgs = isGroup ? dd["msgs"].toList() : dd["ms"].toList();
        if (msgs.isEmpty()) msgs = dd["ms"].toList();
    }


    for (int i = 0; i < msgs.size(); ++i) {
        handleWsPushedMessage(msgs[i].toMap(), isGroup);
    }
}

void ZaloService::handleWsPushedMessage(QVariantMap m, bool isGroup)
{
    QString rawUidFrom = m["uidFrom"].toString();
    QString rawIdTo    = m["idTo"].toString();
    bool isSelf = (rawUidFrom == "0");

    QString uidFrom = isSelf      ? m_uid : rawUidFrom;
    QString idTo    = (rawIdTo == "0") ? m_uid : rawIdTo;

    QString threadId;
    if (isGroup) {
        threadId = idTo;
    } else {
        threadId = isSelf ? idTo : uidFrom;
    }
    if (threadId.isEmpty() || threadId == m_uid) return;

    if (isGroupPollNotice(m)) {
        QString pollIdN = extractPollNoticeId(m);
        emit boardEventOccurred(threadId, "update_board", QString(), isSelf, 3, QString(), QString());
        return;
    }

    QString msgId = m["msgId"].toString();
    if (!msgId.isEmpty()) {
        if (m_seenMsgIds.contains(msgId)) {
            // Already saved, usually from the HTTP response of our own message, which had no server ts and
            // used the local clock. The WS push carries the real ts: patch the row instead of skipping it.
            QString serverTs = m["ts"].toString();
            if (!serverTs.isEmpty() && m_db) {
                const char *sqlFixTs = "UPDATE messages SET ts=? WHERE msgId=?";
                sqlite3_stmt *fixStmt = 0;
                if (sqlite3_prepare_v2(m_db, sqlFixTs, -1, &fixStmt, 0) == SQLITE_OK) {
                    sqlite3_bind_text(fixStmt, 1, serverTs.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(fixStmt, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                    sqlite3_step(fixStmt);
                    sqlite3_finalize(fixStmt);
                }
                emit messageTsCorrected(threadId, msgId, serverTs);
            }
            return;
        }
        m_seenMsgIds.insert(msgId);
    }
    QString cliMsgId = m["cliMsgId"].toString();

    // Tombstone guard (same as the cmd=510 history path): a "delete for me" msgId never reaches
    // UI/DB again, even on duplicate pushes.
    if (isMessageDeletedForMe(msgId)) {
        return;
    }

    QString recalledId = extractRecalledMsgId(m);
    if (!recalledId.isEmpty()) {
        markMessageRecalled(threadId, recalledId);
        return;
    }

    QString delMsgId, deleterUid;
    if (extractDeleteInfo(m, delMsgId, deleterUid)) {
        if (deleterUid == m_uid) {
            markMessageDeletedForMe(threadId, delMsgId);
        }
        return;
    }

    QString reactMsgId, reactUid, reactIcon;
    if (extractReactionInfo(m, reactMsgId, reactUid, reactIcon)) {
        if (reactIcon.isEmpty()) dbRemoveReaction(reactMsgId, reactUid);
        else                     dbSaveReaction(threadId, reactMsgId, reactUid, reactIcon);
        emit reactionUpdated(threadId, reactMsgId, reactUid, reactIcon);
        return;
    }

    QVariantMap out;
    out["msgId"]    = msgId;
    out["cliMsgId"] = cliMsgId;
    out["senderId"] = uidFrom;
    out["dName"]    = m["dName"].toString();
    out["ts"]       = m["ts"].toString();
    out["isGroup"]  = isGroup;
    out["isMine"]   = isSelf;
    out["msgType"]  = m["msgType"].toInt();

    // quote object: globalMsgId = quoted message id, "msg" = snippet, "fromD" = quoted sender name.
    // Empty quoteMsgId means the message is not a reply.
    QVariantMap quoteObj = m["quote"].toMap();
    if (!quoteObj.isEmpty()) {
        // globalMsgId/ownerId arrive as exact strings: quoteBigJsonInts() quotes bare 16+ digit numbers
        // before JSON.parse, which would otherwise round them (860110201644973228 -> ...200).
        out["quoteMsgId"]      = quoteObj["globalMsgId"].toString();
        out["quoteContent"]    = quoteObj["msg"].toString();
        out["quoteSenderName"] = quoteObj["fromD"].toString();
        out["quoteMsgType"]    = quoteObj["cliMsgType"].toInt();
        // ownerId = real uid of the quoted message's sender. In 1-1 chats Zalo reports "0" (self) for
        // our own quoted messages; it must be mapped to our uid or quoteIsMine is always false.
        QString qOwnerIdStr = quoteObj["ownerId"].toString();
        out["quoteOwnerId"] = (qOwnerIdStr == "0") ? m_uid : qOwnerIdStr;
    }

    int mt = m["msgType"].toInt();
    if (mt == 0) {
        mt = m["type"].toInt();
        if (mt == 0) mt = m["mt"].toInt();
        if (mt == 0) mt = m["msgtype"].toInt();
    }
    if (mt == 0) {
        QString mtStr = m["msgType"].toString().toLower();
        if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
        if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
            mt = 2;
        else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
            mt = 3;
        // chat.recommended covers two bubble kinds, told apart by content.action (confirmed on device,
        // not in zca-js): "recommened.calltime"/"recommened.misscall" = call log (params JSON with
        // duration/calltype/isCaller), "recommened.link" = link share (title/description/href/thumb).
        // Read the action BEFORE setting mt, or link shares show up as voice calls.
        // Call logs are normalized to {callResult, callKind, duration}.
        else if (mtStr.contains("chat.recommended"))
        {
            QVariantMap peekMap = m["content"].toMap();
            if (peekMap.isEmpty()) {
                QString peekStr = m["content"].toString();
                if (!peekStr.isEmpty() && peekStr.trimmed().startsWith("{"))
                    peekMap = jsonToMap(peekStr.toUtf8());
            }
            QString peekAction = peekMap["action"].toString();
            mt = peekAction.contains("link") ? 6 : 4;
        }
        // Sticker content is {"id","catId","type"}: id plugs straight into
        // https://zalo-api.zadn.vn/api/emoticon/sticker/webpc?eid={id}; catId and getStickersDetail
        // aren't needed to display received stickers. Normalized to {"stickerId":N}.
        else if (mtStr.contains("chat.sticker"))
            mt = 5;
    }

    if (mt == 5) {
        QVariantMap sm = m["content"].toMap();
        if (sm.isEmpty()) {
            QString sStr = m["content"].toString();
            if (!sStr.isEmpty() && sStr.trimmed().startsWith("{"))
                sm = jsonToMap(sStr.toUtf8());
        }
        qint64 stickerId = sm["id"].toString().toLongLong();
        if (stickerId == 0) stickerId = sm["id"].toLongLong();
        QString newContent = QString("{\"stickerId\":%1}").arg(stickerId);
        m["content"] = newContent;
        // Start the download here in C++: QML init signals (Component.onCompleted/onVisibleChanged)
        // proved unreliable from the nested delegate. downloadSticker() dedups; QML only reads
        // stickerLocalPath.
        if (stickerId > 0) downloadSticker(QString::number(stickerId));
    }

    if (mt == 6) {
        QVariantMap lm = m["content"].toMap();
        if (lm.isEmpty()) {
            QString lStr = m["content"].toString();
            if (!lStr.isEmpty() && lStr.trimmed().startsWith("{"))
                lm = jsonToMap(lStr.toUtf8());
        }
        QString lTitle = lm["title"].toString();
        QString lDesc  = lm["description"].toString();
        QString lHref  = lm["href"].toString();
        QString lThumb = lm["thumb"].toString();
        QString esc = lTitle; esc.replace("\\", "\\\\").replace("\"", "\\\"");
        QString descEsc = lDesc; descEsc.replace("\\", "\\\\").replace("\"", "\\\"");
        QString hrefEsc = lHref; hrefEsc.replace("\\", "\\\\").replace("\"", "\\\"");
        QString thumbEsc = lThumb; thumbEsc.replace("\\", "\\\\").replace("\"", "\\\"");
        QString newContent = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                                  .arg(esc).arg(descEsc).arg(hrefEsc).arg(thumbEsc);
        m["content"] = newContent;
    }

    if (mt == 4) {
        QVariantMap rm = m["content"].toMap();
        if (rm.isEmpty()) {
            QString rStr = m["content"].toString();
            if (!rStr.isEmpty() && rStr.trimmed().startsWith("{"))
                rm = jsonToMap(rStr.toUtf8());
        }
        QString action = rm["action"].toString();
        QVariant paramsV = rm["params"];
        QVariantMap paramsMap = (paramsV.type() == QVariant::String)
            ? jsonToMap(paramsV.toString().toUtf8())
            : paramsV.toMap();
        QString callResult = action.contains("misscall") ? "missed" : "ended";
        QString callKind   = (paramsMap["calltype"].toInt() == 1) ? "video" : "voice";
        qint64  duration   = paramsMap["duration"].toString().toLongLong();
        QString newContent = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}")
                                  .arg(callResult).arg(callKind).arg(duration);
        m["content"] = newContent;
    }

    if (mt == 3) {
        QVariantMap fm = m["content"].toMap();
        if (fm.isEmpty()) {
            QString fStr = m["content"].toString();
            if (!fStr.isEmpty() && fStr.trimmed().startsWith("{"))
                fm = jsonToMap(fStr.toUtf8());
        }
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
            QString newContent = QString("{\"fileName\":\"%1\",\"href\":\"%2\"")
                                      .arg(fTitleEsc).arg(fHref);
            if (fSize > 0) newContent += QString(",\"fileSize\":%1").arg(fSize);
            newContent += "}";
            m["content"] = newContent;
        } else {
        }
    }

    // Qt4's QScriptEngine converts nested JSON objects to QVariantMap, so content.toString() is
    // empty for photo/object payloads: re-serialize to a JSON string.
    QString rawContent = m["content"].toString();
    if (rawContent.isEmpty()) {
        QVariantMap cm = m["content"].toMap();
        if (!cm.isEmpty()) {
            rawContent = "{";
            QStringList cmKeys = cm.keys();
            for (int k = 0; k < cmKeys.size(); ++k) {
                if (k > 0) rawContent += ",";
                QString v = cm[cmKeys[k]].toString();
                v.replace("\\", "\\\\").replace("\"", "\\\"");
                rawContent += "\"" + cmKeys[k] + "\":\"" + v + "\"";
            }
            rawContent += "}";
        }
    }

    // No content: fall back to paramsExt/previewThumb, where realtime WS sometimes carries photo data.
    if (rawContent.isEmpty()) {
        QString paramsExtStr = m["paramsExt"].toString();
        QString previewThumb = m["previewThumb"].toString();
    }

    // Realtime photo: msgType may be 0 with URLs in paramsExt/previewThumb. mt == 3 (video/file) is
    // excluded so a share.file without href stays text instead of being taken for a photo.
    if (mt != 3 && mt != 4 && mt != 5 && (mt == 2 || rawContent.isEmpty())) {
        QString normalized = normalizePhotoContent(m, rawContent);
        if (normalized != rawContent && !normalized.isEmpty()) {
            rawContent = normalized;
            mt = 2;
            out["msgType"] = 2;
        }
    }
    if (mt == 2) {
        QString nUrl, hUrl, tUrl, fSizeStr;
        if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
            QVariantMap cm = jsonToMap(rawContent.toUtf8());
            nUrl = cm["normalUrl"].toString();
            hUrl = cm["hdUrl"].toString();
            tUrl = cm["thumbUrl"].toString();
            if (nUrl.isEmpty()) nUrl = cm["href"].toString();
            if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
            if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
            if (fSizeStr.isEmpty()) fSizeStr = cm["hdSize"].toString();
            if (fSizeStr.isEmpty()) fSizeStr = cm["fileSize"].toString();
        }
        if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
        if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
        if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
        if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
        if (tUrl.isEmpty()) tUrl = m["thumb"].toString();
        if (fSizeStr.isEmpty()) fSizeStr = m["hdSize"].toString();
        if (nUrl.isEmpty()) {
            QVariantMap att = m["attach"].toMap();
            if (att.isEmpty()) {
                QString attStr = m["attach"].toString();
                if (!attStr.isEmpty() && attStr.startsWith("{"))
                    att = jsonToMap(attStr.toUtf8());
            }
            if (!att.isEmpty()) {
                if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = att["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = att["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = att["href"].toString();
                if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                if (tUrl.isEmpty()) tUrl = att["thumb"].toString();
                if (fSizeStr.isEmpty()) fSizeStr = att["hdSize"].toString();
            }
        }
        if (nUrl.isEmpty()) {
            QVariantMap prm = m["params"].toMap();
            if (prm.isEmpty()) {
                QString prmStr = m["params"].toString();
                if (!prmStr.isEmpty() && prmStr.startsWith("{"))
                    prm = jsonToMap(prmStr.toUtf8());
            }
            if (!prm.isEmpty()) {
                if (nUrl.isEmpty()) nUrl = prm["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = prm["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = prm["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = prm["href"].toString();
                if (tUrl.isEmpty()) tUrl = prm["thumb"].toString();
                if (fSizeStr.isEmpty()) fSizeStr = prm["hdSize"].toString();
            }
        }

        if (nUrl.isEmpty()) nUrl = hUrl;
        if (nUrl.isEmpty()) nUrl = tUrl;
        if (tUrl.isEmpty()) tUrl = nUrl;
        if (hUrl.isEmpty()) hUrl = nUrl;

        if (!nUrl.isEmpty()) {
            QString caption;
            if (!rawContent.isEmpty() && rawContent.contains("\"caption\":\"")) {
                QVariantMap prevCm = jsonToMap(rawContent.toUtf8());
                caption = prevCm["caption"].toString();
            }
            if (caption.isEmpty()) {
                QVariantMap cm = jsonToMap(m["content"].toString().toUtf8());
                if (!cm.isEmpty()) {
                    caption = cm["title"].toString();
                    if (caption.isEmpty()) caption = cm["description"].toString();
                }
            }
            if (caption.isEmpty()) caption = m["title"].toString();

            if (!caption.isEmpty()) {
                caption.replace("\\", "\\\\").replace("\"", "\\\"")
                       .replace("\n", "\\n").replace("\r", "\\r")
                       .replace("\t", "\\t");
            }

            // For the echo of our own photo, prefer fileSize/fileName known from sendPhoto() over the
            // server's (possibly missing) hdSize.
            QVariantMap sentInfo;
            bool haveSentInfo = false;
            if (isSelf && !cliMsgId.isEmpty() && m_pendingSentPhotoInfo.contains(cliMsgId)) {
                sentInfo = m_pendingSentPhotoInfo.value(cliMsgId);
                haveSentInfo = true;
            }

            qint64 fSize = haveSentInfo ? sentInfo.value("fileSize", 0).toLongLong()
                                         : fSizeStr.toLongLong();
            QString fName = haveSentInfo ? sentInfo.value("fileName").toString() : QString();
            if (!fName.isEmpty()) fName.replace("\\", "\\\\").replace("\"", "\\\"");

            rawContent = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"")
                             .arg(nUrl).arg(tUrl).arg(hUrl);
            if (fSize > 0) rawContent += QString(",\"fileSize\":%1").arg(fSize);
            if (!fName.isEmpty()) rawContent += QString(",\"fileName\":\"%1\"").arg(fName);
            if (!caption.isEmpty()) rawContent += QString(",\"caption\":\"%1\"").arg(caption);
            rawContent += "}";

            // Self-echo of our own photo: reuse the local cached file. Fetching the CDN copy right after
            // upload races (can 404/be empty) and left our own photos as permanent grey boxes. The file
            // was copied to the persistent "/tmp" cache at pick time (cacheLocalImage()) and is only
            // removed by clearCache().
            if (haveSentInfo) {
                QString localP = sentInfo.value("localPath").toString();
                QString fsPath = localP.startsWith("file://") ? localP.mid(7) : localP;
                if (!fsPath.isEmpty() && QFile::exists(fsPath)) {
                    QSize dim = imageDimensions(localP);
                    out["localImage"] = localP;
                    out["imgWidth"]   = dim.width();
                    out["imgHeight"]  = dim.height();
                    m_avatarCache[nUrl] = localP;
                }
                m_pendingSentPhotoInfo.remove(cliMsgId);
            }
        }
        if (nUrl.isEmpty()) {
            qWarning() << "[WS] photo message without URL";
        }
        // nUrl is not an http URL (Zalo protobuf thumbnail): decode the thumbnail for a quick preview,
        // then fetch the full-res URL via WS cmd=510 history.
        if (!nUrl.isEmpty() && !nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
            downloadImageMessage(msgId, nUrl, threadId);
            if (!m_pendingPhotoMsgIds.contains(msgId)) {
                fetchPhotoViaWs510(msgId, threadId);
                fetchPhotoViaHttp(msgId, threadId);
            }
        } else if (!nUrl.isEmpty() && nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
            downloadImageMessage(msgId, nUrl, threadId);
        }
    }
    // Always sync out["msgType"] with the locally resolved mt: Zalo sometimes sends msgType as a
    // string ("chat.photo") and QVariant::toInt() silently returns 0, which left photo bubbles
    // empty because QML only copies the cached local image when msgType === 2.
    out["msgType"] = mt;
    out["content"] = rawContent;

    dbSaveMessage(out, threadId);
    // Keep m_threadLastMsgId updated for realtime messages too (not only cmd=510); otherwise
    // fetchMessages() falls back to lastId="0" and re-fetches the whole history.
    if (!msgId.isEmpty()) {
        qint64 numH = msgId.toLongLong();
        if (numH > m_threadLastMsgId.value(threadId, "0").toLongLong())
            m_threadLastMsgId[threadId] = msgId;
    }
    emit newMessage(threadId, out);
    if (!isSelf && threadId != m_activeThreadId && !m_mutedThreads.contains(threadId)) {
        QString senderName = out["dName"].toString();
        if (senderName.isEmpty()) senderName = "Unknown";
        int mt = out["msgType"].toInt();
        QString msgPreview;
        if (mt == 2) {
            msgPreview = "[Photo]";
        } else if (mt == 3) {
            QString cs = out["content"].toString();
            QString fn;
            int fi = cs.indexOf("\"fileName\":\"");
            if (fi >= 0) {
                fi += 12;
                int fe = cs.indexOf("\"", fi);
                if (fe > fi) fn = cs.mid(fi, fe - fi);
            }
            msgPreview = fn.isEmpty() ? "[File]" : ("[File] " + fn);
        } else if (mt == 4) {
            QString cs = out["content"].toString();
            bool missed = cs.contains("\"callResult\":\"missed\"");
            bool video  = cs.contains("\"callKind\":\"video\"");
            msgPreview = missed ? (video ? "Missed video call" : "Missed call")
                                : (video ? "Video call" : "Voice call");
        } else if (mt == 5) {
            msgPreview = "[Sticker]";
        } else if (mt == 6) {
            QString cs = out["content"].toString();
            QString lt;
            int li = cs.indexOf("\"linkTitle\":\"");
            if (li >= 0) {
                li += 13;
                int le = cs.indexOf("\"", li);
                if (le > li) lt = cs.mid(li, le - li);
            }
            msgPreview = lt.isEmpty() ? "[Link]" : lt.left(80);
        } else {
            msgPreview = out["content"].toString().left(80);
        }
        if (msgPreview.isEmpty()) msgPreview = "[Message]";
        bool isGrp = out["isGroup"].toBool();
        QString notifTitle = isGrp ? m_groupNames.value(threadId, "Zalo10") : "Zalo10";
        sendHubNotification(notifTitle, senderName + ": " + msgPreview, threadId, isGrp);
        sendBannerNotification(notifTitle, senderName + ": " + msgPreview, threadId, isGrp);
    }
    // The user is looking at this thread, so the message is read: tell Zalo's servers too, or the
    // official app keeps counting it as unread.
    if (!isSelf && threadId == m_activeThreadId) sendThreadSeenRemote(threadId, out["isGroup"].toBool());
    if (threadId == m_activeThreadId && !msgId.isEmpty()) {
        qint64 newNum = msgId.toLongLong();
        qint64 curNum = m_lastPollMsgId.toLongLong();
        if (newNum > curNum) m_lastPollMsgId = msgId;
    }
}
