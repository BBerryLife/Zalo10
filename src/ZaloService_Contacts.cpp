#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include <bb/platform/Notification>
#include <bb/platform/NotificationDefaultApplicationSettings>
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

// Conversations, friends, group details/avatars, invites, and per-thread
// settings (mute, block, clear history, leave group).

void ZaloService::fetchConversations(){
    if (!m_loggedIn) return;
    if (m_isFetchingConversations) {
        qDebug() << "[Zalo] fetchConversations: already in progress, skipping duplicate call";
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchConvoTime > 0 && (now - m_lastFetchConvoTime) < FETCH_COOLDOWN_MS) {
        qDebug() << "[Zalo] fetchConversations: cooldown active, skipping ("
                 << (now - m_lastFetchConvoTime) << "ms since last fetch, need" << FETCH_COOLDOWN_MS << "ms)";
        return;
    }
    m_isFetchingConversations = true;

    QVariantMap qp;
    qp["zpw_ver"]  = QString::number(API_VERSION);
    qp["zpw_type"] = QString::number(API_TYPE);

    QString urlStr = buildRawUrl(m_groupServiceUrl + "/api/group/getlg/v4", qp);
    qDebug() << "[Zalo] fetchConversations URL:" << urlStr;
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchConvoDone()));

}

void ZaloService::onFetchConvoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[Zalo Error] fetchConversations Network Error:" << reply->errorString();
    }

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.isEmpty()) {
        emit conversationsReady(QVariantList());
        m_isFetchingConversations = false;
        return;
    }

    qDebug() << "[Zalo] fetchConvo raw (first200):" << raw.left(200);

    // Xử lý HTTP 429 Too Many Requests — raw là HTML, không phải JSON
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] fetchConversations: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo Error] fetchConvo error_code:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        // ec=600: zpw_sek expired — session cookies invalid, must re-login
        if (ec == 600) {
            qDebug() << "[Zalo] fetchConvo: session expired (600), emitting sessionExpired";
            m_loggedIn = false;
            emit loggedInChanged();
            emit sessionExpired();
            return;
        }
        emit conversationsReady(QVariantList());
        return;
    }
    m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch();

    QVariantList threads;
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchConvo decrypted (first150):" << dec.left(150);

    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

    QVariantMap gridVerMap = inner["gridVerMap"].toMap();
    qDebug() << "[Zalo] fetchConvo gridVerMap size:" << gridVerMap.size();

    QStringList groupIds = gridVerMap.keys();

    qDebug() << "[Zalo] fetchConvo found" << groupIds.size() << "groups, fetching details...";

    if (!groupIds.isEmpty()) {
        fetchGroupDetails(groupIds);
    } else {
        emit conversationsReady(QVariantList());
    }
}

void ZaloService::fetchGroupDetails(const QStringList &groupIds)
{
    if (groupIds.isEmpty()) return;

    QVariantMap gridVerMapObj;
    for (int i = 0; i < groupIds.size(); ++i)
        gridVerMapObj[groupIds[i]] = 0;
    QString gridVerMapStr = QString::fromUtf8(mapToJson(gridVerMapObj));
    qDebug() << "[Zalo] gridVerMap string (first100):" << gridVerMapStr.left(100);

    QVariantMap inner;
    inner["gridVerMap"] = gridVerMapStr;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(inner)));

    QString urlStr = m_groupServiceUrl + "/api/group/getmg-v2"
        + "?zpw_ver=" + QString::number(API_VERSION)
        + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("Origin",  "https://chat.zalo.me");
    req.setRawHeader("Referer", "https://chat.zalo.me/");

    QByteArray body = "params=" + QUrl::toPercentEncoding(encParams);
    qDebug() << "[Zalo] fetchGroupDetails POST" << urlStr;
    qDebug() << "[Zalo] fetchGroupDetails body (first100):" << QString::fromUtf8(body.left(100));

    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupDetailsDone()));
}

