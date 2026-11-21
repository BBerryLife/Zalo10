#include "core/contacts/ContactsCommon.hpp"

// Friends come page by page (FRIENDS_PAGE_SIZE) so a huge list never needs one giant reply to be
// decrypted and parsed at once. Whether the server really honours page/count is not known, so the
// loop adapts: a reply bigger than a page means "count ignored" (done), a page that adds no new
// uid means "page ignored" (done), page 0 is probed once in case pages start at 0, and a failed
// first page falls back to the old single request (count 20000).
static const int FRIENDS_PAGE_SIZE = 500;
static const int FRIENDS_MAX_PAGES = 60;

void ZaloService::fetchFriends()
{
    if (!m_loggedIn) return;
    if (m_isFetchingFriends) {
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchFriendsTime > 0 && (now - m_lastFetchFriendsTime) < FETCH_COOLDOWN_MS) {
        return;
    }
    m_isFetchingFriends = true;
    m_friendsAccum.clear();
    m_friendsSeen.clear();
    m_friendsPage = 1;
    m_friendsRetry = 0;
    m_friendsLegacy = false;
    requestFriendsPage();
}

void ZaloService::onFriendsNextPage()
{
    if (!m_isFetchingFriends) return;
    requestFriendsPage();
}

void ZaloService::requestFriendsPage()
{
    // zca-js uses GET with params in the query string, not a POST body.
    QVariantMap innerParams;
    innerParams["incInvalid"]  = 1;
    innerParams["page"]        = m_friendsLegacy ? 1 : m_friendsPage;
    innerParams["count"]       = m_friendsLegacy ? 20000 : FRIENDS_PAGE_SIZE;
    innerParams["avatar_size"] = 120;
    innerParams["actiontime"]  = 0;
    innerParams["imei"]        = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_profileServiceUrl + "/api/social/friend/getfriends"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchFriendsDone()));
}

void ZaloService::onFetchFriendsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // HTTP 429 (rate limit): the body is HTML, not JSON.
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        if (m_friendsPage > 1 && !m_friendsLegacy && m_friendsRetry < 2) {
            ++m_friendsRetry;
            qWarning() << "[Contacts] fetchFriends page" << m_friendsPage << "got 429, retry" << m_friendsRetry;
            QTimer::singleShot(3000 * m_friendsRetry, this, SLOT(onFriendsNextPage()));
            return;
        }
        qWarning() << "[Contacts] fetchFriends: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingFriends = false;
        m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit friendsFetchFailed();
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Contacts] fetchFriends:" << root["error_message"].toString();
        if (!m_friendsLegacy && m_friendsPage == 1 && m_friendsAccum.isEmpty()) {
            qWarning() << "[Contacts] fetchFriends: paged request refused, using single request";
            m_friendsLegacy = true;
            requestFriendsPage();
            return;
        }
        if (m_friendsPage == 0) {                 // page 0 probe refused: just move on
            m_friendsPage = 2;
            QTimer::singleShot(350, this, SLOT(onFriendsNextPage()));
            return;
        }
        if (!m_friendsAccum.isEmpty()) { finishFriends(); return; }
        m_isFetchingFriends = false;
        emit friendsFetchFailed();
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());

    QVariantList friends;
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (outer.contains("data") && outer["data"].type() == QVariant::List)
        friends = outer["data"].toList();
    else if (outer.contains("friends") && outer["friends"].type() == QVariant::List)
        friends = outer["friends"].toList();
    else {
        QVariantList arr = jsonToList(dec.toUtf8());
        if (!arr.isEmpty())
            friends = arr;
    }

    int added = 0;
    for (int i = 0; i < friends.size(); ++i) {
        QVariantMap f = friends[i].toMap();
        QString uid = f["userId"].toString();
        if (uid.isEmpty()) uid = f["uid"].toString();
        if (uid.isEmpty() || m_friendsSeen.contains(uid)) continue;
        m_friendsSeen.insert(uid);
        m_friendsAccum.append(f);
        ++added;
    }
    qDebug() << "[Contacts] fetchFriends page" << m_friendsPage << "got" << friends.size()
             << "new" << added << "total" << m_friendsAccum.size();

    if (m_friendsLegacy || friends.size() > FRIENDS_PAGE_SIZE) {   // one reply held everything
        finishFriends();
        return;
    }

    int next = -1;
    if (m_friendsPage == 1) {
        if (friends.size() >= FRIENDS_PAGE_SIZE) next = 0;
        else if (friends.isEmpty() && m_friendsAccum.isEmpty()) {
            qWarning() << "[Contacts] fetchFriends: empty first page, trying single request";
            m_friendsLegacy = true;
            requestFriendsPage();
            return;
        }
    } else if (m_friendsPage == 0) {
        next = 2;
    } else if (friends.size() >= FRIENDS_PAGE_SIZE && added > 0 && m_friendsPage < FRIENDS_MAX_PAGES) {
        next = m_friendsPage + 1;
    }

    if (next < 0) {
        finishFriends();
        return;
    }
    m_friendsPage = next;
    m_friendsRetry = 0;
    QTimer::singleShot(350, this, SLOT(onFriendsNextPage()));
}

