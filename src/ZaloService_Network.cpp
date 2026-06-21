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

// Low-level HTTP request building, cookie handling, and persisted-session
// (save/load/refresh) management.

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
            QString line = QString::fromUtf8(hp.second);
            QString kv   = line.split(";").first().trimmed();
            int eq = kv.indexOf('=');
            if (eq > 0) {
                m_cookies[kv.left(eq).trimmed()] = kv.mid(eq + 1).trimmed();
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
    s.setValue("friendUrl",  m_friendServiceUrl);
    s.setValue("fileUrl",    m_fileServiceUrl);
    s.setValue("quickMsgUrl", m_quickMessageServiceUrl);
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
    s.sync();
    qDebug() << "[Zalo] saveSession: saved" << m_cookies.size() << "cookies, uid=" << m_uid;
}

bool ZaloService::loadSession()
{
    QSettings s("BerryLife", "Zalo10");
    QString uid = s.value("uid").toString();
    if (uid.isEmpty()) {
        qDebug() << "[Zalo] loadSession: no saved session";
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
    m_friendServiceUrl    = s.value("friendUrl").toString();
    m_fileServiceUrl      = s.value("fileUrl").toString();
    m_quickMessageServiceUrl = s.value("quickMsgUrl").toString();
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

    if (m_uid.isEmpty() || m_secretKey.isEmpty() || m_chatServiceUrl.isEmpty()) {
        qDebug() << "[Zalo] loadSession: incomplete session data";
        return false;
    }

    qDebug() << "[Zalo] loadSession: restored session uid=" << m_uid
             << "cookies=" << m_cookies.size();

    if (m_loggedIn) {
        m_loggedIn = false;
        emit loggedInChanged();
    }

    qDebug() << "[Zalo] loadSession: cookies restored, refreshing secretKey...";
    refreshSessionKey();

    return true;
}

// ─── refreshSessionKey ────────────────────────────────────────────────────
void ZaloService::refreshSessionKey()
{
    qDebug() << "[Zalo] refreshSessionKey: calling getLoginInfo with saved cookies";
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
        qDebug() << "[Zalo] refreshSessionKey network error:" << reply->errorString()
                 << "- session may be invalid, triggering re-login";
        reply->deleteLater();
        // Do NOT fake loginSuccess here — cookies may be expired.
        // Fall through to step7 to attempt cookie renewal; if that also fails,
        // sessionExpired will be emitted and QR login sheet will open.
        m_isAutoRenew = true;
        step7_checkSession();
        return;
    }

    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] refreshSessionKey response (first200):" << raw.left(200);

    // Parse outer để lấy error_code + encrypted data
    QScriptEngine outerEng;
    outerEng.globalObject().setProperty("__raw", QString::fromUtf8(raw));
    outerEng.evaluate("var __o=null;try{__o=JSON.parse(__raw);}catch(e){__o=null;}");
    QScriptValue outerObj = outerEng.globalObject().property("__o");

    int ec = outerObj.isObject() ? outerObj.property("error_code").toInt32() : -1;
    QString encData = outerObj.isObject() ? outerObj.property("data").toString() : QString();

    // Decrypt
    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
        qDebug() << "[Zalo] refreshSessionKey decrypted (first100):" << decrypted.left(100);
    }

    bool refreshOk = false;
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
            qDebug() << "[Zalo] refreshSessionKey: inner error_code=" << innerEc << "- session expired";
        } else if (info.isObject() && !info.isNull()) {
            QString newKey = info.property("zpw_enk").toString();
            if (!newKey.isEmpty()) {
                m_secretKey   = newKey;
                m_displayName = info.property("display_name").toString();
                qDebug() << "[Zalo] refreshSessionKey: new secretKey, first20:" << m_secretKey.left(20);
                refreshOk = true;

                // Update service URLs
                QScriptValue svcMap = info.property("zpw_service_map_v3");
                if (svcMap.isObject()) {
                    QScriptValue c = svcMap.property("chat");
                    QScriptValue g = svcMap.property("group");
                    QScriptValue p = svcMap.property("profile");
                    QScriptValue gp= svcMap.property("group_poll");
                    QScriptValue f = svcMap.property("friend");
                    QScriptValue qm= svcMap.property("quick_message");
                    if (c.isArray())  m_chatServiceUrl      = c.property(0).toString();
                    if (g.isArray())  m_groupServiceUrl     = g.property(0).toString();
                    if (p.isArray())  m_profileServiceUrl   = p.property(0).toString();
                    if (gp.isArray()) m_groupPollServiceUrl = gp.property(0).toString();
                    if (f.isArray())  m_friendServiceUrl    = f.property(0).toString();
                    if (qm.isArray()) m_quickMessageServiceUrl = qm.property(0).toString();
                }

                // Update WS URLs
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

    if (!refreshOk) {
        qDebug() << "[Zalo] refreshSessionKey: secretKey expired - tự động renew qua step7/step8";
        m_secretKey.clear();
        disconnectWebSocket();
        m_isAutoRenew = true;  // đánh dấu: step7/step8 này là auto-renew, KHÔNG hiện QR nếu thất bại
        step7_checkSession();
        return;
    }

    if (!m_loggedIn) {
        m_loggedIn = true;
        emit loggedInChanged();
        m_listenTimer->start(8000);
    }
    // Chỉ emit loginSuccess lần đầu; các lần refresh dùng sessionRefreshed
    if (!m_loginEmitted) {
        m_loginEmitted = true;
        emit loginSuccess(m_uid, m_displayName);
    } else {
        emit sessionRefreshed();
    }
}

// Marks an existing message as recalled in SQLite (msgType=99, content cleared)
// and notifies QML so the already-displayed bubble can update in place instead
// of a stray "chat.undo" JSON blob appearing as its own message.
//
// The original text is preserved in recalledOriginalContent regardless of the
// current "Show Recalled Messages" setting, so toggling the setting later (or
// re-fetching the thread) can still recover it instead of it being gone forever.
//
// IMPORTANT: this must be idempotent. The server can (and does) redeliver the
// same "chat.undo" event again later — e.g. when a thread is reopened and the
// app resyncs from an older lastId checkpoint, both the original message and
// its recall get replayed. On that replay, `content` in the row may already be
// '' (already recalled) or may have been momentarily restored by a redelivered
// dbSaveMessage for the original message — either way, blindly doing
// "recalledOriginalContent = content" again could clobber the text we already
// preserved on the first, real recall. The CASE guard below only copies
// `content` into recalledOriginalContent the first time (while it's still
// empty), and leaves it untouched on any later, duplicate recall of the same
// message.