void ZaloService::onGroupDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] groupDetails raw (first200):" << raw.left(200);

    // Xử lý HTTP 429 Too Many Requests
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] groupDetails: got 429, backing off";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo Error] groupDetails outer error:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] groupDetails decrypted (first200):" << dec.left(200);

    QVariantMap outer = jsonToMap(dec.toUtf8());
    int ec2 = outer["error_code"].toInt();
    if (ec2 != 0) {
        qDebug() << "[Zalo Error] groupDetails inner error:" << ec2 << outer["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

    qDebug() << "[Zalo] groupDetails inner keys:" << inner.keys();

    QVariantList threads;

    QVariantMap gridInfoMap = inner["gridInfoMap"].toMap();
    if (!gridInfoMap.isEmpty()) {
        QStringList keys = gridInfoMap.keys();
        for (int i = 0; i < keys.size(); ++i) {
            QVariantMap g = gridInfoMap[keys[i]].toMap();
            QVariantMap t;
            QString gname = g["name"].toString();
            if (gname.isEmpty()) gname = "Nhom " + g["groupId"].toString().right(6);
            QString gid = g["groupId"].toString();
            t["threadId"] = gid;
            t["name"]     = gname;
            t["isGroup"]  = true;
            t["avatar"]   = g["avt"].toString();
            t["unread"]   = 0;
            threads.append(t);
            m_groupNames[gid] = gname; // cache for notifications

            // currentMems: per-member {id, dName, zaloName, ...} — see
            // GroupCurrentMem in zca-js's Group.ts. This is the reliable
            // source for uid->name (m_memberNames), unlike the per-message
            // wire dName field which is not trustworthy for incoming
            // messages (see m_memberNames' declaration comment in the header).
            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
        }
    } else {
        QVariantList grids = inner["gridInfos"].toList();
        for (int i = 0; i < grids.size(); ++i) {
            QVariantMap g = grids[i].toMap();
            QVariantMap t;
            QString gid = g["groupId"].toString();
            t["threadId"] = gid;
            t["name"]     = g["name"].toString();
            t["isGroup"]  = true;
            t["avatar"]   = g["avt"].toString();
            t["unread"]   = 0;
            threads.append(t);
            m_groupNames[gid] = g["name"].toString(); // cache

            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
        }
    }

    qDebug() << "[Zalo] groupDetails found" << threads.size() << "groups with names";
    if (!threads.isEmpty())
        emit conversationsReady(threads);
    m_isFetchingConversations = false;
}

void ZaloService::downloadAvatar(const QString &threadId, const QString &url)
{
    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;

    // Check cache theo cả full URL và base URL (fast path within this session)
    if (m_avatarCache.contains(url)) {
        emit avatarReady(threadId, m_avatarCache[url]);
        return;
    }
    if (m_avatarCache.contains(baseUrl)) {
        emit avatarReady(threadId, m_avatarCache[baseUrl]);
        return;
    }

    // Persistent dedup check: compare the new URL's hash against what we
    // already have on disk for this exact threadId (a stable per-person key —
    // background avatars are passed in as "bg_"+threadId, so they never collide
    // with the main avatar). This is what makes the cache survive app restarts,
    // logout/login, and toggling "Show Recalled Messages" (which never touches
    // images): if the person hasn't actually changed their picture, we reuse
    // the file already sitting in tmp instead of re-downloading it.
    QString newHash = md5Hex(baseUrl);
    QString storedHash, storedPath;
    if (avatarMetaLookup(threadId, storedHash, storedPath)) {
        QString fsPath = storedPath;
        if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
        if (storedHash == newHash && !fsPath.isEmpty() && QFile::exists(fsPath)) {
            // Same avatar URL as last time, and the cached file is still there
            // (i.e. the user hasn't run "Clear Cache") — reuse it, no network call.
            m_avatarCache[url] = storedPath;
            m_avatarCache[baseUrl] = storedPath;
            emit avatarReady(threadId, storedPath);
            return;
        }
        // Either the URL hash changed (genuinely a new profile picture) or the
        // file went missing — fall through and re-download below.
    }

    // Check cả full URL và base URL
    if (m_pendingAvatars.contains(url) || m_pendingAvatars.contains(baseUrl)) {
        QString pendingKey = m_pendingAvatars.contains(url) ? url : baseUrl;
        m_pendingAvatarWaiters[pendingKey].insert(threadId);
        return;
    }
    m_pendingAvatars.insert(baseUrl);
    m_pendingAvatarWaiters[baseUrl].clear();
    m_pendingAvatarWaiters[baseUrl].insert(threadId);

    QString httpUrl = url;
    if (httpUrl.startsWith("https://"))
        httpUrl = "http://" + httpUrl.mid(8);

    QUrl avatarQUrl(httpUrl);
    QNetworkRequest avatarReq(avatarQUrl);
    avatarReq.setRawHeader("Referer",    "https://chat.zalo.me/");
    avatarReq.setRawHeader("User-Agent", m_userAgent.toUtf8());
    avatarReq.setRawHeader("Accept",     "image/webp,image/apng,image/*,*/*;q=0.8");
    QNetworkReply *reply = m_manager->get(avatarReq);
    reply->setProperty("avatarUrl",      url);
    reply->setProperty("avatarThreadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onAvatarDownloaded()));
}

void ZaloService::onAvatarDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString url        = reply->property("avatarUrl").toString();
    QString threadId   = reply->property("avatarThreadId").toString();
    bool hasError      = (reply->error() != QNetworkReply::NoError);
    QByteArray data    = reply->readAll();
    reply->deleteLater();

    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;
    QSet<QString> waiters = m_pendingAvatarWaiters.take(baseUrl);
    if (waiters.isEmpty()) waiters = m_pendingAvatarWaiters.take(url);
    m_pendingAvatars.remove(baseUrl);
    m_pendingAvatars.remove(url);

    if (hasError || data.isEmpty()) {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "[Zalo] avatar download failed for" << threadId
                 << "error:" << reply->errorString()
                 << "HTTP:" << httpStatus;
        return;
    }

    // Fixed filename per-person (md5 of threadId, NOT of the URL): this means
    // a changed profile picture overwrites the same file in place instead of
    // leaving the old image as an orphaned file in tmp every time the CDN
    // hands back a different URL for an unchanged picture.
    QString fname = "/tmp/avatar_" + md5Hex(threadId) + ".jpg";
    QFile f(fname);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    QString localPath = "file://" + fname;
    m_avatarCache[url] = localPath;
    int qmark = url.indexOf('?');
    if (qmark > 0) m_avatarCache[url.left(qmark)] = localPath;

    // Persist the threadId -> urlHash -> localPath mapping so next launch (or
    // next refresh after logout/login) can recognise this exact avatar again
    // without re-downloading it.
    avatarMetaUpsert(threadId, md5Hex(baseUrl), localPath);

    qDebug() << "[Zalo] avatar saved:" << threadId << "->" << fname;
    foreach (const QString &wid, waiters)
        emit avatarReady(wid, localPath);
    if (!waiters.contains(threadId))
        emit avatarReady(threadId, localPath);

    // Cập nhật pending friends list
    if (!m_pendingFriends.isEmpty() && m_pendingFriendAvatarCount > 0) {
        QSet<QString> allWaiters = waiters;
        allWaiters.insert(threadId);
        bool updated = false;
        for (int i = 0; i < m_pendingFriends.size(); ++i) {
            QVariantMap t = m_pendingFriends[i].toMap();
            QString tid = t["threadId"].toString();
            if (allWaiters.contains(tid) && t["localAvatar"].toString().isEmpty()) {
                t["localAvatar"] = localPath;
                m_pendingFriends[i] = t;
                m_loadedFriendAvatarCount++;
                updated = true;
            }
        }
        Q_UNUSED(updated);
        // Re-emit khi đủ avatar
        if (m_loadedFriendAvatarCount >= m_pendingFriendAvatarCount) {
            qDebug() << "[Zalo] all friend avatars loaded, re-emitting friendsReady";
            QVariantList finalList = m_pendingFriends;
            m_pendingFriends.clear();
            m_pendingFriendAvatarCount = 0;
            m_loadedFriendAvatarCount  = 0;
            emit friendsReady(finalList);
        }
    }
}

void ZaloService::fetchFriends()
{
    if (!m_loggedIn) return;
    if (m_isFetchingFriends) {
        qDebug() << "[Zalo] fetchFriends: already in progress, skipping duplicate call";
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchFriendsTime > 0 && (now - m_lastFetchFriendsTime) < FETCH_COOLDOWN_MS) {
        qDebug() << "[Zalo] fetchFriends: cooldown active, skipping ("
                 << (now - m_lastFetchFriendsTime) << "ms since last fetch, need" << FETCH_COOLDOWN_MS << "ms)";
        return;
    }
    m_isFetchingFriends = true;

    // zca-js dùng GET, params trong query string (không phải POST body)
    QVariantMap innerParams;
    innerParams["incInvalid"]  = 1;
    innerParams["page"]        = 1;
    innerParams["count"]       = 20000;
    innerParams["avatar_size"] = 120;
    innerParams["actiontime"]  = 0;
    innerParams["imei"]        = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_profileServiceUrl + "/api/social/friend/getfriends"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchFriends GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchFriendsDone()));
}

