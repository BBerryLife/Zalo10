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

#include <webp/decode.h>

void ZaloService::fetchConversations(){
    if (!m_loggedIn) return;
    if (m_isFetchingConversations) {
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchConvoTime > 0 && (now - m_lastFetchConvoTime) < FETCH_COOLDOWN_MS) {
        return;
    }
    m_isFetchingConversations = true;

    QVariantMap qp;
    qp["zpw_ver"]  = QString::number(API_VERSION);
    qp["zpw_type"] = QString::number(API_TYPE);

    QString urlStr = buildRawUrl(m_groupServiceUrl + "/api/group/getlg/v4", qp);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchConvoDone()));

}

void ZaloService::onFetchConvoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Contacts] fetchConversations Network Error:" << reply->errorString();
    }

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.isEmpty()) {
        emit conversationsReady(QVariantList());
        m_isFetchingConversations = false;
        return;
    }

    // HTTP 429 (rate limit): the body is HTML, not JSON.
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qWarning() << "[Contacts] fetchConversations: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] fetchConvo error_code:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        // ec=600: zpw_sek expired, session cookies invalid; re-login required.
        if (ec == 600) {
            qWarning() << "[Contacts] fetchConvo: session expired (600), emitting sessionExpired";
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

    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

    QVariantMap gridVerMap = inner["gridVerMap"].toMap();

    QStringList groupIds = gridVerMap.keys();

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

    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupDetailsDone()));
}

void ZaloService::onGroupDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qWarning() << "[Contacts] groupDetails: got 429, backing off";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] groupDetails outer error:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());

    QVariantMap outer = jsonToMap(dec.toUtf8());
    int ec2 = outer["error_code"].toInt();
    if (ec2 != 0) {
        qWarning() << "[Contacts] groupDetails inner error:" << ec2 << outer["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

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
            m_groupNames[gid] = gname;

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
            m_groupNames[gid] = g["name"].toString();

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

    if (!threads.isEmpty())
        emit conversationsReady(threads);
    m_isFetchingConversations = false;
    if (!threads.isEmpty())
        startGroupSync();
}

// Stickers are cached forever by stickerId (the image never changes), so a file-exists check
// is enough. The URL pattern was confirmed from Zalo Web: message content is {id, catId, type}
// with no URL, and id is the eid parameter.
void ZaloService::downloadSticker(const QString &stickerIdStr)
{
    qint64 stickerId = stickerIdStr.toLongLong();
    if (stickerId == 0) return;

    if (m_stickerCache.contains(stickerId)) {
        emit stickerReady(stickerIdStr, m_stickerCache[stickerId]);
        return;
    }

    QString base = QString("/tmp/sticker_%1").arg(stickerId);
    QString cachedPath;
    if (QFile::exists(base + ".png")) cachedPath = base + ".png";
    else if (QFile::exists(base + ".gif")) cachedPath = base + ".gif";

    if (!cachedPath.isEmpty()) {
        QString localPath = "file://" + cachedPath;
        m_stickerCache[stickerId] = localPath;
        emit stickerReady(stickerIdStr, localPath);
        return;
    }

    if (m_pendingStickers.contains(stickerId)) return;
    m_pendingStickers.insert(stickerId);

    // The https->http downgrade for *.zadn.vn is a workaround for an SSL/cert issue in this NDK's
    // OpenSSL build, not a Zalo requirement (same as downloadAvatar()).
    QString url = QString("http://zalo-api.zadn.vn/api/emoticon/sticker/webpc?eid=%1&size=130&version=4")
                      .arg(stickerId);
    QUrl stickerQUrl(url);
    QNetworkRequest stickerReq(stickerQUrl);
    stickerReq.setRawHeader("Referer",    "https://chat.zalo.me/");
    stickerReq.setRawHeader("User-Agent", m_userAgent.toUtf8());
    stickerReq.setRawHeader("Accept",     "image/webp,image/apng,image/*,*/*;q=0.8");
    QNetworkReply *reply = m_manager->get(stickerReq);
    reply->setProperty("stickerId", stickerId);
    connect(reply, SIGNAL(finished()), this, SLOT(onStickerDownloaded()));
}

static bool isPngData(const QByteArray &data)
{
    static const char PNG_MAGIC[8] = {
        (char)0x89, 'P', 'N', 'G', '\r', '\n', (char)0x1a, '\n'
    };
    return data.size() >= 8 && memcmp(data.constData(), PNG_MAGIC, 8) == 0;
}

static bool isGifData(const QByteArray &data)
{
    return data.size() >= 6
        && (data.startsWith("GIF89a") || data.startsWith("GIF87a"));
}

static bool isWebpData(const QByteArray &data)
{
    return data.size() >= 12
        && data.startsWith("RIFF")
        && memcmp(data.constData() + 8, "WEBP", 4) == 0;
}

static bool saveStickerData(const QByteArray &data, const QString &basePathNoExt,
                             QString *outPath)
{
    if (isPngData(data) || isGifData(data)) {
        const QString ext = isPngData(data) ? ".png" : ".gif";
        QString path = basePathNoExt + ext;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        f.write(data);
        f.close();
        *outPath = path;
        return true;
    }

    if (!isWebpData(data)) {
        QByteArray header = data.left(16).toHex();
        qWarning() << "[Contacts] sticker: unknown image format, size=" << data.size();
        return false;
    }

    int width = 0, height = 0;
    uint8_t *rgba = WebPDecodeRGBA(
        reinterpret_cast<const uint8_t*>(data.constData()),
        data.size(), &width, &height);

    if (!rgba || width <= 0 || height <= 0) {
        qWarning() << "[Contacts] sticker: WebP decode failed (animated?), size=" << data.size();
        if (rgba) WebPFree(rgba);
        return false;
    }

    uint8_t *px = rgba;
    const int total = width * height;
    for (int i = 0; i < total; ++i) {
        uint8_t r = px[0];
        px[0] = px[2];
        px[2] = r;
        px += 4;
    }
    QImage img(rgba, width, height, width * 4, QImage::Format_ARGB32);
    QImage owned = img.copy();
    WebPFree(rgba);

    QString path = basePathNoExt + ".png";
    if (!owned.save(path, "PNG")) {
        qWarning() << "[Contacts] saveStickerData: QImage::save PNG failed for" << path;
        return false;
    }
    *outPath = path;
    return true;
}

void ZaloService::onStickerDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    qint64 stickerId = reply->property("stickerId").toLongLong();
    bool hasError     = (reply->error() != QNetworkReply::NoError);
    QByteArray data   = reply->readAll();
    reply->deleteLater();

    m_pendingStickers.remove(stickerId);

    if (hasError || data.isEmpty()) {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qWarning() << "[Contacts] sticker download failed for" << stickerId
                 << "error:" << reply->errorString()
                 << "HTTP:" << httpStatus;
        return;
    }

    QString base = QString("/tmp/sticker_%1").arg(stickerId);
    QString savedPath;
    if (!saveStickerData(data, base, &savedPath)) {
        return;
    }

    QString localPath = "file://" + savedPath;
    m_stickerCache[stickerId] = localPath;

    emit stickerReady(QString::number(stickerId), localPath);
}

