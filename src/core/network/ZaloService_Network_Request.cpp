#include "core/network/NetworkCommon.hpp"

QString ZaloService::buildRawUrl(const QString &base, const QVariantMap &params)
{
    QString safeBase = base.trimmed().isEmpty() ? "https://wpa.chat.zalo.me" : base;

    QStringList qp;
    for (QVariantMap::const_iterator it = params.constBegin(); it != params.constEnd(); ++it) {
        QString k = QString::fromUtf8(QUrl::toPercentEncoding(it.key()));
        QString v = QString::fromUtf8(QUrl::toPercentEncoding(it.value().toString()));
        qp << (k + "=" + v);
    }
    if (qp.isEmpty()) return safeBase;
    return safeBase + "?" + qp.join("&");
}

QNetworkRequest ZaloService::buildRequest(const QString &urlStr, const QString &referer, bool jsonAccept)
{
    QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
    QNetworkRequest req(url);

    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    req.setRawHeader("User-Agent", m_userAgent.toUtf8());
    req.setRawHeader("Accept", jsonAccept ? "application/json, text/plain, */*" : "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");

    if (!referer.isEmpty()) {
        req.setRawHeader("Referer", referer.toUtf8());
        QString origin = referer;
        int slashCount = 0;
        int pos = 0;
        for(int i=0; i<referer.length(); i++) {
            if(referer[i] == '/') slashCount++;
            if(slashCount == 3) { pos = i; break; }
        }
        if(pos > 0) origin = referer.left(pos);
        req.setRawHeader("Origin", origin.toUtf8());
    }

    req.setRawHeader("Connection", "keep-alive");
    req.setRawHeader("sec-fetch-dest", "empty");
    req.setRawHeader("sec-fetch-mode", "cors");
    req.setRawHeader("sec-fetch-site", "same-origin");

    QString ck = buildCookieHeader();
    if (!ck.isEmpty()) {
        req.setRawHeader("Cookie", ck.toUtf8());
    }

    return req;
}

QString ZaloService::buildCookieHeader() const
{
    QStringList p;
    for (QMap<QString,QString>::const_iterator it = m_cookies.constBegin(); it != m_cookies.constEnd(); ++it)
        p << it.key() + "=" + it.value();
    return p.join("; ");
}

void ZaloService::parseCookiesFromReply(QNetworkReply *reply)
{
    typedef QPair<QByteArray, QByteArray> HeaderPair;
    foreach (const HeaderPair &hp, reply->rawHeaderPairs()) {
        if (hp.first.toLower() == "set-cookie") {
          foreach (const QString &line, QString::fromUtf8(hp.second).split('\n', QString::SkipEmptyParts)) {
            QString kv   = line.split(";").first().trimmed();
            int eq = kv.indexOf('=');
            if (eq > 0) {
                QString name = kv.left(eq).trimmed();
                QString val  = kv.mid(eq + 1).trimmed();
                // Servers delete cookies via Set-Cookie with an empty/"deleted" value or Max-Age<=0. Never
                // overwrite a good cookie with it, or saveSession() stores a broken cookie and forces a re-login.
                QString low = line.toLower();
                bool isDelete = val.isEmpty() || val.toLower() == "deleted"
                             || low.contains("max-age=0") || low.contains("max-age=-");
                if (isDelete && m_cookies.contains(name)) {
                    continue;
                }
                if (isDelete) continue;
                m_cookies[name] = val;
            }
          }
        }
    }
}

