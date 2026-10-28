#include "core/websocket/WebSocketCommon.hpp"

static QByteArray inflateAuto(const QByteArray &in)
{
    if (in.isEmpty()) return QByteArray();
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in  = (Bytef*)in.constData();
    zs.avail_in = in.size();
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return QByteArray();
    QByteArray out;
    out.resize(in.size() * 8 + 4096);
    int outPos = 0, ret = Z_OK;
    do {
        if (outPos >= out.size()) out.resize(out.size() * 2);
        zs.next_out  = (Bytef*)(out.data() + outPos);
        zs.avail_out = out.size() - outPos;
        ret = inflate(&zs, Z_SYNC_FLUSH);
        outPos = zs.total_out;
    } while (ret == Z_OK && zs.avail_in > 0);
    inflateEnd(&zs);
    if (zs.total_out == 0) return QByteArray();
    out.resize(zs.total_out);
    return out;
}

// True if the uid list contains anyone other than us (delivered/seen events with only our own
// uid come from our other devices).
static bool uidListHasOther(const QVariantList &uids, const QString &self)
{
    for (int i = 0; i < uids.size(); ++i)
        if (uids[i].toString() != self) return true;
    return false;
}

void ZaloService::handleWsHandshakeReply(const QByteArray &data)
{
    QVariantMap parsed = jsonToMap(data);
    QString keyB64 = parsed["key"].toString();
    m_wsCipherKey = QByteArray::fromBase64(keyB64.toUtf8());
    qDebug() << "[WS] handshake OK";

    sendWsPing();
    if (m_listenTimer) m_listenTimer->start(25000);
    if (!m_pending510Toid.isEmpty()) {
        QString req510 = QString("{\"first\":true,\"lastId\":null,\"toid\":\"%1\",\"preIds\":[]}")
                         .arg(m_pending510Toid);
        sendWsRequest(510, 1, req510);
    }
    fetchPinConversations();
    QTimer::singleShot(1500, this, SLOT(onSyncStartDeferred()));
}

void ZaloService::handleWsHistoryReply(const QByteArray &data)
{
    QVariantMap outer = jsonToMap(data);
    QVariantMap d;
    int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
    if (encType == 0) {
        d = outer.contains("data") ? outer["data"].toMap() : outer;
    } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
        QString rawB64 = outer["data"].toString();
        if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
        QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
        QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
        if (encType == 2 && !plain.isEmpty()) {
            QByteArray inflated;
            inflated.resize(plain.size() * 8 + 4096);
            z_stream zs2; memset(&zs2, 0, sizeof(zs2));
            zs2.next_in = (Bytef*)plain.constData(); zs2.avail_in = plain.size();
            bool inflateOk2 = false;
            if (inflateInit2(&zs2, 15 + 16) == Z_OK) {
                int outPos2 = 0, r2 = Z_OK;
                do {
                    if (outPos2 >= inflated.size()) inflated.resize(inflated.size() * 2);
                    zs2.next_out  = (Bytef*)(inflated.data() + outPos2);
                    zs2.avail_out = inflated.size() - outPos2;
                    r2 = inflate(&zs2, Z_SYNC_FLUSH);
                    outPos2 = zs2.total_out;
                } while ((r2 == Z_OK || r2 == Z_BUF_ERROR) && zs2.avail_in > 0);
                inflateEnd(&zs2);
                if ((r2 == Z_STREAM_END || r2 == Z_OK) && zs2.total_out > 0) {
                    inflated.resize(zs2.total_out);
                    inflateOk2 = true;
                }
            }
            if (inflateOk2) {
                QVariantMap parsed = jsonToMap(inflated);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qWarning() << "[WS] inflate FAILED, trying raw plain";
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            }
        } else if (!plain.isEmpty()) {
            QVariantMap parsed = jsonToMap(plain);
            if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                d = parsed["data"].toMap();
            else
                d = parsed;
        } else {
        }
    } else {
        QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
        if (dec.isEmpty() || dec.trimmed() == "{}")
            dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
        QVariantMap r = jsonToMap(dec.toUtf8());
        d = r.contains("data") ? r["data"].toMap() : r;
    }

    QVariantList rawMsgs = d["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d["ms"].toList();

    if (m_sync.awaiting) {
        bool forSync = true;
        if (!m_pending510Toid.isEmpty()) {
            for (int si = 0; si < rawMsgs.size(); ++si) {
                QVariantMap sm = rawMsgs[si].toMap();
                QString sFrom = sm["uidFrom"].toString();
                QString sTo   = sm["idTo"].toString();
                if (sFrom == "0") sFrom = m_uid;
                if (sTo   == "0") sTo   = m_uid;
                if (sFrom == m_pending510Toid || sTo == m_pending510Toid) { forSync = false; break; }
            }
        }
        if (forSync) {
            handleSyncPage(rawMsgs);
            return;
        }
    }

    // emitThread comes from m_pending510Toid (the thread that sent the request), not a FIFO queue,
    // to avoid mismatches when the user switches tabs quickly.
    QString emitThread = m_pending510Toid.isEmpty() ? m_activeThreadId : m_pending510Toid;
    m_pending510Toid.clear();

    if (emitThread.isEmpty()) return;

    if (rawMsgs.isEmpty() && emitThread != m_activeThreadId) {
        return;
    }

    if (!rawMsgs.isEmpty()) {
        bool anyMatch = false;
        for (int vi = 0; vi < rawMsgs.size(); ++vi) {
            QVariantMap vm = rawMsgs[vi].toMap();
            QString vFrom = vm["uidFrom"].toString();
            QString vTo   = vm["idTo"].toString();
            if (vFrom == "0") vFrom = m_uid;
            if (vTo   == "0") vTo   = m_uid;
            if (vFrom == emitThread || vTo == emitThread
                || (vFrom == m_uid && vTo == emitThread)
                || (vTo == m_uid && vFrom == emitThread)) {
                anyMatch = true; break;
            }
        }
        if (!anyMatch) {
            return;
        }
    }

    processOldDmBatch(rawMsgs, emitThread, false);
}

    // cmd=601 subCmd=0: group_event (pin/note/poll board activity from any member, plus unrelated
    // membership/settings changes). Envelope is { controls: [...] }; relevant entries have
    // content.act_type == "group", content.act names the event and content.data is per-type
    // (sometimes JSON nested in a string). Not yet confirmed against the real server.