void ZaloService::onFetchFriendsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchFriends raw (first300):" << raw.left(300);

    // Xử lý HTTP 429 Too Many Requests — raw là HTML, không phải JSON
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] fetchFriends: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingFriends = false;
        // Đặt cooldown dài hơn bình thường khi bị rate-limit
        m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchFriends:" << root["error_message"].toString();
        m_isFetchingFriends = false;
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchFriends decrypted (first300):" << dec.left(300);

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

    qDebug() << "[Zalo] fetchFriends found" << friends.size() << "friends";

    QVariantList threads;
    for (int i = 0; i < friends.size(); ++i) {
        QVariantMap f = friends[i].toMap();
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
        if (!uid.isEmpty() && !name.isEmpty())
            threads.append(t);
    }

    qDebug() << "[Zalo] fetchFriends parsed" << threads.size() << "valid friends";
    m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch();

    if (!threads.isEmpty()) {
        // IMPORTANT: compute needDownload and set m_pendingFriends/
        // m_pendingFriendAvatarCount BEFORE emit friendsReady() below, not
        // after. emit is synchronous here — QML's onFriendsReady handler
        // runs to completion (including up to ~87 back-to-back
        // zService.downloadAvatar() calls) on top of this very call stack
        // before control ever returns to this function. Previously the
        // bookkeeping below ran AFTER the emit, so for the entire duration
        // of that reentrant QML loop, m_pendingFriends was still the OLD
        // (possibly stale/empty) value — a real ordering hazard on top of
        // heavy QSet churn in downloadAvatar()/onAvatarDownloaded(). This
        // crashed with SIGSEGV inside Qt's own QHash internals
        // (QHash<QString,QHashDummyValue>::duplicateNode) on a fresh-login
        // run where the avatar cache was empty and all 87 friends hit that
        // pending-avatar bookkeeping path back-to-back in one synchronous
        // burst — the exact conditions this reordering removes.
        int needDownload = 0;
        for (int i = 0; i < threads.size(); ++i) {
            QVariantMap t = threads[i].toMap();
            if (t["localAvatar"].toString().isEmpty() && !t["avatar"].toString().isEmpty())
                needDownload++;
        }
        qDebug() << "[Zalo] friends: need to download" << needDownload << "avatars";
        if (needDownload > 0) {
            m_pendingFriends = threads;
            m_pendingFriendAvatarCount = needDownload;
            m_loadedFriendAvatarCount  = 0;
        }
        emit friendsReady(threads);
    }
    m_isFetchingFriends = false;
}

// Pulls the user's own quick-message list from their real Zalo account
// (api/quickmessage/list — same GET + AES-encrypted-query-param shape as
// fetchFriends() above) and merges it into the local quick_messages table.
// "keyword" -> quick message name, "message.title" -> content. Matched by
// name (case-insensitive) against what's already saved locally, same
// "existing wins" rule as importData() in ZaloService_Db.cpp.
void ZaloService::fetchServerQuickMessages()
{
    if (!m_loggedIn) {
        emit serverQuickMessagesReady(0, 0, "Not logged in");
        return;
    }

    QString svcUrl = m_quickMessageServiceUrl;
    if (svcUrl.isEmpty()) svcUrl = m_profileServiceUrl; // fallback, observed same host in practice
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

    qDebug() << "[Zalo] fetchServerQuickMessages GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchServerQuickMessagesDone()));
}

void ZaloService::onFetchServerQuickMessagesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchServerQuickMessages raw (first300):" << raw.left(300);

    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        emit serverQuickMessagesReady(0, 0, "Server is busy (429) — try again in a bit");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        QString em = root["error_message"].toString();
        qDebug() << "[Zalo Error] fetchServerQuickMessages:" << em;
        emit serverQuickMessagesReady(0, 0, em.isEmpty() ? "Request failed" : em);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchServerQuickMessages decrypted (first300):" << dec.left(300);

    // Same double-wrap shape as every other Zalo endpoint here (fetchFriends,
    // fetchInvites, ...): the decrypted blob is itself {error_code, error_message,
    // data:{...}}, and for quickmessage/list the real payload — {cursor, version,
    // items} — lives under that inner "data" key, not at the top level.
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap payload = outer.value("data").toMap();
    QVariantList items = payload.value("items").toList();
    if (items.isEmpty() && outer.contains("items"))
        items = outer.value("items").toList(); // fallback in case the shape ever changes
    qDebug() << "[Zalo] fetchServerQuickMessages found" << items.size() << "items";

    // Existing local names, same dedup approach as importData().
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

    qDebug() << "[Zalo] fetchServerQuickMessages:" << imported << "imported," << skipped << "skipped";
    emit serverQuickMessagesReady(imported, skipped, "");
}