void ZaloService::saveSession()
{
    QSettings s("BerryLife", "Zalo10");
    s.setValue("uid",        m_uid);
    s.setValue("secretKey",  m_secretKey);
    s.setValue("imei",       m_imei);
    s.setValue("userAgent",  m_userAgent);
    s.setValue("chatUrl",    m_chatServiceUrl);
    s.setValue("groupUrl",   m_groupServiceUrl);
    s.setValue("profileUrl", m_profileServiceUrl);
    s.setValue("grpPollUrl", m_groupPollServiceUrl);
    s.setValue("grpBoardUrl", m_groupBoardServiceUrl);
    s.setValue("friendUrl",  m_friendServiceUrl);
    s.setValue("fileUrl",    m_fileServiceUrl);
    s.setValue("quickMsgUrl", m_quickMessageServiceUrl);
    s.setValue("conversationUrl", m_conversationServiceUrl);
    s.setValue("zpwWsUrls",  m_zpwWsUrls);
    s.setValue("mutedThreads", QStringList(m_mutedThreads.toList()));

    QVariantMap cookieMap;
    QMapIterator<QString, QString> it(m_cookies);
    while (it.hasNext()) {
        it.next();
        cookieMap[it.key()] = it.value();
    }
    QString cookieJson = mapToJson(cookieMap);
    s.setValue("cookies", cookieJson);

    ZaloCookieJar *jar = static_cast<ZaloCookieJar*>(m_manager->cookieJar());
    QStringList jarList;
    if (jar) {
        QDateTime now = QDateTime::currentDateTime();
        foreach (const QNetworkCookie &c, jar->exportAll()) {
            if (!c.isSessionCookie() && c.expirationDate() < now) continue;
            jarList << QString::fromUtf8(c.toRawForm(QNetworkCookie::Full));
        }
    }
    s.setValue("jarCookies", jarList);
    s.sync();
    qDebug() << "[Net] session saved";
}

bool ZaloService::loadSession()
{
    QSettings s("BerryLife", "Zalo10");
    QString uid = s.value("uid").toString();
    if (uid.isEmpty()) {
        qDebug() << "[Net] loadSession: no saved session";
        return false;
    }

    m_uid              = uid;
    m_secretKey        = s.value("secretKey").toString();
    m_imei             = s.value("imei").toString();
    m_userAgent        = s.value("userAgent", QString::fromLatin1(USER_AGENT)).toString();
    m_chatServiceUrl      = s.value("chatUrl").toString();
    m_groupServiceUrl     = s.value("groupUrl").toString();
    m_profileServiceUrl   = s.value("profileUrl").toString();
    m_groupPollServiceUrl = s.value("grpPollUrl").toString();
    m_groupBoardServiceUrl = s.value("grpBoardUrl").toString();
    m_friendServiceUrl    = s.value("friendUrl").toString();
    m_fileServiceUrl      = s.value("fileUrl").toString();
    m_quickMessageServiceUrl = s.value("quickMsgUrl").toString();
    m_conversationServiceUrl = s.value("conversationUrl").toString();
    m_zpwWsUrls           = s.value("zpwWsUrls").toStringList();
    m_mutedThreads        = QSet<QString>::fromList(s.value("mutedThreads").toStringList());

    QString cookieJson = s.value("cookies").toString();
    if (!cookieJson.isEmpty()) {
        QVariantMap cookieMap = jsonToMap(cookieJson.toUtf8());
        QMapIterator<QString, QVariant> it(cookieMap);
        while (it.hasNext()) {
            it.next();
            m_cookies[it.key()] = it.value().toString();
        }
    }

    {
        ZaloCookieJar *jar = static_cast<ZaloCookieJar*>(m_manager->cookieJar());
        QList<QNetworkCookie> restored;
        foreach (const QString &raw, s.value("jarCookies").toStringList())
            restored += QNetworkCookie::parseCookies(raw.toUtf8());
        if (jar && !restored.isEmpty()) jar->importAll(restored);
    }

    if (m_uid.isEmpty() || m_secretKey.isEmpty() || m_chatServiceUrl.isEmpty()) {
        qDebug() << "[Net] loadSession: incomplete session data";
        return false;
    }

    qDebug() << "[Net] session restored";

    if (m_loggedIn) {
        m_loggedIn = false;
        emit loggedInChanged();
    }

    refreshSessionKey();

    return true;
}