void ZaloService::handleWsGroupEvent(const QByteArray &data)
{
    QVariantMap outer = jsonToMap(data);
    QVariantMap d = decodeWsEnvelope(outer, "cmd601");

    QVariantList controls = d["controls"].toList();
    for (int i = 0; i < controls.size(); ++i) {
        QVariantMap control = controls[i].toMap();
        QVariantMap content = control["content"].toMap();
        QString actType = content["act_type"].toString();

        // Video/file upload completion arrives asynchronously here, not in the asyncfile/upload HTTP
        // response. content.data may be a map or a JSON string. fileId is matched against
        // m_pendingVideoUpload and step 2 continues in handleFileUploadDone().
        if (actType == "file_done") {
            QString fileId = content["fileId"].toString();
            QVariant fdV = content["data"];
            QVariantMap fdMap = (fdV.type() == QVariant::String)
                ? jsonToMap(fdV.toString().toUtf8())
                : fdV.toMap();
            QString fileUrl = fdMap["url"].toString();
            if (fileUrl.isEmpty()) fileUrl = fdMap["fileUrl"].toString();
            qDebug() << "[WS] file upload done";
            if (!fileId.isEmpty() && !fileUrl.isEmpty())
                handleFileUploadDone(fileId, fileUrl);
            continue;
        }

        if (actType == "syncmsgmb") {
            QVariant smV = content["data"];
            QVariantMap smData = (smV.type() == QVariant::String)
                ? jsonToMap(smV.toString().toUtf8())
                : smV.toMap();
            handlePhoneSyncEvent(content["act"].toString(), smData);
            continue;
        }

        if (actType != "group") continue;

        QString act = content["act"].toString();
        if (act == "join_reject") continue;

        QVariant dataV = content["data"];
        QVariantMap eventData = (dataV.type() == QVariant::String)
            ? jsonToMap(dataV.toString().toUtf8())
            : dataV.toMap();

        QString groupId = eventData.contains("groupId") ? eventData["groupId"].toString()
                                                          : eventData["group_id"].toString();
        if (groupId.isEmpty()) continue;

        QString title;
        bool isSelf = false;
        bool relevant = true;
        int topicType = -1;
        QString topicId;
        QString actorNameOut;

        if (act == "new_pin_topic" || act == "unpin_topic" || act == "update_pin_topic") {
            QString actorId = eventData["actorId"].toString();
            isSelf = (actorId == m_uid);
            QVariantMap topic = eventData["topic"].toMap();
            topicType = topic["type"].toInt();
            topicId = topic["id"].toString();
            QString actorName = isSelf ? "You" : memberDisplayName(actorId);
            if (actorName.isEmpty()) actorName = "Someone";
            actorNameOut = actorName;
            QString what = (topicType == 3) ? "a poll" : (topicType == 0) ? "a note" : "a message";
            if (act == "new_pin_topic")       title = actorName + " pinned " + what;
            else if (act == "unpin_topic")    title = actorName + " unpinned " + what;
            else                              title = actorName + " updated a pinned item";
        } else if (act == "update_board" || act == "remove_board") {
            QString sourceId = eventData["sourceId"].toString();
            isSelf = (sourceId == m_uid);
            QVariant gtV = eventData["groupTopic"];
            QVariantMap groupTopic = gtV.toMap();
            topicType = groupTopic["type"].toInt();
            topicId = groupTopic["id"].toString();
            QString actorName = isSelf ? "You" : memberDisplayName(sourceId);
            if (actorName.isEmpty()) actorName = "Someone";
            actorNameOut = actorName;
            if (act == "remove_board") {
                title = actorName + " removed a board item";
            } else if (topicType == 3) {
                title = actorName + " created a poll";
            } else if (topicType == 0) {
                title = actorName + " created a note";
            } else {
                // update_board also covers poll votes (the whole topic is resent on every vote), so "created"
                // can't be told from "voted" by topicType; keep the wording generic.
                title = actorName + " updated the group board";
            }
        } else if (act == "update_topic" || act == "remove_topic") {
            QString actorId = eventData.contains("creatorId") ? eventData["creatorId"].toString()
                                                                : eventData["sourceId"].toString();
            isSelf = (actorId == m_uid);
            topicId = eventData.contains("topicId") ? eventData["topicId"].toString()
                                                      : eventData["id"].toString();
            QString actorName = isSelf ? "You" : memberDisplayName(actorId);
            if (actorName.isEmpty()) actorName = "Someone";
            actorNameOut = actorName;
            title = (act == "remove_topic") ? (actorName + " removed a board item")
                                             : (actorName + " updated the group board");
        } else {
            relevant = false;
        }

        if (relevant && !title.isEmpty()) {
            emit boardEventOccurred(groupId, act, actorNameOut, isSelf, topicType, topicId, title);

            // Self-actions already get a Hub notification from the success path of onPinGroupMessageDone()/
            // onCreateGroupNoteDone()/onCreateGroupPollDone()/onVoteGroupPollDone(); skip them here.
            // Also skip when that group's board is open (ChatView reacts to boardEventOccurred).
            if (!isSelf && groupId != m_activeThreadId && !m_mutedThreads.contains(groupId)) {
                QString grpName = m_groupNames.value(groupId, "Zalo10");
                sendHubNotification(grpName, title, groupId, true);
                sendBannerNotification(grpName, title, groupId, true);
            }
        }
    }
}