void ZaloService::downloadAvatar(const QString &threadId, const QString &url)
{
    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;

    if (m_avatarCache.contains(url)) {
        emit avatarReady(threadId, m_avatarCache[url]);
        return;
    }
    if (m_avatarCache.contains(baseUrl)) {
        emit avatarReady(threadId, m_avatarCache[baseUrl]);
        return;
    }

    // Persistent dedup: compare the new URL hash with the one stored for this threadId (background
    // avatars use "bg_"+threadId, so they don't collide). Survives restart and relogin: an unchanged
    // avatar reuses the file in tmp.
    QString newHash = md5Hex(baseUrl);
    QString storedHash, storedPath;
    if (avatarMetaLookup(threadId, storedHash, storedPath)) {
        QString fsPath = storedPath;
        if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
        if (storedHash == newHash && !fsPath.isEmpty() && QFile::exists(fsPath)) {
            m_avatarCache[url] = storedPath;
            m_avatarCache[baseUrl] = storedPath;
            emit avatarReady(threadId, storedPath);
            return;
        }
    }

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
        qWarning() << "[Contacts] avatar download failed for" << threadId
                 << "error:" << reply->errorString()
                 << "HTTP:" << httpStatus;
        return;
    }

    // File name is per person (md5 of threadId, not of the URL): a changed avatar overwrites the old
    // file instead of leaving orphans when the CDN returns different URLs.
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

    avatarMetaUpsert(threadId, md5Hex(baseUrl), localPath);

    foreach (const QString &wid, waiters)
        emit avatarReady(wid, localPath);
    if (!waiters.contains(threadId))
        emit avatarReady(threadId, localPath);

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
        if (m_loadedFriendAvatarCount >= m_pendingFriendAvatarCount) {
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
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchFriendsTime > 0 && (now - m_lastFetchFriendsTime) < FETCH_COOLDOWN_MS) {
        return;
    }
    m_isFetchingFriends = true;

    // zca-js uses GET with params in the query string, not a POST body.
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
        qWarning() << "[Contacts] fetchFriends: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingFriends = false;
        m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Contacts] fetchFriends:" << root["error_message"].toString();
        m_isFetchingFriends = false;
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

    m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch();

    if (!threads.isEmpty()) {
        // IMPORTANT: compute needDownload and set m_pendingFriends/m_pendingFriendAvatarCount BEFORE
        // emit friendsReady(). The emit is synchronous: the QML handler (~87 downloadAvatar() calls)
        // re-enters on this stack, and with stale bookkeeping it crashed (SIGSEGV inside Qt's QHash)
        // when the avatar cache was empty.
        int needDownload = 0;
        for (int i = 0; i < threads.size(); ++i) {
            QVariantMap t = threads[i].toMap();
            if (t["localAvatar"].toString().isEmpty() && !t["avatar"].toString().isEmpty())
                needDownload++;
        }
        if (needDownload > 0) {
            m_pendingFriends = threads;
            m_pendingFriendAvatarCount = needDownload;
            m_loadedFriendAvatarCount  = 0;
        }
        emit friendsReady(threads);
    }
    m_isFetchingFriends = false;
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

