#include "core/contacts/ContactsCommon.hpp"
#include <QBuffer>
#include <QImageReader>
#include <QTransform>

static QString stripUid0(const QString &s)
{
    return s.endsWith("_0") ? s.left(s.size() - 2) : s;
}

static bool looksLikeProfile(const QVariantMap &m)
{
    return m.contains("userId") || m.contains("displayName") || m.contains("zaloName")
        || m.contains("username") || m.contains("avatar");
}

// The response nests the profile and keys it as "<uid>_0", so search recursively and compare
// uids without the "_0" suffix.
static QVariantMap findProfileMap(const QVariant &v, const QString &uid, int depth)
{
    if (depth > 8) return QVariantMap();
    const QString want = stripUid0(uid);
    if (v.type() == QVariant::Map) {
        QVariantMap m = v.toMap();
        if (m.contains("userId") && stripUid0(m["userId"].toString()) == want) return m;
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
            if (stripUid0(it.key()) == want && it.value().type() == QVariant::Map) {
                QVariantMap c = it.value().toMap();
                if (looksLikeProfile(c)) return c;
            }
        }
        for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
            QVariantMap r = findProfileMap(it.value(), uid, depth + 1);
            if (!r.isEmpty()) return r;
        }
    } else if (v.type() == QVariant::String) {
        QString str = v.toString().trimmed();
        if (str.startsWith("{") && str.size() > 2) {
            QVariantMap inner = jsonToMap(str.toUtf8());
            if (!inner.isEmpty()) return findProfileMap(inner, uid, depth + 1);
        }
    } else if (v.type() == QVariant::List) {
        QVariantList l = v.toList();
        for (int i = 0; i < l.size(); ++i) {
            QVariantMap r = findProfileMap(l[i], uid, depth + 1);
            if (!r.isEmpty()) return r;
        }
    }
    return QVariantMap();
}

QNetworkReply *ZaloService::sendProfileApi(const QString &path, bool post, QVariantMap params)
{
    params["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(params)));
    QString urlStr = m_profileServiceUrl + path
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);
    if (post) {
        QByteArray body = "params=" + QUrl::toPercentEncoding(encParams);
        QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        return m_manager->post(req, body);
    }
    urlStr = urlStr + "&params=" + QUrl::toPercentEncoding(encParams);
    return m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
}

void ZaloService::fetchProfileDetails(const QString &uid)
{
    if (!m_loggedIn || uid.isEmpty()) return;

    QVariantList verMap;
    verMap.append(uid.endsWith("_0") ? uid : uid + "_0");
    QVariantMap p;
    p["phonebook_version"]   = 0;
    p["friend_pversion_map"] = verMap;
    p["avatar_size"]         = 120;
    p["language"]            = "vi";
    p["show_online_status"]  = 1;

    QNetworkReply *reply = sendProfileApi("/api/social/friend/getprofiles/v2", true, p);
    reply->setProperty("profileUid", uid);
    connect(reply, SIGNAL(finished()), this, SLOT(onProfileDetailsDone()));
}

void ZaloService::onProfileDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid    = reply->property("profileUid").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Contacts] fetchProfileDetails:" << root["error_code"].toInt()
                   << root["error_message"].toString();
        return;
    }
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap profile = findProfileMap(jsonToMap(dec.toUtf8()), uid, 0);
    if (profile.isEmpty()) {
        qWarning() << "[Contacts] fetchProfileDetails: profile not found";
        return;
    }
    emit profileDetailsReady(uid, profile);
    {
        // Chats are built from stored messages now (no friend list), so a person who is not a friend
        // gets her name from here: keep it so the next start does not ask again.
        QString pn = profile["zaloName"].toString();
        if (pn.isEmpty()) pn = profile["displayName"].toString();
        if (!pn.isEmpty()) {
            if (!m_memberDbLoaded) loadMemberNamesFromDb();
            rememberMemberName(stripUid0(uid), pn, false);
        }
    }
    {
        // Feeds: a profile we just read anyway is compared with the snapshot for free.
        QMap<QString, QVariantMap> one;
        one.insert(stripUid0(uid), profile);
        feedsApplyProfiles(QStringList() << stripUid0(uid), one, false);
    }
}