void ZaloService::handleWsReceipts(qint32 cmd, const QByteArray &data)
{
    bool isGroup = (cmd == 522);
    QVariantMap outer = jsonToMap(data);
    int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : -1;
    QVariant rawData = outer["data"];
    QVariantMap d;
    // zca-js decodeEventData: with encrypt=0, "data" is a JSON STRING. decodeWsEnvelope() only
    // handles objects (delivereds/seens came back empty), so parse the string here.
    if (encType == 0) {
        d = (rawData.type() == QVariant::Map) ? rawData.toMap()
                                              : jsonToMap(rawData.toString().toUtf8());
    } else {
        d = decodeWsEnvelope(outer, "cmd502");
    }
    if (!d.contains("delivereds") && !d.contains("seens") && !d.contains("groupSeens")
        && d["data"].type() == QVariant::Map)
        d = d["data"].toMap();
    if (!d.contains("delivereds") && !d.contains("seens") && !d.contains("groupSeens")
        && rawData.type() == QVariant::String) {
        QByteArray plain = inflateAuto(QByteArray::fromBase64(rawData.toString().toUtf8()));
        if (!plain.isEmpty()) {
            QVariantMap parsed = jsonToMap(plain);
            d = parsed["data"].type() == QVariant::Map ? parsed["data"].toMap() : parsed;
        }
    }

    QVariantList delivereds = d["delivereds"].toList();
    QVariantList seens      = isGroup ? d["groupSeens"].toList() : d["seens"].toList();
    if (delivereds.isEmpty() && seens.isEmpty()) {
        QVariantMap dd = d["data"].toMap();
        delivereds = dd["delivereds"].toList();
        seens      = isGroup ? dd["groupSeens"].toList() : dd["seens"].toList();
    }

    for (int i = 0; i < delivereds.size(); ++i) {
        QVariantMap e = delivereds[i].toMap();
        QVariantList dUids = e["deliveredUids"].toList();
        QString threadId = isGroup ? e["groupId"].toString()
                                   : (dUids.isEmpty() ? QString() : dUids.first().toString());
        if (threadId.isEmpty() || threadId == m_uid) continue;
        if (!uidListHasOther(dUids, m_uid)) continue;
        if (dbAdvanceMessageStates(threadId, 2, e["msgId"].toString(), e["realMsgId"].toString()))
            emit messageStatesChanged(threadId);
    }
    for (int i = 0; i < seens.size(); ++i) {
        QVariantMap e = seens[i].toMap();
        QString threadId;
        if (isGroup) {
            threadId = e["groupId"].toString();
            if (!uidListHasOther(e["seenUids"].toList(), m_uid)) continue;
        } else {
            threadId = e["idTo"].toString();
        }
        if (threadId.isEmpty() || threadId == m_uid) continue;
        if (dbAdvanceMessageStates(threadId, 3, e["msgId"].toString(), e["realMsgId"].toString()))
            emit messageStatesChanged(threadId);
    }
}

    // cmd=612: realtime reactions on their own channel (not "chat.reaction" inside cmd=501/521,
    // which was an unconfirmed guess). The envelope has parallel lists "reacts" (1-1) and
    // "reactGroups" (group). Each entry's "content" is a JSON string:
    // { rMsg: [{gMsgID, cMsgID, msgType}], rIcon, rType, source }.
    // uidFrom == "0" means our own reaction echoed from another logged-in device.