// Group board = pinned message + note + poll in one list (BoardType: Note=1, PinnedMessage=2,
// Poll=3). board_type=0 fetches everything; the sheet's tabs filter client-side.
// zpw_service_map_v3 has separate "group_poll" and "group_board" hosts: boards use group_board
// (using the poll host returned 404 on every fetch); poll actions use the plain group service.
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

    // Same false-success risk as onPinGroupMessageDone(): check the HTTP status before trusting error_code.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] fetchGroupBoard HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit groupBoardReady(groupId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit groupBoardReady(groupId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] fetchGroupBoard error_code:" << ec << root["error_message"].toString();
        emit groupBoardReady(groupId, QVariantList(), root["error_message"].toString().isEmpty()
                              ? QString("Error %1").arg(ec) : root["error_message"].toString());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap boardData = (outer.contains("data") && outer["data"].type() == QVariant::Map)
                             ? outer["data"].toMap() : outer;
    QVariantList rawItems = boardData["items"].toList();

    QVariantList items;
    for (int i = 0; i < rawItems.size(); ++i) {
        QVariantMap raw_ = rawItems[i].toMap();
        int boardType = raw_["boardType"].toInt();
        QVariantMap data = raw_["data"].toMap();

        QVariantMap item;
        if (boardType == 1) {
            item["boardType"] = "note";
        } else if (boardType == 2) {
            item["boardType"] = "pin";
        } else if (boardType == 3) {
            item["boardType"] = "poll";
        } else {
            continue;
        }

        if (boardType == 1 || boardType == 2) {
            item["id"]         = data["id"].toString();
            item["creatorId"]  = data["creatorId"].toString();
            item["createTime"] = data["createTime"].toLongLong();
            QVariant paramsV = data["params"];
            QVariantMap params_;
            if (paramsV.type() == QVariant::String) {
                params_ = jsonToMap(paramsV.toString().toUtf8());
            } else {
                params_ = paramsV.toMap();
            }
            item["title"] = params_["title"].toString();
            item["extra"] = params_["extra"].toString();
            if (boardType == 2) item["msgId"] = params_["global_msg_id"].toString();
        } else {
            item["id"]              = QString::number(data["poll_id"].toLongLong());
            item["creatorId"]       = data["creator"].toString();
            item["createTime"]      = data["created_time"].toLongLong();
            item["question"]        = data["question"].toString();
            item["title"]           = data["question"].toString();
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
                oo["optionId"] = o["option_id"].toLongLong();
                outOpts.append(oo);
            }
            item["options"] = outOpts;
        }
        items.append(item);
    }
    emit groupBoardReady(groupId, items, QString());
}

