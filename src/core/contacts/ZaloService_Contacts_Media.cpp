#include "core/contacts/ContactsCommon.hpp"

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
