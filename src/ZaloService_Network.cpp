#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include "ZaloCookieJar.hpp"
#include <QDateTime>
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

void ZaloService::retryRefreshSessionKey()
{
    if (m_loggedIn) return;
    refreshSessionKey();
}

void ZaloService::refreshSessionKey()
{
    QVariantMap data;
    data["computer_name"] = QString("Web");
    data["imei"]          = m_imei;
    data["language"]      = m_language;
    data["ts"]            = QString::number(QDateTime::currentMSecsSinceEpoch());

    EncryptedParams ep = buildEncryptedParams(data);
    m_pendingEncryptKey = ep.encryptKey;

    QVariantMap paramsForSign;
    paramsForSign["zcid"]           = ep.zcid;
    paramsForSign["zcid_ext"]       = ep.zcid_ext;
    paramsForSign["enc_ver"]        = ep.enc_ver;
    paramsForSign["params"]         = ep.encryptedData;
    paramsForSign["type"]           = QString::number(API_TYPE);
    paramsForSign["client_version"] = QString::number(API_VERSION);
    paramsForSign["nretry"]         = QString("0");

    QVariantMap params = paramsForSign;
    params["signkey"] = buildSignKey("getlogininfo", paramsForSign);
    params["imei"]    = m_imei;

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getLoginInfo", params);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setRawHeader("zpw_ver",  QByteArray::number(API_VERSION));
    req.setRawHeader("zpw_type", QByteArray::number(API_TYPE));

    QNetworkReply *reply = m_manager->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onRefreshSessionKeyDone()));
}

void ZaloService::onRefreshSessionKeyDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Net] refreshSessionKey network error:" << reply->errorString()
                 << "- session may be invalid, triggering re-login";
        QNetworkReply::NetworkError nerr = reply->error();
        reply->deleteLater();
        // Transient network error (code < 200): keep the saved session and retry in 5s instead of
        // falling into step7/step8 and showing the QR login.
        if (int(nerr) < 200 && m_renewRetry < 12) {
            ++m_renewRetry;
            qDebug() << "[Net] session refresh: transient network error, retry" << m_renewRetry;
            QTimer::singleShot(5000, this, SLOT(retryRefreshSessionKey()));
            return;
        }
        m_isAutoRenew = true;
        step7_checkSession();
        return;
    }

    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    bool refreshOk = false;
    if (ZJson::nativeMode()) {
        // Headless service: no QtScript (it segfaults in the headless process).
        refreshOk = applyRefreshSessionResponseNative(raw);
    } else {
    QScriptEngine outerEng;
    outerEng.globalObject().setProperty("__raw", QString::fromUtf8(raw));
    outerEng.evaluate("var __o=null;try{__o=JSON.parse(__raw);}catch(e){__o=null;}");
    QScriptValue outerObj = outerEng.globalObject().property("__o");

    int ec = outerObj.isObject() ? outerObj.property("error_code").toInt32() : -1;
    QString encData = outerObj.isObject() ? outerObj.property("data").toString() : QString();

    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
    }

    if (ec == 0 && !decrypted.isEmpty()) {
        QScriptEngine eng;
        eng.globalObject().setProperty("__dec", decrypted);
        eng.evaluate("var __info=null; var __innerEc=0;"
                     "try{"
                     "  var tmp=JSON.parse(__dec);"
                     "  __innerEc = (tmp.error_code !== undefined) ? tmp.error_code : 0;"
                     "  if(tmp&&tmp.data){__info=tmp.data;}else{__info=tmp;}"
                     "}catch(e){__info=null;}");
        QScriptValue info = eng.globalObject().property("__info");
        int innerEc = eng.globalObject().property("__innerEc").toInt32();

        if (innerEc != 0) {
            qWarning() << "[Net] refreshSessionKey: inner error_code=" << innerEc << "- session expired";
        } else if (info.isObject() && !info.isNull()) {
            QString newKey = info.property("zpw_enk").toString();
            if (!newKey.isEmpty()) {
                m_secretKey   = newKey;
                m_displayName = info.property("display_name").toString();
                refreshOk = true;

                QScriptValue svcMap = info.property("zpw_service_map_v3");
                if (svcMap.isObject()) {
                    QScriptValue c = svcMap.property("chat");
                    QScriptValue g = svcMap.property("group");
                    QScriptValue p = svcMap.property("profile");
                    QScriptValue gp= svcMap.property("group_poll");
                    QScriptValue gb= svcMap.property("group_board");
                    QScriptValue f = svcMap.property("friend");
                    QScriptValue qm= svcMap.property("quick_message");
                    QScriptValue rc= svcMap.property("reaction");
                    QScriptValue cv= svcMap.property("conversation");
                    if (c.isArray())  m_chatServiceUrl      = c.property(0).toString();
                    if (g.isArray())  m_groupServiceUrl     = g.property(0).toString();
                    if (p.isArray())  m_profileServiceUrl   = p.property(0).toString();
                    if (gp.isArray()) m_groupPollServiceUrl = gp.property(0).toString();
                    if (gb.isArray()) m_groupBoardServiceUrl = gb.property(0).toString();
                    if (f.isArray())  m_friendServiceUrl    = f.property(0).toString();
                    if (qm.isArray()) m_quickMessageServiceUrl = qm.property(0).toString();
                    if (rc.isArray()) m_reactionServiceUrl  = rc.property(0).toString();
                    if (cv.isArray()) m_conversationServiceUrl = cv.property(0).toString();
                }

                QScriptValue wsArr = info.property("zpw_ws");
                if (wsArr.isArray()) {
                    m_zpwWsUrls.clear();
                    int len = wsArr.property("length").toInt32();
                    for (int i = 0; i < len && i < 10; ++i) {
                        QString wsUrl = wsArr.property(i).toString();
                        if (!wsUrl.isEmpty()) m_zpwWsUrls << wsUrl;
                    }
                    disconnectWebSocket();
                    connectWebSocket();
                }

                saveSession();
            }
        }
    }

    }

    if (!refreshOk) {
        qDebug() << "[Net] session key expired, renewing";
        m_secretKey.clear();
        disconnectWebSocket();
        m_isAutoRenew = true;
        step7_checkSession();
        return;
    }

    m_renewRetry = 0;
    if (!m_loggedIn) {
        m_loggedIn = true;
        emit loggedInChanged();
        m_listenTimer->start(8000);
        m_keepAliveTimer->start(KEEPALIVE_INTERVAL_MS);
    }
    if (!m_loginEmitted) {
        m_loginEmitted = true;
        emit loginSuccess(m_uid, m_displayName);
    } else {
        emit sessionRefreshed();
    }
}