void ZaloService::handleWsReactions(const QByteArray &data)
{
    QVariantMap outer = jsonToMap(data);
    QVariantMap d = decodeWsEnvelope(outer, "cmd612");

    QVariantList reacts      = d["reacts"].toList();
    QVariantList reactGroups = d["reactGroups"].toList();

    for (int pass = 0; pass < 2; ++pass) {
        const QVariantList &list = (pass == 0) ? reacts : reactGroups;
        bool isGroupPass = (pass == 1);

        for (int i = 0; i < list.size(); ++i) {
            QVariantMap entry = list[i].toMap();

            QVariant contentV = entry.value("content");
            QVariantMap content = (contentV.type() == QVariant::String)
                ? jsonToMap(contentV.toString().toUtf8())
                : contentV.toMap();
            if (content.isEmpty()) continue;

            QString rawUidFrom = entry.value("uidFrom").toString();
            QString rawIdTo    = entry.value("idTo").toString();
            bool isSelf = (rawUidFrom == "0");

            QString resolvedUidFrom = isSelf ? m_uid : rawUidFrom;
            QString resolvedIdTo    = (rawIdTo == "0") ? m_uid : rawIdTo;
            QString threadId = (isGroupPass || isSelf) ? resolvedIdTo : resolvedUidFrom;

            QVariantList rMsg = content.value("rMsg").toList();
            if (rMsg.isEmpty()) continue;
            QString reactMsgId = rMsg.first().toMap().value("gMsgID").toString();
            if (reactMsgId.isEmpty()) continue;

            int rType = content.value("rType").toInt();
            QString rIcon = content.value("rIcon").toString();
            QString icon = (rType < 0 || rIcon.isEmpty()) ? QString() : emojiToReactionIcon(rIcon);

            if (icon.isEmpty()) dbRemoveReaction(reactMsgId, resolvedUidFrom);
            else                dbSaveReaction(threadId, reactMsgId, resolvedUidFrom, icon);
            emit reactionUpdated(threadId, reactMsgId, resolvedUidFrom, icon);
        }
    }
}