void ZaloService::fetchInvites()
{
    if (!m_loggedIn) return;

    // zca-js: /api/friend/recommendsv2/list — recommItemType=2 = ReceivedFriendRequest
    // (per getFriendRecommendations.js in zca-js source)
    QString base = m_friendServiceUrl.isEmpty() ? m_profileServiceUrl : m_friendServiceUrl;
    QVariantMap innerParams;
    innerParams["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/friend/recommendsv2/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchInvites (recommendsv2/list) GET" << urlStr.left(120);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchInvitesDone()));
}

void ZaloService::onFetchInvitesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchInvites raw (first300):" << raw.left(300);
    QVariantMap root = jsonToMap(raw);

    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo] fetchInvites error_code:" << ec;
        emit invitesReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchInvites decrypted FULL:" << dec.left(800);
    QVariantMap outer = jsonToMap(dec.toUtf8());
    qDebug() << "[Zalo] fetchInvites outer keys:" << outer.keys();

    // recommendsv2/list response (per zca-js getFriendRecommendations.d.ts):
    // { recommItems: [{ recommItemType: 1=PYMK, 2=ReceivedFriendRequest,
    //                   dataInfo: { userId, zaloName, displayName, avatar,
    //                               recommInfo: { message } } }] }
    QVariantList recommItems = outer["recommItems"].toList();
    if (recommItems.isEmpty() && outer.contains("data")) {
        QVariant dataV = outer["data"];
        if (dataV.type() == QVariant::Map)
            recommItems = dataV.toMap()["recommItems"].toList();
        else if (dataV.type() == QVariant::List)
            recommItems = dataV.toList();
    }
    qDebug() << "[Zalo] fetchInvites recommItems count:" << recommItems.size();

    QVariantList invites;
    for (int i = 0; i < recommItems.size(); ++i) {
        QVariantMap item = recommItems[i].toMap();
        QVariantMap info = item["dataInfo"].toMap();

        // zca-js types this two different ways and it's easy to grab the wrong
        // one: the outer "recommItemType" sibling of dataInfo is just `number`
        // (untyped), while the field actually typed as FriendRecommendationsType
        // (1=RecommendedFriend/PYMK, 2=ReceivedFriendRequest) is dataInfo.recommType.
        // Filtering on the outer field was the bug — it doesn't reliably distinguish
        // PYMK from real pending requests; dataInfo.recommType does.
        int itemType = info["recommType"].toInt();

        if (i < 5)
            qDebug() << "[Zalo] fetchInvites item[" << i << "] outer.recommItemType=" << item["recommItemType"].toInt()
                     << "dataInfo.recommType=" << itemType
                     << "dataInfo keys=" << info.keys();

        if (itemType != 2) continue;

        if (info.isEmpty()) continue;

        // userId is the correct field per zca-js type definition
        QString uid = info["userId"].toString();
        if (uid.isEmpty()) uid = info["uid"].toString();
        if (uid.isEmpty()) uid = info["fid"].toString();
        if (uid.isEmpty()) continue;

        // zaloName or displayName
        QString name = info["zaloName"].toString();
        if (name.isEmpty()) name = info["displayName"].toString();
        if (name.isEmpty()) name = info["fullName"].toString();

        // avatar field
        QString avatarUrl = info["avatar"].toString();

        // message from recommInfo.message per type definition
        QString msg;
        QVariant recommInfoV = info["recommInfo"];
        if (recommInfoV.type() == QVariant::Map) {
            QVariantMap recommInfo = recommInfoV.toMap();
            msg = recommInfo["message"].toString();
            if (msg.isEmpty()) msg = recommInfo["customText"].toString();
        }
        // Fallback fields — reject anything that looks like a serialized JSON object
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
    qDebug() << "[Zalo] fetchInvites found" << invites.size() << "pending requests";
    emit invitesReady(invites);
}

// ─── fetchGroupBoard ──────────────────────────────────────────────────────
// Group board = pinned messages + notes + polls shown together in one place
// (see GroupBoardSheet.qml). Ported from zca-js's getListBoard.ts:
// GET {group_board service}/api/board/list?params=AES({group_id, board_type:0,
// page, count, last_id:0, last_type:0, imei}). board_type=0 asks the server
// for every type at once rather than filtering server-side — the 4 tabs in
// the sheet (All/Pinned Message/Note/Poll) are a client-side filter over one
// fetched list, same design zca-js itself documents (BoardType enum: Note=1,
// PinnedMessage=2, Poll=3 — used below to tag each item for the QML filter).
//
// NOTE: zpw_service_map_v3 has BOTH a "group_poll" key and a separate
// "group_board" key — these are two different hosts, not aliases of each
// other. zca-js's getListBoard.ts/createNote.ts/editNote.ts all build their
// serviceURL from zpwServiceMap.group_board specifically; "group_poll" is
// not used by the board endpoints at all (and isn't used by poll vote/
// create/lock either — see votePoll below, which uses the plain "group"
// service same as m_groupServiceUrl). Using m_groupPollServiceUrl here was
// hitting the wrong host and 404ing every board fetch.
void ZaloService::fetchGroupBoard(const QString &groupId, int page, int count)
{
    if (!m_loggedIn || groupId.isEmpty()) return;

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap innerParams;
    innerParams["group_id"]   = groupId;
    innerParams["board_type"] = 0;
    innerParams["page"]       = (page > 0) ? page : 1;
    innerParams["count"]      = (count > 0) ? count : 50;
    innerParams["last_id"]    = 0;
    innerParams["last_type"]  = 0;
    innerParams["imei"]       = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/board/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchGroupBoard GET" << urlStr.left(120) << "groupId:" << groupId;
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchGroupBoardDone()));
}

