#include "core/contacts/ContactsCommon.hpp"

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