// Own profile (avatar link + display name). Same endpoint zca-js uses in fetchAccountInfo():
// GET <profile service>/api/social/profile/me-v2, no parameters; the answer is encrypted like the others.
void ZaloService::fetchSelfProfile()
{
    if (!m_loggedIn || m_profileServiceUrl.isEmpty()) return;
    QString urlStr = m_profileServiceUrl + "/api/social/profile/me-v2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onSelfProfileDone()));
}

void ZaloService::onSelfProfileDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Contacts] fetchSelfProfile:" << root["error_code"].toInt();
        // Fallback: the generic profile lookup also works for our own uid.
        fetchProfileDetails(m_uid);
        return;
    }
    QVariantMap outer = jsonToMap(aesDecryptBase64(m_secretKey, root["data"].toString()).toUtf8());
    QVariantMap profile = outer["data"].toMap()["profile"].toMap();
    if (profile.isEmpty()) profile = outer["profile"].toMap();
    if (profile.isEmpty()) profile = findProfileMap(outer, m_uid, 0);
    if (!profile.isEmpty()) emit selfProfileDetails(profile);
    QString avatar = profile["avatar"].toString();
    if (avatar.isEmpty()) {
        qWarning() << "[Contacts] fetchSelfProfile: no avatar in response";
        fetchProfileDetails(m_uid);
        return;
    }
    QString name = profile["displayName"].toString();
    if (name.isEmpty()) name = profile["zaloName"].toString();
    emit selfProfileReady(avatar, name.isEmpty() ? m_displayName : name);
}

// ---- Own profile picture (zca-js changeAccountAvatar) ----

static quint32 readUInt(const uchar *p, int bytes, bool littleEndian)
{
    quint32 v = 0;
    for (int k = 0; k < bytes; ++k)
        v |= (quint32)p[littleEndian ? k : bytes - 1 - k] << (8 * k);
    return v;
}

// EXIF orientation (1..8) of a JPEG; 1 when there is none. Qt4 does not apply it by itself.
static int jpegOrientation(const QByteArray &d)
{
    const uchar *p = reinterpret_cast<const uchar*>(d.constData());
    const int n = d.size();
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 1;
    int i = 2;
    while (i + 4 <= n) {
        if (p[i] != 0xFF) return 1;
        const uchar marker = p[i + 1];
        if (marker == 0xDA || marker == 0xD9) return 1;   // image data / end: no EXIF before it
        const int len = (p[i + 2] << 8) | p[i + 3];
        if (len < 2) return 1;
        if (marker == 0xE1 && len >= 16 && i + 10 <= n && memcmp(p + i + 4, "Exif\0\0", 6) == 0) {
            const uchar *t = p + i + 10;                  // TIFF header
            const int tl = qMin(len - 8, n - (i + 10));
            if (tl < 8) return 1;
            const bool le = (t[0] == 'I');
            const quint32 ifd = readUInt(t + 4, 4, le);
            if (ifd + 2 > (quint32)tl) return 1;
            const int count = (int)readUInt(t + ifd, 2, le);
            for (int k = 0; k < count; ++k) {
                const quint32 e = ifd + 2 + 12 * k;
                if (e + 12 > (quint32)tl) return 1;
                if (readUInt(t + e, 2, le) == 0x0112) {
                    int o = (int)readUInt(t + e + 8, 2, le);
                    return (o >= 1 && o <= 8) ? o : 1;
                }
            }
            return 1;
        }
        i += 2 + len;
    }
    return 1;
}