void ZaloService::onFetchGroupBoardDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchGroupBoard raw (first200):" << raw.left(200);

    // Same false-positive-success risk as onPinGroupMessageDone: an HTML
    // error page (e.g. a 404 from hitting the wrong service host) parses
    // as an empty QVariantMap via jsonToMap(), so error_code silently
    // defaults to 0 unless we check the HTTP status / parse result first.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] fetchGroupBoard HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit groupBoardReady(groupId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] fetchGroupBoard: response did not parse as JSON";
        emit groupBoardReady(groupId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo] fetchGroupBoard error_code:" << ec << root["error_message"].toString();
        emit groupBoardReady(groupId, QVariantList(), root["error_message"].toString().isEmpty()
                              ? QString("Error %1").arg(ec) : root["error_message"].toString());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchGroupBoard decrypted (first400):" << dec.left(400);
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantList rawItems = outer["items"].toList();
    qDebug() << "[Zalo] fetchGroupBoard items count:" << rawItems.size();

    QVariantList items;
    for (int i = 0; i < rawItems.size(); ++i) {
        QVariantMap raw_ = rawItems[i].toMap();
        int boardType = raw_["boardType"].toInt();
        QVariantMap data = raw_["data"].toMap();

        QVariantMap item;
        // BoardType per zca-js: Note=1, PinnedMessage=2, Poll=3
        if (boardType == 1) {
            item["boardType"] = "note";
        } else if (boardType == 2) {
            item["boardType"] = "pin";
        } else if (boardType == 3) {
            item["boardType"] = "poll";
        } else {
            continue; // unknown type — skip rather than showing a broken card
        }

        // Fields common to all 3 detail shapes (Note/PinnedMessage share the
        // exact same envelope per zca-js's NoteDetail/PinnedMessageDetail;
        // Poll has its own distinct shape — see below).
        if (boardType == 1 || boardType == 2) {
            item["id"]         = data["id"].toString();
            item["creatorId"]  = data["creatorId"].toString();
            item["createTime"] = data["createTime"].toLongLong();
            // "params" arrives as a JSON-string-within-JSON on the wire (zca-js
            // itself JSON.parse()s it — see getListBoard.ts's "if boardType !=
            // Poll, params = JSON.parse(params)" step); our jsonToMap() already
            // decodes nested objects from the outer decrypt, but params can
            // still show up as a raw string here if the server sent it
            // double-encoded, so handle both shapes defensively.
            QVariant paramsV = data["params"];
            QVariantMap params_;
            if (paramsV.type() == QVariant::String) {
                params_ = jsonToMap(paramsV.toString().toUtf8());
            } else {
                params_ = paramsV.toMap();
            }
            item["title"] = params_["title"].toString();
            item["extra"] = params_["extra"].toString();
        } else {
            // Poll
            item["id"]              = QString::number(data["poll_id"].toLongLong());
            item["creatorId"]       = data["creator"].toString();
            item["createTime"]      = data["created_time"].toLongLong();
            item["question"]        = data["question"].toString();
            item["closed"]          = data["closed"].toBool();
            item["numVote"]         = data["num_vote"].toInt();
            item["allowMultiChoices"] = data["allow_multi_choices"].toBool();
            QVariantList opts = data["options"].toList();
            QVariantList outOpts;
            for (int j = 0; j < opts.size(); ++j) {
                QVariantMap o = opts[j].toMap();
                QVariantMap oo;
                oo["content"]  = o["content"].toString();
                oo["votes"]    = o["votes"].toInt();
                oo["voted"]    = o["voted"].toBool();
                oo["optionId"] = o["option_id"].toInt();
                outOpts.append(oo);
            }
            item["options"] = outOpts;
        }
        items.append(item);
    }
    emit groupBoardReady(groupId, items, QString());
}

