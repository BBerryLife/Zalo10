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