// What upavatar gets: a square JPEG, upright, at most 1080 px.
static bool prepareAvatarJpeg(const QString &path, QByteArray &out, int &side)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray raw = f.readAll();
    f.close();
    if (raw.isEmpty()) return false;

    const int orient = jpegOrientation(raw);
    QBuffer inBuf(&raw);
    inBuf.open(QIODevice::ReadOnly);
    QImageReader reader(&inBuf);
    const QSize sz = reader.size();
    // A camera photo is 8 MP or more: decode it smaller instead of holding the whole bitmap.
    if (sz.isValid() && (sz.width() > 2000 || sz.height() > 2000)) {
        // Qt4 has no QSize::scaled(): fit the long side to 1600 by hand.
        const int longSide = qMax(sz.width(), sz.height());
        const int w = qMax(1, (int)((qint64)sz.width()  * 1600 / longSide));
        const int h = qMax(1, (int)((qint64)sz.height() * 1600 / longSide));
        reader.setScaledSize(QSize(w, h));
    }
    QImage img = reader.read();
    if (img.isNull()) return false;

    switch (orient) {
    case 2: img = img.mirrored(true, false); break;
    case 3: img = img.transformed(QTransform().rotate(180)); break;
    case 4: img = img.mirrored(false, true); break;
    case 5: img = img.mirrored(true, false).transformed(QTransform().rotate(270)); break;
    case 6: img = img.transformed(QTransform().rotate(90)); break;
    case 7: img = img.mirrored(true, false).transformed(QTransform().rotate(90)); break;
    case 8: img = img.transformed(QTransform().rotate(270)); break;
    default: break;
    }

    int sq = qMin(img.width(), img.height());
    if (sq <= 0) return false;
    img = img.copy((img.width() - sq) / 2, (img.height() - sq) / 2, sq, sq);
    if (sq > 1080) { img = img.scaled(1080, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); sq = 1080; }

    out.clear();
    QBuffer outBuf(&out);
    outBuf.open(QIODevice::WriteOnly);
    if (!img.save(&outBuf, "JPEG", 90)) return false;
    side = sq;
    return true;
}

void ZaloService::changeSelfAvatar(const QString &localFilePath)
{
    if (!m_loggedIn) { emit selfAvatarChanged(false, "Not logged in"); return; }
    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    if (path.isEmpty() || !QFile::exists(path)) { emit selfAvatarChanged(false, "Photo not found"); return; }

    QByteArray jpeg;
    int side = 0;
    if (!prepareAvatarJpeg(path, jpeg, side)) {
        // Could not decode / re-encode here: send the file as it is.
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { emit selfAvatarChanged(false, "Cannot read the photo"); return; }
        jpeg = f.readAll();
        f.close();
        side = 1080;
    }

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }

    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    QVariantMap origin;    origin["width"] = side; origin["height"] = side;
    QVariantMap processed; processed["width"] = side; processed["height"] = side; processed["size"] = (int)jpeg.size();
    QVariantMap meta;      meta["origin"] = origin; meta["processed"] = processed;

    QVariantMap p;
    p["avatarSize"] = 120;
    // zca-js: uid + "HH:mm dd/MM/yyyy"
    p["clientId"]   = m_uid + QDateTime::currentDateTime().toString("HH:mm dd/MM/yyyy");
    p["language"]   = "vi";
    // mapToJson() is flat (a nested map would become ""), so metaData goes through the recursive writer.
    p["metaData"]   = QString::fromUtf8(variantToJsonCompact(QVariant(meta)));
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    QString urlStr = fileBase + "/api/profile/upavatar"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QString boundary = "----ZaloBoundary" + QString::number(ts);
    QByteArray body;
    body += (QString("--") + boundary + "\r\n").toUtf8();
    body += QByteArray("Content-Disposition: form-data; name=\"fileContent\"; filename=\"blob\"\r\n");
    body += QByteArray("Content-Type: image/jpeg\r\n\r\n");
    body += jpeg;
    body += QByteArray("\r\n");
    body += (QString("--") + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onChangeAvatarDone()));
}

void ZaloService::onChangeAvatarDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool netOk = (reply->error() == QNetworkReply::NoError);
    QString netErr = reply->errorString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    if (!netOk) { emit selfAvatarChanged(false, netErr); return; }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        QString msg = root["error_message"].toString();
        qWarning() << "[Contacts] changeSelfAvatar:" << root["error_code"].toInt() << msg;
        emit selfAvatarChanged(false, msg.isEmpty() ? QString("Zalo refused the picture") : msg);
        return;
    }
    // The inner envelope can carry its own error code.
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    if (!dec.isEmpty()) {
        QVariantMap inner = jsonToMap(dec.toUtf8());
        if (inner.contains("error_code") && inner["error_code"].toInt() != 0) {
            QString msg = inner["error_message"].toString();
            qWarning() << "[Contacts] changeSelfAvatar inner:" << inner["error_code"].toInt() << msg;
            emit selfAvatarChanged(false, msg.isEmpty() ? QString("Zalo refused the picture") : msg);
            return;
        }
    }
    emit selfAvatarChanged(true, QString());
}

