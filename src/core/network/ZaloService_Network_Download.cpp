#include "core/network/NetworkCommon.hpp"

// Idempotent: the server may replay chat.undo (reopened thread, resync), so content is copied
// into recalledOriginalContent only the first time (while still empty). The original text is
// kept regardless of the "Show Recalled Messages" setting.

void ZaloService::downloadUpdate(const QString &url, const QString &filename)
{
    if (m_updateReply) {
        m_updateReply->abort();
        m_updateReply->deleteLater();
        m_updateReply = 0;
    }

    QString dest = QString("/accounts/1000/shared/downloads/") + filename;
    m_updateDestPath = dest;

    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("User-Agent", m_userAgent.toUtf8());

    // The update host is on the same CDN as the version manifest and needs TLS 1.2+: force
    // AnyProtocol (same fix as buildManifestRequest() / AboutSheet.qml).
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConf.setProtocol(QSsl::AnyProtocol);
    req.setSslConfiguration(sslConf);

    m_updateReply = m_manager->get(req);

    connect(m_updateReply, SIGNAL(sslErrors(const QList<QSslError>&)),
            this, SLOT(onUpdateSslErrors(const QList<QSslError>&)));
    connect(m_updateReply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(onUpdateDownloadProgress(qint64,qint64)));
    connect(m_updateReply, SIGNAL(finished()),
            this, SLOT(onUpdateDownloadFinished()));
}

void ZaloService::cancelUpdateDownload()
{
    if (!m_updateReply) return;
    m_updateReply->abort();
}

static QString photoExtensionFor(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".png"))  return ".png";
    if (lower.endsWith(".gif"))  return ".gif";
    if (lower.endsWith(".webp")) return ".webp";
    return ".jpg";
}

QString ZaloService::downloadPhotoToGallery(const QString &localImagePath, const QString &msgId)
{
    QString src = localImagePath;
    if (src.startsWith("file://")) src = src.mid(7);

    if (src.isEmpty() || !QFile::exists(src)) {
        return QString();
    }

    QString destDir = "/accounts/1000/shared/downloads/zalo10/photos";
    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qWarning() << "[Net] downloadPhotoToGallery: mkpath failed" << destDir;
        return QString();
    }

    QString ext  = photoExtensionFor(src);
    QString name = msgId.isEmpty()
                 ? (QString("Zalo10_%1%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(ext))
                 : (QString("Zalo10_%1%2").arg(msgId).arg(ext));
    QString destPath = destDir + "/" + name;

    if (QFile::exists(destPath))
        QFile::remove(destPath);

    if (!QFile::copy(src, destPath)) {
        qWarning() << "[Net] downloadPhotoToGallery: copy failed" << src << "->" << destPath;
        return QString();
    }

    return destPath;
}

// Downloads keep the ORIGINAL file name (Media Player shows it), unlike photos
// (Zalo10_<msgId>). Idempotent: an existing file emits videoDownloadFinished() immediately.
static QString videoExtensionFor(const QString &fileName)
{
    QString ext = fileName.section('.', -1).toLower();
    if (ext.isEmpty() || ext.length() > 4) ext = "mp4";
    return "." + ext;
}

// Strip characters invalid on the BB10 filesystem: the server's fileName isn't sanitized and
// '/' would become a subdirectory.
static QString sanitizeFileName(const QString &name)
{
    QString s = name;
    s.replace(QRegExp("[/\\\\:*?\"<>|]"), "_");
    s = s.trimmed();
    return s;
}

// Unknown extensions fall back to "documents" (most files sent in chat are documents).
static QString destDirFor(const QString &fileName)
{
    QString ext = fileName.section('.', -1).toLower();
    static const char* kVideoExt[] = { "mp4", "mov", "3gp", "mkv", 0 };
    static const char* kMusicExt[] = { "mp3", "flac", 0 };
    static const char* kVoiceExt[] = { "m4a", 0 };
    static const char* kMiscExt[]  = { "apk", "cer", "zip", "rar", "7z", "vcf", "bar", 0 };
    for (int i = 0; kVideoExt[i]; ++i) if (ext == kVideoExt[i]) return "/accounts/1000/shared/downloads/zalo10/videos";
    for (int i = 0; kMusicExt[i]; ++i) if (ext == kMusicExt[i]) return "/accounts/1000/shared/downloads/zalo10/music";
    for (int i = 0; kVoiceExt[i]; ++i) if (ext == kVoiceExt[i]) return "/accounts/1000/shared/downloads/zalo10/voice";
    for (int i = 0; kMiscExt[i]; ++i)  if (ext == kMiscExt[i])  return "/accounts/1000/shared/downloads/zalo10/misc";
    return "/accounts/1000/shared/downloads/zalo10/documents";
}

void ZaloService::downloadVideoMessage(const QString &msgId, const QString &url, const QString &fileName)
{
    if (url.isEmpty() || msgId.isEmpty()) {
        emit videoDownloadFailed(msgId, "Missing URL");
        return;
    }

    QString destDir = destDirFor(fileName.isEmpty() ? url : fileName);
    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qWarning() << "[Net] downloadVideoMessage: mkpath failed" << destDir;
        emit videoDownloadFailed(msgId, "Cannot create " + destDir);
        return;
    }

    QString ext = videoExtensionFor(fileName.isEmpty() ? url : fileName);
    QString cleanName = sanitizeFileName(fileName);
    QString destPath;
    if (cleanName.isEmpty()) {
        destPath = destDir + "/Zalo10_" + msgId + ext;
    } else {
        destPath = destDir + "/" + cleanName;
    }

    if (QFile::exists(destPath)) {
        emit videoDownloadFinished(msgId, destPath);
        return;
    }

    if (m_videoDownloadReply) {
        m_videoDownloadReply->abort();
        m_videoDownloadReply->deleteLater();
        m_videoDownloadReply = 0;
    }

    m_videoDownloadMsgId   = msgId;
    m_videoDownloadDestPath = destPath;

    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    m_videoDownloadReply = m_manager->get(req);
    connect(m_videoDownloadReply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(onVideoDownloadProgress(qint64,qint64)));
    connect(m_videoDownloadReply, SIGNAL(finished()),
            this, SLOT(onVideoDownloadFinished()));

}