// ─── pinGroupMessage ──────────────────────────────────────────────────────
// Pin a message to the group board. zca-js (the JS reference library this
// app otherwise ports its API calls from) has NO "pin message" endpoint —
// confirmed by reading its full apis/ directory; only board LISTING
// (getListBoard.ts, used by fetchGroupBoard above) and unrelated
// whole-thread conversation pinning exist there. That's why this used to
// be a silent console.log()-only stub on the QML side.
//
// zlapi (github.com/Its-VrxxDev/zlapi — a SEPARATE, independently
// reverse-engineered "Zalo API for Python" library, unrelated to zca-js)
// DOES implement this, as pinGroupMsg(). Verified against zlapi 1.0.3's
// actual source (downloaded from PyPI, zlapi/_client.py) rather than
// guessing: it POSTs to the exact same {group_board}/api/board/topic/
// createv2 endpoint fetchGroupBoard's sibling createNote (zca-js) already
// hits — just with type:2 (PinnedMessage board item) instead of type:0
// (Note), and a JSON-string params.params payload describing the pinned
// message instead of a note title. Ported 1:1 from that:
//   payload.params = { grid, type:2, color:-14540254, emoji:"📌",
//     startTime:-1, duration:-1, repeat:0, src:-1, imei, pinAct:1,
//     params: JSON.stringify({ client_msg_id, global_msg_id, senderUid,
//       senderName, title, msg_type }) }
// Only the "webchat" (text) and "chat.photo" shapes are ported — those are
// the only two message types this client actually creates/pins; zlapi
// supports several more (voice/sticker/link/location/file/gif) this app
// has nothing to pin from. msgType here is Zalo's client message type
// code (1=webchat, 32=chat.photo — zlapi's getClientMessageType()), same
// convention sendMessageQuote()'s qmsgType already uses; QML converts
// from the local 1/2 msgType before calling this, same as it already does
// when building a quote.
void ZaloService::pinGroupMessage(const QString &groupId, const QString &msgId,
                                   const QString &cliMsgId, const QString &senderId,
                                   const QString &senderName, const QString &content,
                                   int msgType)
{
    if (!m_loggedIn || groupId.isEmpty() || msgId.isEmpty()) {
        emit pinMessageDone(false, "Not ready to pin");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap pinParams;
    pinParams["client_msg_id"] = cliMsgId;
    pinParams["global_msg_id"] = msgId;
    pinParams["senderUid"]     = senderId.isEmpty() ? m_uid : senderId;
    pinParams["senderName"]    = senderName;
    pinParams["title"]         = content;
    pinParams["msg_type"]      = (msgType == 32) ? 32 : 1;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 2; // BoardType.PinnedMessage (zca-js's Board.ts enum)
    innerParams["color"]     = -14540254;
    innerParams["emoji"]     = QString::fromUtf8("\xF0\x9F\x93\x8C"); // 📌, matches zlapi
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = -1;
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = 1;
    // Nested JSON-string field, same "flat mapToJson() twice" trick used
    // nowhere else yet in this file — mapToJson() is flat-only (see its
    // comment in ZaloServiceUtils.hpp), so the inner object is serialized
    // to its own JSON string first, then embedded as an ordinary string
    // value in the outer map, matching Python's json.dumps()-as-a-dict-
    // value that zlapi does for this exact same field.
    innerParams["params"]    = QString::fromUtf8(mapToJson(pinParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] pinGroupMessage POST" << urlStr.left(120) << "groupId:" << groupId << "msgId:" << msgId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onPinGroupMessageDone()));
}

void ZaloService::onPinGroupMessageDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] pinGroupMessage raw (first200):" << raw.left(200);

    // A non-2xx HTTP status (e.g. the 404 nginx error page returned while
    // this hit the wrong service host) or a network-level error means the
    // request never reached real API logic at all. jsonToMap() on an HTML
    // error page silently returns an EMPTY QVariantMap (it's not JSON), so
    // root["error_code"].toInt() previously defaulted to 0 — which reads
    // identically to Zalo's own "no error" success code and was reporting
    // pin as successful even on a full 404. Guard against that explicitly
    // rather than trusting error_code on an empty/unparsed body.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] pinGroupMessage HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pinMessageDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] pinGroupMessage: response did not parse as JSON";
        emit pinMessageDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] pinGroupMessage error_code:" << ec << err;
        emit pinMessageDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit pinMessageDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You pinned a message", groupId, true);
}

// ─── createGroupNote ──────────────────────────────────────────────────────
// Ported from zca-js's createNote.ts: same {group_board}/api/board/topic/
// createv2 endpoint pinGroupMessage() uses, type:0 (BoardType.Note) instead
// of type:2 (PinnedMessage), and params.params only carries {title} — no
// client_msg_id/global_msg_id/senderUid, since a note isn't tied to an
// existing chat message the way a pin is.
void ZaloService::createGroupNote(const QString &groupId, const QString &title, bool pinAct)
{
    if (!m_loggedIn || groupId.isEmpty() || title.trimmed().isEmpty()) {
        emit createNoteDone(false, "Not ready to create note");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap noteParams;
    noteParams["title"] = title;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 0; // BoardType.Note
    innerParams["color"]     = -16777216; // matches zca-js's createNote.ts default
    innerParams["emoji"]     = QString();
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = 1; // zca-js uses src:1 for notes (pinGroupMessage's
                                  // zlapi-derived pin uses src:-1 — kept distinct
                                  // to match each reference source exactly)
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = pinAct ? 1 : 0;
    innerParams["params"]    = QString::fromUtf8(mapToJson(noteParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] createGroupNote POST" << urlStr.left(120) << "groupId:" << groupId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupNoteDone()));
}

void ZaloService::onCreateGroupNoteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] createGroupNote raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] createGroupNote HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createNoteDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] createGroupNote: response did not parse as JSON";
        emit createNoteDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] createGroupNote error_code:" << ec << err;
        emit createNoteDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createNoteDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a note", groupId, true);
}

// ─── createGroupPoll ──────────────────────────────────────────────────────
// Ported from zca-js's createPoll.ts. Unlike createGroupNote/pinGroupMessage,
// this hits the PLAIN "group" service (m_groupServiceUrl) — NOT group_board
// and NOT group_poll. See fetchGroupBoard()'s doc comment for why those two
// are easy to mix up here; poll actions (create/vote/lock/detail/option-add/
// share) are consistently on "group" in zca-js, board listing/pin/note are
// consistently on "group_board".
void ZaloService::createGroupPoll(const QString &groupId, const QString &question,
                                   const QStringList &optionsList, bool allowMultiChoices,
                                   bool allowAddNewOption, bool hideVotePreview,
                                   bool isAnonymous)
{
    if (!m_loggedIn || groupId.isEmpty() || question.trimmed().isEmpty() || optionsList.size() < 2) {
        emit createPollDone(false, "Poll needs a question and at least 2 options");
        return;
    }

    QVariantMap params;
    params["group_id"]             = groupId;
    params["question"]             = question;
    QVariantList optsVariant;
    foreach (const QString &opt, optionsList) optsVariant.append(opt);
    params["options"]               = optsVariant;
    params["expired_time"]          = 0; // "Set deadline" UI not wired yet — 0 = no expiration,
                                          // matches zca-js's createPoll.ts default
    params["pinAct"]                = false;
    params["allow_multi_choices"]   = allowMultiChoices;
    params["allow_add_new_option"]  = allowAddNewOption;
    params["is_hide_vote_preview"]  = hideVotePreview;
    params["is_anonymous"]          = isAnonymous;
    params["poll_type"]             = 0;
    params["src"]                   = 1;
    params["imei"]                  = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/create"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] createGroupPoll POST" << urlStr.left(120) << "groupId:" << groupId << "options:" << optionsList.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupPollDone()));
}