void ZaloService::finishFriends()
{
    QVariantList friends = m_friendsAccum;
    m_friendsAccum.clear();
    m_friendsSeen.clear();

    QVariantList threads;
    QMap<QString, QString> friendNames;
    m_friendUids.clear();
    for (int i = 0; i < friends.size(); ++i) {
        QVariantMap f = friends[i].toMap();
        if (i == 0) {
            // Diagnostics for the phone-sync id mapping: field names with value length (d = all digits).
            QString info;
            for (QVariantMap::const_iterator it = f.constBegin(); it != f.constEnd(); ++it) {
                QString v = it.value().toString();
                bool digits = !v.isEmpty();
                for (int k = 0; k < v.size(); ++k) if (!v[k].isDigit()) { digits = false; break; }
                info += QString("%1[%2%3] ").arg(it.key()).arg(v.size()).arg(digits ? "d" : "");
            }
            qDebug() << "[Contacts] friend entry fields:" << info;
        }
        QString uid  = f["userId"].toString();
        if (uid.isEmpty()) uid = f["uid"].toString();
        QString name = f["zaloName"].toString();
        if (name.isEmpty()) name = f["displayName"].toString();
        if (name.isEmpty()) name = f["username"].toString();
        QString avatarUrl   = f["avatar"].toString();
        QString bgAvatarUrl = f["bgavatar"].toString();
        QString avatarBase   = avatarUrl.contains('?')   ? avatarUrl.left(avatarUrl.indexOf('?'))   : avatarUrl;
        QString bgAvatarBase = bgAvatarUrl.contains('?') ? bgAvatarUrl.left(bgAvatarUrl.indexOf('?')) : bgAvatarUrl;
        QString localAvatar   = m_avatarCache.value(avatarBase,   m_avatarCache.value(avatarUrl,   ""));
        QString localBgAvatar = m_avatarCache.value(bgAvatarBase, m_avatarCache.value(bgAvatarUrl, ""));

        QVariantMap t;
        t["threadId"]      = uid;
        t["name"]          = name;
        t["isGroup"]       = false;
        t["avatar"]        = avatarUrl;
        t["bgavatar"]      = bgAvatarUrl;
        t["localAvatar"]   = localAvatar;
        t["localBgAvatar"] = localBgAvatar;
        t["unread"]        = 0;
        t["lastMessage"]   = "";
        if (!uid.isEmpty() && !name.isEmpty()) {
            friendNames[uid] = name;
            threads.append(t);
            m_friendUids << uid;
        }
    }

    // Friends are group members too: their names answer most "who is this" questions for free.
    rememberMemberNames(friendNames);

    m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch();

    if (!threads.isEmpty()) {
        m_lastFriendThreads = threads;
        feedsOnFriends(threads, true);
        emit friendsReady(threads);
        m_isFetchingFriends = false;
    } else {
        m_isFetchingFriends = false;
        emit friendsFetchFailed();
    }
}

void ZaloService::fetchServerQuickMessages()
{
    if (!m_loggedIn) {
        emit serverQuickMessagesReady(0, 0, "Not logged in");
        return;
    }

    QString svcUrl = m_quickMessageServiceUrl;
    if (svcUrl.isEmpty()) svcUrl = m_profileServiceUrl;
    if (svcUrl.isEmpty()) {
        emit serverQuickMessagesReady(0, 0, "Service URL unavailable — try logging in again");
        return;
    }

    QVariantMap innerParams;
    innerParams["version"] = 0;
    innerParams["lang"]    = 0;
    innerParams["imei"]    = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = svcUrl + "/api/quickmessage/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchServerQuickMessagesDone()));
}

void ZaloService::onFetchServerQuickMessagesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        emit serverQuickMessagesReady(0, 0, "Server is busy (429) — try again in a bit");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        QString em = root["error_message"].toString();
        qDebug() << "[Contacts] fetchServerQuickMessages:" << em;
        emit serverQuickMessagesReady(0, 0, em.isEmpty() ? "Request failed" : em);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());

    // Double-wrapped like the other endpoints: the decrypted blob is {error_code, error_message,
    // data:{...}} and the real {cursor, version, items} lives in the nested "data".
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap payload = outer.value("data").toMap();
    QVariantList items = payload.value("items").toList();
    if (items.isEmpty() && outer.contains("items"))
        items = outer.value("items").toList();

    QSet<QString> existingNames;
    QVariantList localQm = getQuickMessages();
    for (int i = 0; i < localQm.size(); ++i)
        existingNames.insert(localQm[i].toMap().value("name").toString().toLower());

    int imported = 0, skipped = 0;
    for (int i = 0; i < items.size(); ++i) {
        QVariantMap it = items[i].toMap();
        QString name = it.value("keyword").toString().trimmed();
        QString content = it.value("message").toMap().value("title").toString().trimmed();
        if (name.isEmpty() || content.isEmpty()) { skipped++; continue; }
        if (existingNames.contains(name.toLower())) { skipped++; continue; }

        int newId = addQuickMessage(name, content);
        if (newId >= 0) { imported++; existingNames.insert(name.toLower()); }
        else skipped++;
    }

    emit serverQuickMessagesReady(imported, skipped, "");
}

void ZaloService::fetchInvites()
{
    if (!m_loggedIn) return;

    QString base = m_friendServiceUrl.isEmpty() ? m_profileServiceUrl : m_friendServiceUrl;
    QVariantMap innerParams;
    innerParams["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/friend/recommendsv2/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchInvitesDone()));
}

void ZaloService::onFetchInvitesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);

    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] fetchInvites error_code:" << ec;
        emit invitesReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());

    QVariantList recommItems = outer["recommItems"].toList();
    if (recommItems.isEmpty() && outer.contains("data")) {
        QVariant dataV = outer["data"];
        if (dataV.type() == QVariant::Map)
            recommItems = dataV.toMap()["recommItems"].toList();
        else if (dataV.type() == QVariant::List)
            recommItems = dataV.toList();
    }

    QVariantList invites;
    for (int i = 0; i < recommItems.size(); ++i) {
        QVariantMap item = recommItems[i].toMap();
        QVariantMap info = item["dataInfo"].toMap();

        // Use dataInfo.recommType (1=PYMK, 2=ReceivedFriendRequest), not the outer recommItemType:
        // filtering on the outer field can't tell PYMK from real invites.
        int itemType = info["recommType"].toInt();


        if (itemType != 2) continue;

        if (info.isEmpty()) continue;

        QString uid = info["userId"].toString();
        if (uid.isEmpty()) uid = info["uid"].toString();
        if (uid.isEmpty()) uid = info["fid"].toString();
        if (uid.isEmpty()) continue;

        QString name = info["zaloName"].toString();
        if (name.isEmpty()) name = info["displayName"].toString();
        if (name.isEmpty()) name = info["fullName"].toString();

        QString avatarUrl = info["avatar"].toString();

        QString msg;
        QVariant recommInfoV = info["recommInfo"];
        if (recommInfoV.type() == QVariant::Map) {
            QVariantMap recommInfo = recommInfoV.toMap();
            msg = recommInfo["message"].toString();
            if (msg.isEmpty()) msg = recommInfo["customText"].toString();
        }
        if (msg.isEmpty()) msg = info["msg"].toString();
        if (msg.trimmed().startsWith("{") || msg.trimmed().startsWith("["))
            msg = QString();

        QVariantMap inv;
        inv["uid"]    = uid;
        inv["name"]   = name.isEmpty() ? uid : name;
        inv["avatar"] = avatarUrl;
        inv["msg"]    = msg.isEmpty() ? "Wants to be your friend" : msg;
        invites.append(inv);
    }
    emit invitesReady(invites);
}
