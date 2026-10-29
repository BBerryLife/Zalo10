#include "core/messages/MessagesCommon.hpp"

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
    sendThreadSeenRemote(threadId);
}

// Newest message in the thread that someone else sent (what a "seen" receipt refers to).
bool ZaloService::dbLastIncomingMessage(const QString &threadId, QVariantMap &out) const
{
    if (!m_db || threadId.isEmpty()) return false;
    const char *sql =
        "SELECT msgId,cliMsgId,senderId,msgType,isGroup FROM messages "
        "WHERE threadId=? AND isMine=0 ORDER BY CAST(ts AS INTEGER) DESC LIMIT 1;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out["msgId"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        out["cliMsgId"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        out["senderId"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        out["msgType"]  = sqlite3_column_int(stmt, 3);
        out["isGroup"]  = (sqlite3_column_int(stmt, 4) == 1);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

// Mirrors zca-js sendSeenEvent (POST chat /api/message/seenv2 or group /api/group/seenv2) and
// removeUnreadMark (POST conversation /api/conv/removeUnreadMark). Without these the official
// Zalo keeps showing the thread as unread after it was read here.
void ZaloService::sendThreadSeenRemote(const QString &threadId, bool isGroupFallback)
{
    if (!m_loggedIn || threadId.isEmpty()) return;

    QVariantMap last;
    bool haveMsg = dbLastIncomingMessage(threadId, last);
    bool isGroup = haveMsg ? last["isGroup"].toBool()
                           : (threadId == m_activeThreadId ? m_activeThreadIsGroup : isGroupFallback);

    if (haveMsg) {
        // Internal msgType -> Zalo wire string.
        QString mt;
        switch (last["msgType"].toInt()) {
        case 2:  mt = "chat.photo";       break;
        case 3:  mt = "share.file";       break;
        case 5:  mt = "chat.sticker";     break;
        case 6:  mt = "chat.recommended"; break;
        default: mt = "webchat";          break;
        }
        QString infos = QString("{\"data\":[{\"cmi\":%1,\"gmi\":%2,\"si\":%3,\"di\":%4,\"mt\":%5,"
                                "\"st\":0,\"at\":0,\"cmd\":0,\"ts\":0}],%6:%7}")
            .arg(jsonQuote(last["cliMsgId"].toString()))
            .arg(jsonQuote(last["msgId"].toString()))
            .arg(jsonQuote(last["senderId"].toString()))
            .arg(jsonQuote(isGroup ? threadId : QString("0")))
            .arg(jsonQuote(mt))
            .arg(isGroup ? "\"grid\"" : "\"senderId\"")
            .arg(jsonQuote(threadId));

        QVariantMap params;
        params["msgInfos"] = infos;
        if (isGroup) params["imei"] = m_imei;

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
        QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);
        QString base = isGroup ? m_groupServiceUrl : m_chatServiceUrl;
        if (!base.isEmpty()) {
            QString urlStr = base + (isGroup ? "/api/group/seenv2" : "/api/message/seenv2")
                           + "?zpw_ver=" + QString::number(API_VERSION)
                           + "&zpw_type=" + QString::number(API_TYPE)
                           + "&nretry=0";
            QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            QNetworkReply *reply = m_manager->post(req, body);
            connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
        }
    }

    QStringList users, groups;
    if (isGroup) groups << threadId; else users << threadId;
    sendRemoveUnreadMark(users, groups);
}

void ZaloService::sendRemoveUnreadMark(const QStringList &userIds, const QStringList &groupIds)
{
    if (!m_loggedIn || m_conversationServiceUrl.isEmpty()) return;
    if (userIds.isEmpty() && groupIds.isEmpty()) return;

    const QString ts = QString::number((qint64)QDateTime::currentMSecsSinceEpoch());
    QStringList usersQ, groupsQ, usersData, groupsData;
    for (int i = 0; i < userIds.size(); ++i) {
        usersQ << jsonQuote(userIds[i]);
        usersData << QString("{\"id\":%1,\"ts\":%2}").arg(jsonQuote(userIds[i])).arg(ts);
    }
    for (int i = 0; i < groupIds.size(); ++i) {
        groupsQ << jsonQuote(groupIds[i]);
        groupsData << QString("{\"id\":%1,\"ts\":%2}").arg(jsonQuote(groupIds[i])).arg(ts);
    }
    QString inner = QString("{\"convsUser\":[%1],\"convsGroup\":[%2],"
                            "\"convsUserData\":[%3],\"convsGroupData\":[%4]}")
        .arg(usersQ.join(","), groupsQ.join(","), usersData.join(","), groupsData.join(","));

    // zca-js wraps the inner object as a JSON *string* under "param".
    QVariantMap params;
    params["param"] = inner;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_conversationServiceUrl + "/api/conv/removeUnreadMark"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), reply, SLOT(deleteLater()));
}

int ZaloService::markAllThreadsRead()
{
    QStringList ids = unreadThreadIds();
    int count = 0;
    for (int i = 0; i < ids.size(); ++i) {
        const QString tid = ids[i];
        if (tid.isEmpty()) continue;
        m_hub->markThreadRead(tid);
        setThreadUnread(tid, false);
        emit threadRead(tid);
        sendThreadSeenRemote(tid, isGroupHubThread(tid));
        ++count;
    }
    return count;
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
        sendThreadSeenRemote(threadId);
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