void ZaloService::onCreateGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] createGroupPoll raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] createGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createPollDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] createGroupPoll: response did not parse as JSON";
        emit createPollDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] createGroupPoll error_code:" << ec << err;
        emit createPollDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createPollDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a poll", groupId, true);
}

// ─── voteGroupPoll ────────────────────────────────────────────────────────
// Ported from zca-js's votePoll.ts — GET with encrypted params in the query
// string (same GET-with-encrypted-query-params shape fetchGroupBoard already
// uses), hits m_groupServiceUrl same as createGroupPoll above (NOT
// group_board, NOT group_poll — see createGroupPoll's comment).
// optionIds empty = clear the caller's vote (zca-js: "unvote = empty array").
void ZaloService::voteGroupPoll(const QString &groupId, const QString &pollId, const QList<int> &optionIds)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Not ready to vote");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    QVariantList optIdsVariant;
    foreach (int optId, optionIds) optIdsVariant.append(optId);
    params["option_ids"] = optIdsVariant;
    params["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString urlStr = m_groupServiceUrl + "/api/poll/vote"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] voteGroupPoll GET" << urlStr.left(120) << "pollId:" << pollId << "options:" << optionIds.size();
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("pollId", pollId);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onVoteGroupPollDone()));
}

void ZaloService::onVoteGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId  = reply->property("pollId").toString();
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] voteGroupPoll raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] voteGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit votePollDone(false, pollId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] voteGroupPoll: response did not parse as JSON";
        emit votePollDone(false, pollId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] voteGroupPoll error_code:" << ec << err;
        emit votePollDone(false, pollId, QVariantList(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    // zca-js's VotePollResponse: { options: PollOptions[] } — decrypt+parse
    // the same way fetchGroupBoard() does, then map option_id/votes/voted
    // into the same shape QML's poll card already expects from
    // groupBoardReady()'s "options" field, so QML can reuse one rendering
    // path for both the initial fetch and a live vote update.
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] voteGroupPoll decrypted (first300):" << dec.left(300);
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantList rawOpts = outer["options"].toList();

    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toInt();
        outOpts.append(oo);
    }
    emit votePollDone(true, pollId, outOpts, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You voted in a poll", groupId, true);
}

// ─── getPollDetail ────────────────────────────────────────────────────────
// Ported from zca-js's getPollDetail.ts — POST with encrypted params in the
// body (same shape createGroupPoll above uses), hits m_groupServiceUrl same
// as create/vote (NOT group_board, NOT group_poll — see createGroupPoll's
// comment). Only the per-option "voters" uid list is new information here;
// everything else duplicates what groupBoardReady/votePollDone already
// carry, so it's kept in its own map under "options" rather than merged
// into any existing model.
void ZaloService::getPollDetail(const QString &pollId)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit pollDetailReady(pollId, QVariantMap(), "Not ready");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    params["imei"]    = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/detail"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] getPollDetail POST" << urlStr.left(120) << "pollId:" << pollId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("pollId", pollId);
    connect(reply, SIGNAL(finished()), this, SLOT(onGetPollDetailDone()));
}