// Group picture (zca-js changeGroupAvatar: file service /api/group/upavatar). Same squared JPEG as the
// own avatar; the group id and a "HH:mm dd/MM/yyyy" stamp make up the clientId. Result: changeGroupAvatarDone().
void ZaloService::changeGroupAvatar(const QString &groupId, const QString &localFilePath)
{
    if (!m_loggedIn) { emit changeGroupAvatarDone(groupId, false, "Not logged in"); return; }
    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    if (groupId.isEmpty() || path.isEmpty() || !QFile::exists(path)) {
        emit changeGroupAvatarDone(groupId, false, "Photo not found");
        return;
    }

    QByteArray jpeg;
    int side = 0;
    if (!prepareAvatarJpeg(path, jpeg, side)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { emit changeGroupAvatarDone(groupId, false, "Cannot read the photo"); return; }
        jpeg = f.readAll();
        f.close();
        side = 1080;
    }

    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }

    QVariantMap p;
    p["grid"]         = groupId;
    p["avatarSize"]   = 120;
    p["clientId"]     = "g" + groupId + QDateTime::currentDateTime().toString("HH:mm dd/MM/yyyy");
    p["imei"]         = m_imei;
    p["originWidth"]  = side;
    p["originHeight"] = side;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(p)));

    QString urlStr = fileBase + "/api/group/upavatar"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QString boundary = "----ZaloBoundary" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QByteArray body;
    body += (QString("--") + boundary + "\r\n").toUtf8();
    body += QByteArray("Content-Disposition: form-data; name=\"fileContent\"; filename=\"blob\"\r\n");
    body += QByteArray("Content-Type: image/jpeg\r\n\r\n");
    body += jpeg;
    body += QByteArray("\r\n");
    body += (QString("--") + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("op", "gavatar");
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupManageDone()));
}

// Own bio (zca-js updateProfileBio: profile service /api/social/profile/status). Like zca-js the
// encrypted params go both in the query and in the body. Result: selfBioChanged().
void ZaloService::updateProfileBio(const QString &status)
{
    if (!m_loggedIn || m_profileServiceUrl.isEmpty()) { emit selfBioChanged(false, "Not logged in"); return; }

    QVariantMap p;
    p["status"] = status;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(p)));
    QByteArray enc = QUrl::toPercentEncoding(encParams);

    QString urlStr = m_profileServiceUrl + "/api/social/profile/status"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QString::fromLatin1(enc.constData(), enc.size());
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, "params=" + enc);
    reply->setProperty("op", "bio");
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupManageDone()));
}

void ZaloService::fetchBizAccount(const QString &uid)
{
    if (!m_loggedIn || uid.isEmpty()) return;
    QVariantMap p;
    p["fid"] = uid;
    QNetworkReply *reply = sendProfileApi("/api/social/friend/get-bizacc", true, p);
    reply->setProperty("profileUid", uid);
    connect(reply, SIGNAL(finished()), this, SLOT(onBizAccountDone()));
}

void ZaloService::onBizAccountDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid    = reply->property("profileUid").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;
    QVariantMap outer = jsonToMap(aesDecryptBase64(m_secretKey, root["data"].toString()).toUtf8());
    if (outer["error_code"].toInt() != 0) {
        qWarning() << "[Contacts] get-bizacc:" << outer["error_code"].toInt();
        return;
    }
    QVariantMap biz = outer["data"].toMap()["biz"].toMap();
    if (biz.isEmpty()) return;
    emit bizAccountReady(uid, biz);
}

void ZaloService::fetchFullAvatar(const QString &uid)
{
    if (!m_loggedIn || uid.isEmpty()) return;
    QVariantMap p;
    p["fid"] = uid; p["page"] = 1; p["count"] = 20;
    QNetworkReply *reply = sendProfileApi("/api/social/profile/avatar", false, p);
    reply->setProperty("profileUid", uid);
    connect(reply, SIGNAL(finished()), this, SLOT(onFullAvatarDone()));
}

void ZaloService::onFullAvatarDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid    = reply->property("profileUid").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;
    QVariantMap outer = jsonToMap(aesDecryptBase64(m_secretKey, root["data"].toString()).toUtf8());
    if (outer["error_code"].toInt() != 0) return;
    // bk_full_avatar is a .jpg; full_avatar is .jxl, which BB10 can't display.
    QString url = outer["data"].toMap()["bk_full_avatar"].toString();
    if (!url.isEmpty()) emit fullAvatarReady(uid, url);
}