void ZaloService::onVideoDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) return;
    int pct = (int)((received * 100) / total);
    emit videoDownloadProgress(m_videoDownloadMsgId, pct);
}

void ZaloService::onVideoDownloadFinished()
{
    if (!m_videoDownloadReply) return;
    QString msgId   = m_videoDownloadMsgId;
    QString dest    = m_videoDownloadDestPath;

    if (m_videoDownloadReply->error() != QNetworkReply::NoError) {
        QString err = m_videoDownloadReply->errorString();
        m_videoDownloadReply->deleteLater();
        m_videoDownloadReply = 0;
        qWarning() << "[Net] downloadVideoMessage: failed" << err;
        emit videoDownloadFailed(msgId, err);
        return;
    }

    QByteArray data = m_videoDownloadReply->readAll();
    m_videoDownloadReply->deleteLater();
    m_videoDownloadReply = 0;

    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly)) {
        emit videoDownloadFailed(msgId, "Cannot write to " + dest);
        return;
    }
    f.write(data);
    f.close();

    emit videoDownloadProgress(msgId, 100);
    emit videoDownloadFinished(msgId, dest);
}

void ZaloService::onUpdateSslErrors(const QList<QSslError> &errors)
{
    for (int i = 0; i < errors.size(); ++i)
        qWarning() << "[Net] update download SSL error:" << errors.at(i).errorString();
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->ignoreSslErrors();
}

void ZaloService::onUpdateDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit updateDownloadProgress(0);
        return;
    }
    int pct = (int)((received * 100) / total);
    emit updateDownloadProgress(pct);
}

void ZaloService::onUpdateDownloadFinished()
{
    if (!m_updateReply) return;

    if (m_updateReply->error() != QNetworkReply::NoError) {
        QString err = m_updateReply->errorString();
        m_updateReply->deleteLater();
        m_updateReply = 0;
        emit updateDownloadFailed(err);
        return;
    }

    QByteArray data = m_updateReply->readAll();
    m_updateReply->deleteLater();
    m_updateReply = 0;

    QFile f(m_updateDestPath);
    if (!f.open(QIODevice::WriteOnly)) {
        emit updateDownloadFailed("Cannot write to " + m_updateDestPath);
        return;
    }
    f.write(data);
    f.close();

    emit updateDownloadProgress(100);
    emit updateDownloadFinished(m_updateDestPath);
}