bool ZaloService::applyRefreshSessionResponseNative(const QByteArray &raw)
{
    QVariantMap outer = jsonToMap(raw);
    int ec = outer.isEmpty() ? -1 : outer.value("error_code").toInt();
    QString encData = outer.value("data").toString();

    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
    }
    if (ec != 0 || decrypted.isEmpty()) return false;

    QVariantMap tmp = jsonToMap(decrypted.toUtf8());
    int innerEc = tmp.contains("error_code") ? tmp.value("error_code").toInt() : 0;
    QVariantMap info = tmp;
    QVariant dataV = tmp.value("data");
    if (dataV.isValid() && !dataV.isNull())
        info = (dataV.type() == QVariant::Map) ? dataV.toMap() : QVariantMap();

    if (innerEc != 0) {
        qWarning() << "[Net] refreshSessionKey(native): inner error_code=" << innerEc << "- session expired";
        return false;
    }
    if (info.isEmpty()) return false;

    QString newKey = info.value("zpw_enk").toString();
    if (newKey.isEmpty()) return false;

    m_secretKey   = newKey;
    m_displayName = info.value("display_name").toString();

    QVariant svcV = info.value("zpw_service_map_v3");
    if (svcV.type() == QVariant::Map) {
        QVariantMap svc = svcV.toMap();
        struct Pick { const char *key; QString *dst; };
        Pick picks[] = {
            { "chat",          &m_chatServiceUrl },
            { "group",         &m_groupServiceUrl },
            { "profile",       &m_profileServiceUrl },
            { "group_poll",    &m_groupPollServiceUrl },
            { "group_board",   &m_groupBoardServiceUrl },
            { "friend",        &m_friendServiceUrl },
            { "quick_message", &m_quickMessageServiceUrl },
            { "reaction",      &m_reactionServiceUrl },
            { "conversation",  &m_conversationServiceUrl }
        };
        for (unsigned i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i) {
            QVariant a = svc.value(picks[i].key);
            if (a.type() == QVariant::List) {
                QVariantList lst = a.toList();
                if (!lst.isEmpty()) *picks[i].dst = lst.at(0).toString();
            }
        }
    }

    QVariant wsV = info.value("zpw_ws");
    if (wsV.type() == QVariant::List) {
        QVariantList wsArr = wsV.toList();
        m_zpwWsUrls.clear();
        for (int i = 0; i < wsArr.size() && i < 10; ++i) {
            QString wsUrl = wsArr.at(i).toString();
            if (!wsUrl.isEmpty()) m_zpwWsUrls << wsUrl;
        }
        disconnectWebSocket();
        connectWebSocket();
    }

    saveSession();
    return true;
}

void ZaloService::onKeepAliveTimer()
{
    sendKeepAlive();
}

void ZaloService::sendKeepAlive()
{
    if (!m_loggedIn || m_secretKey.isEmpty() || m_chatServiceUrl.isEmpty()) {
        return;
    }

    QVariantMap innerParams;
    innerParams["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_chatServiceUrl + "/keepalive"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onKeepAliveDone()));
}

void ZaloService::onKeepAliveDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Session renewal comes from the new Set-Cookie in the /keepalive response. If it isn't read and
    // saved, the request still succeeds (error_code=0) but the stored cookies expire on schedule.
    int cookiesBefore = m_cookies.size();
    parseCookiesFromReply(reply);
    int cookiesAfter = m_cookies.size();

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec == 0) {
        saveSession();
    } else {
        qWarning() << "[Net] keepAlive error_code=" << ec
                 << "msg=" << root["error_message"].toString();
    }
}

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