// Only text ("webchat") and "chat.photo" can be pinned. msgType is Zalo's code (1=webchat,
// 32=chat.photo), converted by QML from the internal 1/2. params.params is a JSON string
// describing the pinned message.
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
    innerParams["type"]      = 2;
    innerParams["color"]     = -14540254;
    innerParams["emoji"]     = QString::fromUtf8("\xF0\x9F\x93\x8C");
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = -1;
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = 1;
    innerParams["params"]    = QString::fromUtf8(mapToJson(pinParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

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

    // Non-2xx HTTP or network errors never reached the API logic: jsonToMap() on an HTML error page
    // returns an empty map, so error_code silently defaulted to 0 and a 404 was reported as
    // success. Guard explicitly.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] pinGroupMessage HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pinMessageDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit pinMessageDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] pinGroupMessage error_code:" << ec << err;
        emit pinMessageDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit pinMessageDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You pinned a message", groupId, true);
}

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
    innerParams["type"]      = 0;
    innerParams["color"]     = -16777216;
    innerParams["emoji"]     = QString();
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = 1;
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

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] createGroupNote HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createNoteDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit createNoteDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] createGroupNote error_code:" << ec << err;
        emit createNoteDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createNoteDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a note", groupId, true);
}

// Polls use the plain "group" service (m_groupServiceUrl), not group_board or group_poll;
// board listing/pin/note use group_board.
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
    params["expired_time"]          = 0;
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

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] createGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createPollDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit createPollDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] createGroupPoll error_code:" << ec << err;
        emit createPollDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createPollDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a poll", groupId, true);
}

// GET with encrypted params in the query string. Empty optionIds clears the vote.
void ZaloService::voteGroupPoll(const QString &groupId, const QString &pollId, const QVariantList &optionIds)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Not ready to vote");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    QVariantList optIdsVariant;
    for (int oi = 0; oi < optionIds.size(); ++oi)
        optIdsVariant.append(optionIds[oi].toLongLong());
    params["option_ids"] = optIdsVariant;
    params["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString urlStr = m_groupServiceUrl + "/api/poll/vote"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

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

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] voteGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit votePollDone(false, pollId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] voteGroupPoll error_code:" << ec << err;
        emit votePollDone(false, pollId, QVariantList(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (!outer.contains("options") && outer["data"].type() == QVariant::Map)
        outer = outer["data"].toMap();
    QVariantList rawOpts = outer["options"].toList();

    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toLongLong();
        outOpts.append(oo);
    }
    emit votePollDone(true, pollId, outOpts, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You voted in a poll", groupId, true);
}

// POST with encrypted params in the body. Only the per-option "voters" are new, so they stay in
// the map under "options" instead of being merged into the existing model.
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

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qWarning() << "[Contacts] getPollDetail HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pollDetailReady(pollId, QVariantMap(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        emit pollDetailReady(pollId, QVariantMap(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qWarning() << "[Contacts] getPollDetail error_code:" << ec << err;
        emit pollDetailReady(pollId, QVariantMap(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (!outer.contains("options") && outer["data"].type() == QVariant::Map)
        outer = outer["data"].toMap();

    QVariantList rawOpts = outer["options"].toList();
    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toLongLong();
        QVariantList votersList;
        QVariantList rawVoters = o["voters"].toList();
        for (int j = 0; j < rawVoters.size(); ++j) votersList.append(rawVoters[j].toString());
        oo["voters"] = votersList;
        outOpts.append(oo);
    }

    QVariantMap detail;
    detail["question"]           = outer["question"].toString();
    detail["creator"]            = outer["creator"].toString();
    detail["createTime"]         = outer["created_time"].toLongLong();
    detail["closed"]             = outer["closed"].toBool();
    detail["allowMultiChoices"]  = outer["allow_multi_choices"].toBool();
    detail["numVote"]            = outer["num_vote"].toInt();
    detail["options"]            = outOpts;

    emit pollDetailReady(pollId, detail, QString());
}

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

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", true);
    connect(reply, SIGNAL(finished()), this, SLOT(onAcceptFriendDone()));
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, false, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.remove(uid);
    emit unblockUserDone(uid, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok && m_db && !tid.isEmpty()) {
        const char *sqlDel = "DELETE FROM messages WHERE threadId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sqlDel, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        QString clearedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
        const char *sqlClear =
            "INSERT OR REPLACE INTO cleared_threads (threadId, clearedAt) VALUES (?,?)";
        if (sqlite3_prepare_v2(m_db, sqlClear, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, clearedAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    emit clearHistoryDone(tid, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit leaveGroupDone(gid, ok);
}