void ZaloService::onGetPollDetailDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId = reply->property("pollId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] getPollDetail raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] getPollDetail HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pollDetailReady(pollId, QVariantMap(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] getPollDetail: response did not parse as JSON";
        emit pollDetailReady(pollId, QVariantMap(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] getPollDetail error_code:" << ec << err;
        emit pollDetailReady(pollId, QVariantMap(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    // zca-js's PollDetail: { creator, question, options: PollOptions[]
    // (content/votes/voted/voters[]/option_id), joined, closed, poll_id,
    // allow_multi_choices, ..., num_vote } — same decrypt-then-jsonToMap
    // step every other poll/board endpoint here already uses.
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] getPollDetail decrypted (first300):" << dec.left(300);
    QVariantMap outer = jsonToMap(dec.toUtf8());

    QVariantList rawOpts = outer["options"].toList();
    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toInt();
        QVariantList votersList;
        QVariantList rawVoters = o["voters"].toList();
        for (int j = 0; j < rawVoters.size(); ++j) votersList.append(rawVoters[j].toString());
        oo["voters"] = votersList;
        outOpts.append(oo);
    }

    QVariantMap detail;
    detail["question"]           = outer["question"].toString();
    detail["creator"]            = outer["creator"].toString();
    detail["closed"]             = outer["closed"].toBool();
    detail["allowMultiChoices"]  = outer["allow_multi_choices"].toBool();
    detail["numVote"]            = outer["num_vote"].toInt();
    detail["options"]            = outOpts;

    emit pollDetailReady(pollId, detail, QString());
}

// ─── acceptFriendRequest ──────────────────────────────────────────────────
// zca-js: POST /api/friend/accept  body=params=AES({fid, language})
void ZaloService::acceptFriendRequest(const QString &friendId)
{
    if (!m_loggedIn || friendId.isEmpty()) return;

    QVariantMap params;
    params["fid"]      = friendId;
    params["language"] = m_language;
    QString encParams  = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/accept"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] acceptFriendRequest fid=" << friendId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", true);
    connect(reply, SIGNAL(finished()), this, SLOT(onAcceptFriendDone()));
}

// ─── rejectFriendRequest ──────────────────────────────────────────────────
// zca-js: POST /api/friend/reject  body=params=AES({fid})
void ZaloService::rejectFriendRequest(const QString &friendId)
{
    if (!m_loggedIn || friendId.isEmpty()) return;

    QVariantMap params;
    params["fid"] = friendId;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/reject"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] rejectFriendRequest fid=" << friendId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", false);
    connect(reply, SIGNAL(finished()), this, SLOT(onRejectFriendDone()));
}

void ZaloService::onAcceptFriendDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString fid = reply->property("friendId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] acceptFriendRequest response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, true, ok);
}

void ZaloService::onRejectFriendDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString fid = reply->property("friendId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] rejectFriendRequest response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, false, ok);
}

// ─── blockUser ────────────────────────────────────────────────────────────
// zca-js: POST friend[0]/api/friend/block  body=params=AES({fid, imei})
void ZaloService::blockUser(const QString &userId)
{
    if (!m_loggedIn || userId.isEmpty()) return;

    QVariantMap params;
    params["fid"]  = userId;
    params["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/block"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] blockUser uid=" << userId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("userId", userId);
    connect(reply, SIGNAL(finished()), this, SLOT(onBlockUserDone()));
}

void ZaloService::onBlockUserDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid = reply->property("userId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] blockUser response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.insert(uid);
    emit blockUserDone(uid, ok);
}

void ZaloService::unblockUser(const QString &userId)
{
    if (!m_loggedIn || userId.isEmpty()) return;

    QVariantMap params;
    params["fid"]  = userId;
    params["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/unblock"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] unblockUser uid=" << userId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("userId", userId);
    connect(reply, SIGNAL(finished()), this, SLOT(onUnblockUserDone()));
}

void ZaloService::onUnblockUserDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid = reply->property("userId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] unblockUser response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.remove(uid);
    emit unblockUserDone(uid, ok);
}

// ─── setMute ──────────────────────────────────────────────────────────────
// zca-js: POST profile[0]/api/social/profile/setmute
//   body=params=AES({toid, duration, action, startTime, muteType, imei})
//   muteType: 1=DM, 2=Group  action: 1=mute, 3=unmute  duration: -1=forever
void ZaloService::setMute(const QString &threadId, bool isGroup, bool mute)
{
    if (!m_loggedIn || threadId.isEmpty()) return;

    QVariantMap params;
    params["toid"]      = threadId;
    params["duration"]  = -1;
    params["action"]    = mute ? 1 : 3;
    params["startTime"] = static_cast<qint64>(QDateTime::currentMSecsSinceEpoch() / 1000);
    params["muteType"]  = isGroup ? 2 : 1;
    params["imei"]      = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_profileServiceUrl + "/api/social/profile/setmute"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] setMute toid=" << threadId << "mute=" << mute;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("muting", mute);
    connect(reply, SIGNAL(finished()), this, SLOT(onSetMuteDone()));
}

void ZaloService::onSetMuteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid  = reply->property("threadId").toString();
    bool muting  = reply->property("muting").toBool();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] setMute response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) {
        if (muting) m_mutedThreads.insert(tid);
        else        m_mutedThreads.remove(tid);
        saveSession();
    }
    emit muteDone(tid, muting, ok);
}

// ─── clearHistory ─────────────────────────────────────────────────────────
// zca-js deleteChat.ts: POST chat[0]/api/message/deleteconver (DM)
//                    or group[0]/api/group/deleteconver (Group)
//   body=params=AES({toid/grid, cliMsgId, conver:{ownerId,cliMsgId,globalMsgId}, onlyMe:1, imei})
// We use empty conver (no last-message info) which clears from beginning.
void ZaloService::clearHistory(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn || threadId.isEmpty()) return;

    QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());

    QVariantMap conver;
    conver["ownerId"]     = "";
    conver["cliMsgId"]    = "0";
    conver["globalMsgId"] = "0";

    QVariantMap params;
    if (isGroup) {
        params["grid"] = threadId;
    } else {
        params["toid"] = threadId;
    }
    params["cliMsgId"] = ts;
    params["conver"]   = conver;
    params["onlyMe"]   = 1;
    params["imei"]     = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString endpoint  = isGroup ? "/api/group/deleteconver" : "/api/message/deleteconver";
    QString baseUrl   = isGroup ? m_groupServiceUrl : m_chatServiceUrl;
    QString urlStr    = baseUrl + endpoint
                      + "?zpw_ver=" + QString::number(API_VERSION)
                      + "&zpw_type=" + QString::number(API_TYPE)
                      + "&nretry=0";

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] clearHistory toid=" << threadId << "isGroup=" << isGroup;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onClearHistoryDone()));
}

void ZaloService::onClearHistoryDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] clearHistory response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok && m_db && !tid.isEmpty()) {
        // Delete local messages
        const char *sqlDel = "DELETE FROM messages WHERE threadId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sqlDel, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        // Record clear timestamp so future server re-fetches are ignored
        QString clearedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
        const char *sqlClear =
            "INSERT OR REPLACE INTO cleared_threads (threadId, clearedAt) VALUES (?,?)";
        if (sqlite3_prepare_v2(m_db, sqlClear, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, clearedAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        qDebug() << "[Zalo] clearHistory deleted local DB for thread:" << tid;
    }
    emit clearHistoryDone(tid, ok);
}

// ─── leaveGroup ───────────────────────────────────────────────────────────
// zca-js: POST group[0]/api/group/leave  body=params=AES({grids:[groupId], imei, silent:0, language})
void ZaloService::leaveGroup(const QString &groupId)
{
    if (!m_loggedIn || groupId.isEmpty()) return;

    QVariantList grids;
    grids.append(groupId);

    QVariantMap params;
    params["grids"]    = grids;
    params["imei"]     = m_imei;
    params["silent"]   = 0;
    params["language"] = m_language;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/group/leave"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] leaveGroup grid=" << groupId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onLeaveGroupDone()));
}

void ZaloService::onLeaveGroupDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString gid = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] leaveGroup response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit leaveGroupDone(gid, ok);
}

