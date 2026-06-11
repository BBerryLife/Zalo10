#include "ZaloService.hpp"
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

const char *ZaloService::USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

const char *ZaloService::AES_FIXED_KEY = "3FC4F0D2AB50057BCE0D90D9187A22B1";

static QVariantMap jsonToMap(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantMap();
    QScriptEngine eng;
    eng.evaluate("var __safeJSON = function(s){try{return JSON.parse(s);}catch(e){return null;}}");
    QScriptValue fn = eng.globalObject().property("__safeJSON");
    QScriptValue val = fn.call(QScriptValue(), QScriptValueList()
                               << eng.toScriptValue(QString::fromUtf8(trimmed)));
    if (!val.isValid() || val.isNull() || val.isUndefined() || val.isError())
        return QVariantMap();
    QVariant v = val.toVariant();
    if (v.type() == QVariant::Map)
        return v.toMap();
    return QVariantMap();
}

static QVariantList jsonToList(const QByteArray &raw)
{
    QByteArray trimmed = raw.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith("<")) return QVariantList();
    QScriptEngine eng;
    eng.evaluate("var __safeJSON = function(s){try{return JSON.parse(s);}catch(e){return null;}}");
    QScriptValue fn = eng.globalObject().property("__safeJSON");
    QScriptValue val = fn.call(QScriptValue(), QScriptValueList()
                               << eng.toScriptValue(QString::fromUtf8(trimmed)));
    if (!val.isValid() || val.isNull() || val.isUndefined() || val.isError())
        return QVariantList();
    QVariant v = val.toVariant();
    if (v.type() == QVariant::List)
        return v.toList();
    return QVariantList();
}

static QByteArray mapToJson(const QVariantMap &map)
{
    QScriptEngine eng;
    QScriptValue obj = eng.newObject();
    for (QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it) {
        QVariant v = it.value();
        switch (v.type()) {
        case QVariant::String:   obj.setProperty(it.key(), v.toString()); break;
        case QVariant::Int:
        case QVariant::LongLong: obj.setProperty(it.key(), (double)v.toLongLong()); break;
        case QVariant::UInt:
        case QVariant::ULongLong: obj.setProperty(it.key(), (double)v.toULongLong()); break;
        case QVariant::Bool:     obj.setProperty(it.key(), (bool)v.toBool()); break;
        case QVariant::Double:   obj.setProperty(it.key(), (double)v.toDouble()); break;
        case QVariant::List: {
            QVariantList lst = v.toList();
            QScriptValue arr = eng.newArray(lst.size());
            for (int i = 0; i < lst.size(); ++i)
                arr.setProperty(i, lst[i].toString());
            obj.setProperty(it.key(), arr);
            break;
        }
        default: obj.setProperty(it.key(), v.toString()); break;
        }
    }
    QScriptValue jsonStringify = eng.evaluate("JSON.stringify");
    QScriptValue result = jsonStringify.call(QScriptValue(), QScriptValueList() << obj);
    return result.toString().toUtf8();
}

static QString normalizePhotoContent(const QVariantMap &m, const QString &rawContent)
{
    QString nUrl, hUrl, tUrl;

    // 1. Try content JSON field first
    if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
        QVariantMap cm = jsonToMap(rawContent.toUtf8());
        nUrl = cm["normalUrl"].toString();
        hUrl = cm["hdUrl"].toString();
        tUrl = cm["thumbUrl"].toString();
        if (nUrl.isEmpty()) nUrl = cm["href"].toString();
        if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
        if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
    }

    // 2. Try top-level fields on message map
    if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
    if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
    if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
    if (tUrl.isEmpty()) tUrl = m["thumb"].toString();

    // 3. Try paramsExt JSON string (Zalo WS real-time photo delivery)
    if (nUrl.isEmpty()) {
        QString pe = m["paramsExt"].toString();
        if (!pe.isEmpty() && pe.trimmed().startsWith("{")) {
            QVariantMap pm = jsonToMap(pe.toUtf8());
            if (nUrl.isEmpty()) nUrl = pm["normalUrl"].toString();
            if (hUrl.isEmpty()) hUrl = pm["hdUrl"].toString();
            if (tUrl.isEmpty()) tUrl = pm["thumbUrl"].toString();
            if (nUrl.isEmpty()) nUrl = pm["href"].toString();
            if (nUrl.isEmpty()) nUrl = pm["oriUrl"].toString();
            if (tUrl.isEmpty()) tUrl = pm["thumb"].toString();
        }
    }

    // 4. Try attach sub-object
    if (nUrl.isEmpty()) {
        QVariantMap att = m["attach"].toMap();
        if (att.isEmpty()) {
            QString attStr = m["attach"].toString();
            if (!attStr.isEmpty() && attStr.startsWith("{"))
                att = jsonToMap(attStr.toUtf8());
        }
        if (!att.isEmpty()) {
            if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
            if (hUrl.isEmpty()) hUrl = att["hdUrl"].toString();
            if (tUrl.isEmpty()) tUrl = att["thumbUrl"].toString();
            if (nUrl.isEmpty()) nUrl = att["href"].toString();
            if (tUrl.isEmpty()) tUrl = att["thumb"].toString();
        }
    }

    // 5. previewThumb as last resort (may be CDN URL or base64)
    if (nUrl.isEmpty()) {
        QString pt = m["previewThumb"].toString();
        if (!pt.isEmpty()) { nUrl = pt; tUrl = pt; }
    }

    if (nUrl.isEmpty()) nUrl = hUrl; if (nUrl.isEmpty()) nUrl = tUrl;
    if (tUrl.isEmpty()) tUrl = nUrl; if (hUrl.isEmpty()) hUrl = nUrl;
    if (nUrl.isEmpty()) return rawContent;
    return QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
           .arg(nUrl).arg(tUrl).arg(hUrl);
}


static QByteArray aesGcmDecrypt(const QByteArray &keyB64, const QByteArray &cipherBytes);

ZaloService::ZaloService(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)),
      m_qrExpireTimer(new QTimer(this)), m_listenTimer(new QTimer(this)),
      m_wsReconnectTimer(new QTimer(this)),
      m_webSocket(0), m_wsUrlIndex(0), m_wsConnected(false), m_wsHandshakeSent(false),
      m_userAgent(USER_AGENT), m_language("vi"), m_loggedIn(false), m_qrCancelled(false), m_db(0)
{
    m_qrExpireTimer->setSingleShot(true);
    m_wsReconnectTimer->setSingleShot(true);
    connect(m_qrExpireTimer,    SIGNAL(timeout()), this, SLOT(onQRExpired()));
    connect(m_listenTimer,      SIGNAL(timeout()), this, SLOT(onListenTimer()));
    connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onWsReconnectTimer()));
    qsrand((uint)QDateTime::currentMSecsSinceEpoch());
    qDebug() << "[Zalo] ===== BUILD v32 - msgId-based thumbnail filename, avoid BB10 image cache =====";

    QString dbPath = QDir::homePath() + "/zalo_messages.db";
    if (sqlite3_open(dbPath.toUtf8().constData(), &m_db) == SQLITE_OK) {
        const char *sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "  msgId      TEXT PRIMARY KEY,"
            "  threadId   TEXT NOT NULL,"
            "  content    TEXT,"
            "  senderId   TEXT,"
            "  dName      TEXT,"
            "  ts         TEXT,"
            "  isMine     INTEGER DEFAULT 0,"
            "  isGroup    INTEGER DEFAULT 0,"
            "  msgType    INTEGER DEFAULT 0,"
            "  localImage TEXT DEFAULT ''"
            ");";
        sqlite3_exec(m_db, sql, 0, 0, 0);
        // Migrations for existing DBs
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN msgType    INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN localImage TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_thread ON messages(threadId,ts);", 0, 0, 0);
        // Track per-thread clear timestamps so re-fetched server msgs are filtered
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS cleared_threads ("
            "  threadId TEXT PRIMARY KEY,"
            "  clearedAt TEXT NOT NULL"
            ");", 0, 0, 0);
        qDebug() << "[Zalo] SQLite DB opened:" << dbPath;
    } else {
        qDebug() << "[Zalo] SQLite open FAILED";
        m_db = 0;
    }
}

ZaloService::~ZaloService() {
    if (m_db) { sqlite3_close(m_db); m_db = 0; }
}

void ZaloService::startQRLogin()
{
    m_qrCancelled  = false;
    m_isAutoRenew  = false;
    m_loggedIn     = false;
    m_pendingFriendAvatarCount = 0;
    m_loadedFriendAvatarCount  = 0;
    m_cookies.clear();
    m_uid.clear();
    m_displayName.clear();
    m_secretKey.clear();
    m_userAgent = generateRandomUserAgent();   // UA ngẫu nhiên mỗi lần đăng nhập mới
    m_imei = generateIMEI();                   // IMEI dựa trên UA mới → unique per login
    qDebug() << "[Zalo] startQRLogin IMEI:" << m_imei << "UA:" << m_userAgent;
    step1_loadLoginPage();
}

void ZaloService::retryQRLogin() { startQRLogin(); }

void ZaloService::cancelQRLogin()
{
    m_qrCancelled = true;
    m_qrExpireTimer->stop();
}

void ZaloService::logout()
{
    m_loggedIn = false;
    m_listenTimer->stop();
    m_wsReconnectTimer->stop();
    disconnectWebSocket();
    m_cookies.clear();
    m_secretKey.clear();
    m_uid.clear();
    m_displayName.clear();
    emit loggedInChanged();
}

void ZaloService::loginWithCookie(const QString &zpsid, const QString &zpwSek, const QString &imei, const QString &ua, const QString &token)
{
    m_qrCancelled = true;
    m_qrExpireTimer->stop();
    m_cookies.clear();
    m_cookies["zpsid"]   = zpsid.trimmed();
    m_cookies["zpw_sek"] = zpwSek.trimmed();

    if (!ua.trimmed().isEmpty()) m_userAgent = ua.trimmed();
    else m_userAgent = USER_AGENT;

    m_imei = imei.trimmed().isEmpty() ? generateIMEI() : imei.trimmed();
    qDebug() << "[Zalo] loginWithCookie IMEI:" << m_imei << "UA:" << m_userAgent;

    if (!token.trimmed().isEmpty()) {
        qDebug() << "[Zalo] Co Token ngoai. Se dung lam secretKey sau cookieStep1.";
        m_externalToken = token.trimmed();
    } else {
        m_externalToken.clear();
    }

    cookieStep1_getZaloLoginInfo();
}

void ZaloService::cookieStep1_getZaloLoginInfo()
{
    qDebug() << "[Zalo] cookieStep1";
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

    qDebug() << "[Zalo] cookieStep1 signkey:" << params["signkey"].toString();

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getLoginInfo", params);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setRawHeader("zpw_ver",  QByteArray::number(API_VERSION));
    req.setRawHeader("zpw_type", QByteArray::number(API_TYPE));

    QNetworkReply *reply = m_manager->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onCookieStep1Done()));
}

void ZaloService::onCookieStep1Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        emit loginFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] cookieStep1 raw response:" << raw.left(400);
    qDebug() << "[Zalo] cookieStep1 pendingEncryptKey:" << m_pendingEncryptKey;
    qDebug() << "[Zalo] cookieStep1 cookies:" << m_cookies;

    QVariantMap root = jsonToMap(raw);
    int errCode = root["error_code"].toInt();
    qDebug() << "[Zalo] cookieStep1 error_code:" << errCode << "msg:" << root["error_message"].toString();

    QVariantMap info;
    QVariant dataVariant = root["data"];
    if (dataVariant.type() == QVariant::Map) {
        info = dataVariant.toMap();
    } else {
        QString encData = dataVariant.toString();
        qDebug() << "[Zalo] cookieStep1 encData (first60):" << encData.left(60);
        if (errCode != 0) {
            emit loginFailed(QString("Cookie loi %1 - %2").arg(errCode).arg(root["error_message"].toString()));
            return;
        }
        if (encData.isEmpty()) {
            emit loginFailed("Cookie: response data rong");
            return;
        }
        QString decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
        qDebug() << "[Zalo] cookieStep1 decrypted (first100):" << decrypted.left(100);
        QVariantMap root2 = jsonToMap(decrypted.toUtf8());
        info = root2["data"].toMap();
        if (info.isEmpty()) info = root2;
    }

    m_secretKey   = info["zpw_enk"].toString();
    m_uid         = info["uid"].toString();
    m_displayName = info["display_name"].toString();

    if (!m_externalToken.isEmpty()) {
        qDebug() << "[Zalo] Ghi de secretKey bang externalToken.";
        m_secretKey = m_externalToken;
        m_externalToken.clear();
    }

    QVariantMap svcMap  = info["zpw_service_map_v3"].toMap();
    QVariantList chatA  = svcMap["chat"].toList();
    QVariantList groupA = svcMap["group"].toList();
    if (!chatA.isEmpty())  m_chatServiceUrl  = chatA[0].toString();
    if (!groupA.isEmpty()) m_groupServiceUrl = groupA[0].toString();

    if (m_uid.isEmpty()) {
        emit loginFailed("Cookie het han hoac sai - Lay lai ZPSID/ZPW_SEK moi");
        return;
    }

    cookieStep2_getServerInfo(m_pendingEncryptKey);
}

void ZaloService::cookieStep2_getServerInfo(const QString &encryptKey)
{
    Q_UNUSED(encryptKey);
    QVariantMap params;
    params["imei"]           = m_imei;
    params["type"]           = QString::number(API_TYPE);
    params["client_version"] = QString::number(API_VERSION);
    params["computer_name"]  = QString("Web");
    params["signkey"]        = buildSignKey("getserverinfo", params);

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getServerInfo", params);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onCookieStep2Done()));
}

void ZaloService::onCookieStep2Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->deleteLater();

    m_loggedIn = true;
    emit loggedInChanged();
    emit loginSuccess(m_uid, m_displayName);
    m_listenTimer->start(8000);
}

QByteArray ZaloService::buildFormBody(const QList<QPair<QString,QString> > &fields)
{
    QStringList parts;
    for (int i = 0; i < fields.size(); ++i) {
        QString k = QString::fromUtf8(QUrl::toPercentEncoding(fields[i].first));
        QString v = QString::fromUtf8(QUrl::toPercentEncoding(fields[i].second));
        parts << k + "=" + v;
    }
    return parts.join("&").toUtf8();
}

void ZaloService::step1_loadLoginPage()
{
    qDebug() << "[Zalo] Step1: Load Login Page";
    QNetworkReply *reply = m_manager->get(buildRequest("https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F", "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep1Done()));
}

void ZaloService::onStep1Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        emit loginFailed("Loi mang: " + reply->errorString());
        reply->deleteLater();
        return;
    }
    parseCookiesFromReply(reply);
    QString html = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    QStringList rxPatterns;
    rxPatterns << "stc-zlogin\\.zdn\\.vn/main-([\\d.]+)\\.js"
               << "main[.-]([\\w.-]+)\\.js"
               << "chunkMain[.-]([\\w.-]+)\\.js";
    m_loginVersion.clear();
    for (int pi = 0; pi < rxPatterns.size(); ++pi) {
        QRegExp rx2(rxPatterns[pi]);
        if (rx2.indexIn(html) >= 0) {
            m_loginVersion = rx2.cap(1);
            break;
        }
    }
    if (m_loginVersion.isEmpty()) m_loginVersion = "5.6.1";

    qDebug() << "[Zalo] Login Version:" << m_loginVersion;
    step2_getLoginInfo();
}

void ZaloService::step2_getLoginInfo()
{
    qDebug() << "[Zalo] Step2: getLoginInfo";
    QNetworkRequest req = buildRequest("https://id.zalo.me/account/logininfo", "https://id.zalo.me/account?continue=https%3A%2F%2Fzalo.me%2Fpc");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString,QString> > f;
    f << QPair<QString,QString>("continue", "https://zalo.me/pc") << QPair<QString,QString>("v", m_loginVersion);
    QNetworkReply *reply = m_manager->post(req, buildFormBody(f));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep2Done()));
}

void ZaloService::onStep2Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) { parseCookiesFromReply(reply); reply->deleteLater(); }
    step3_verifyClient();
}

void ZaloService::step3_verifyClient()
{
    qDebug() << "[Zalo] Step3: verifyClient";
    QNetworkRequest req = buildRequest("https://id.zalo.me/account/verify-client", "https://id.zalo.me/account?continue=https%3A%2F%2Fzalo.me%2Fpc");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString,QString> > f;
    f << QPair<QString,QString>("type", "device") << QPair<QString,QString>("continue", "https://zalo.me/pc") << QPair<QString,QString>("v", m_loginVersion);
    QNetworkReply *reply = m_manager->post(req, buildFormBody(f));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep3Done()));
}

void ZaloService::onStep3Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) { parseCookiesFromReply(reply); reply->deleteLater(); }
    step4_generateQR();
}

void ZaloService::step4_generateQR()
{
    qDebug() << "[Zalo] Step4: generateQR";
    QNetworkRequest req = buildRequest("https://id.zalo.me/account/authen/qr/generate", "https://id.zalo.me/account?continue=https%3A%2F%2Fzalo.me%2Fpc");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString,QString> > f;
    f << QPair<QString,QString>("continue", "https://zalo.me/pc") << QPair<QString,QString>("v", m_loginVersion);
    QNetworkReply *reply = m_manager->post(req, buildFormBody(f));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep4Done()));
}

void ZaloService::onStep4Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        emit loginFailed(reply->errorString());
        reply->deleteLater();
        return;
    }
    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    QVariantMap data = root["data"].toMap();
    m_qrCode = data["code"].toString();

    qDebug() << "[Zalo] Step4: QR code =" << m_qrCode.left(30) << "...";
    qDebug() << "[Zalo] Step4: image field =" << data["image"].toString().left(60);

    if (m_qrCode.isEmpty()) {
        emit loginFailed("Khong nhan duoc ma QR tu Zalo. Thu lai.");
        return;
    }

    QString imageB64 = data["image"].toString();
    int comma = imageB64.indexOf(',');
    if (comma >= 0) imageB64 = imageB64.mid(comma + 1);

    if (!imageB64.isEmpty()) {
        QByteArray imgData = QByteArray::fromBase64(imageB64.toUtf8());
        QString tempPath = QDir::tempPath() + "/qr.png";
        QFile imgFile(tempPath);
        if (imgFile.open(QIODevice::WriteOnly)) {
            imgFile.write(imgData);
            imgFile.close();
        }
        emit qrCodeReady("file://" + tempPath, m_qrCode);
    } else {
        QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(m_qrCode));
        QString qrApiUrl = "https://quickchart.io/qr?text=" + encoded + "&size=300&margin=2";
        qDebug() << "[Zalo] Step4: Fetching QR image from quickchart.io...";
        QNetworkRequest qrReq = buildRequest(qrApiUrl, "");
        QNetworkReply *qrReply = m_manager->get(qrReq);
        connect(qrReply, SIGNAL(finished()), this, SLOT(onQRImageFetched()));
    }
    m_qrExpireTimer->start(100000);
    step5_waitingScan();
}

void ZaloService::onQRImageFetched()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[Zalo] onQRImageFetched: network error:" << reply->errorString();
        emit qrCodeReady("", m_qrCode);
        reply->deleteLater();
        return;
    }

    QByteArray imgData = reply->readAll();
    reply->deleteLater();

    if (imgData.isEmpty()) {
        qDebug() << "[Zalo] onQRImageFetched: empty response";
        emit qrCodeReady("", m_qrCode);
        return;
    }

    QString tempPath = QDir::tempPath() + "/qr.png";
    QFile imgFile(tempPath);
    if (imgFile.open(QIODevice::WriteOnly)) {
        imgFile.write(imgData);
        imgFile.close();
        qDebug() << "[Zalo] onQRImageFetched: saved QR image to" << tempPath;
        emit qrCodeReady("file://" + tempPath, m_qrCode);
    } else {
        qDebug() << "[Zalo] onQRImageFetched: cannot write file";
        emit qrCodeReady("", m_qrCode);
    }
}

void ZaloService::step5_waitingScan()
{
    if (m_qrCancelled) return;
    qDebug() << "[Zalo] Step5: waitingScan";
    QNetworkRequest req = buildRequest("https://id.zalo.me/account/authen/qr/waiting-scan", "https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString,QString> > f;
    f << QPair<QString,QString>("code", m_qrCode) << QPair<QString,QString>("continue", "https://chat.zalo.me/") << QPair<QString,QString>("v", m_loginVersion);
    QNetworkReply *reply = m_manager->post(req, buildFormBody(f));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep5Done()));
}

void ZaloService::onStep5Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (m_qrCancelled) { reply->deleteLater(); return; }
    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    int errorCode    = root["error_code"].toInt();

    if (errorCode == 8) {
        step5_waitingScan();
    } else if (errorCode == 0) {
        m_displayName = root["data"].toMap()["display_name"].toString();
        emit qrScanned(m_displayName);
        step6_waitingConfirm();
    } else {
        emit loginFailed(QString("QR scan error: %1").arg(errorCode));
    }
}

void ZaloService::step6_waitingConfirm()
{
    if (m_qrCancelled) return;
    qDebug() << "[Zalo] Step6: waitingConfirm";
    QNetworkRequest req = buildRequest("https://id.zalo.me/account/authen/qr/waiting-confirm", "https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QList<QPair<QString,QString> > f;
    f << QPair<QString,QString>("code", m_qrCode) << QPair<QString,QString>("gToken", "") << QPair<QString,QString>("gAction", "CONFIRM_QR") << QPair<QString,QString>("continue", "https://chat.zalo.me/") << QPair<QString,QString>("v", m_loginVersion);
    QNetworkReply *reply = m_manager->post(req, buildFormBody(f));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep6Done()));
}

void ZaloService::onStep6Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (m_qrCancelled) { reply->deleteLater(); return; }
    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] Step6 Response:" << raw;

    QVariantMap root = jsonToMap(raw);
    int errorCode    = root["error_code"].toInt();

    if      (errorCode == 8)   { step6_waitingConfirm(); }
    else if (errorCode == -13) { emit loginFailed("Tu choi xac nhan"); }
    else if (errorCode == 0)   { m_qrExpireTimer->stop(); step7_checkSession(); }
    else { emit loginFailed(QString("Confirm error: %1").arg(errorCode)); }
}

void ZaloService::step7_checkSession()
{
    qDebug() << "[Zalo] Step7: checkSession cookies:" << m_cookies.keys();
    QNetworkRequest req = buildRequest(
        "https://id.zalo.me/account/checksession?continue=https%3A%2F%2Fchat.zalo.me%2Findex.html",
        "https://id.zalo.me/account?continue=https%3A%2F%2Fchat.zalo.me%2F");
    req.setRawHeader("sec-fetch-dest", "document");
    req.setRawHeader("sec-fetch-mode", "navigate");
    req.setRawHeader("sec-fetch-site", "same-origin");
    req.setRawHeader("upgrade-insecure-requests", "1");
    QNetworkReply *reply = m_manager->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onStep7Done()));
}

void ZaloService::onStep7Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    parseCookiesFromReply(reply);

    QVariant redirectVar = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectVar.isValid()) {
        QUrl redirectUrl = redirectVar.toUrl();
        if (redirectUrl.isRelative()) {
            redirectUrl = reply->url().resolved(redirectUrl);
        }
        qDebug() << "[Zalo] Step7 Dang Redirect de hung Cookie:" << redirectUrl.toString();

        QString redirStr = redirectUrl.toString();
        QString redirReferer = redirStr.contains("jr.zaloapp.com") ? "https://id.zalo.me/"
                             : redirStr.contains("jr.chat.zalo.me") ? "https://jr.zaloapp.com/"
                             : "https://id.zalo.me/";
        QNetworkRequest redirReq = buildRequest(redirStr, redirReferer);
        // Với cross-domain redirect (jr.*), set sec-fetch-site = cross-site
        if (!redirStr.contains("id.zalo.me") && !redirStr.contains("chat.zalo.me")) {
            redirReq.setRawHeader("sec-fetch-site", "cross-site");
            redirReq.setRawHeader("sec-fetch-dest", "document");
            redirReq.setRawHeader("sec-fetch-mode", "navigate");
            redirReq.setRawHeader("upgrade-insecure-requests", "1");
        }
        QNetworkReply *redirReply = m_manager->get(redirReq);
        connect(redirReply, SIGNAL(finished()), this, SLOT(onStep7Done()));
        reply->deleteLater();
        return;
    }

    reply->deleteLater();
    step8_getZaloLoginInfo();
}

void ZaloService::step8_getZaloLoginInfo()
{
    qDebug() << "[Zalo] Step8: getZaloLoginInfo";
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

    qDebug() << "[Zalo] step8 signkey:" << params["signkey"].toString();
    qDebug() << "[Zalo] step8 zcid:" << params["zcid"].toString().left(20);
    qDebug() << "[Zalo] step8 zcid_ext:" << params["zcid_ext"].toString();
    qDebug() << "[Zalo] step8 enc_ver:" << params["enc_ver"].toString();
    qDebug() << "[Zalo] step8 params(enc):" << params["params"].toString().left(40);

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getLoginInfo", params);
    qDebug() << "[Zalo] step8 URL:" << urlStr.left(300);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setRawHeader("zpw_ver",  QByteArray::number(API_VERSION));
    req.setRawHeader("zpw_type", QByteArray::number(API_TYPE));

    QNetworkReply *reply = m_manager->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onStep8Done()));
}

void ZaloService::onStep8Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[Zalo Error] Step8 network error:" << reply->errorString();
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] Step8 raw response:" << raw.left(300);
    qDebug() << "[Zalo] Step8 cookies count:" << m_cookies.size();

    QScriptEngine outerEng;
    outerEng.globalObject().setProperty("__raw", QString::fromUtf8(raw));
    outerEng.evaluate("var __o = null; try { __o = JSON.parse(__raw); } catch(e) { __o = null; }");
    QScriptValue outerObj = outerEng.globalObject().property("__o");

    if (!outerObj.isValid() || outerObj.isNull()) {
        qDebug() << "[Zalo Error] Step8: outer JSON parse failed";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8 parse failed");
        return;
    }

    int errCode8 = outerObj.property("error_code").toInt32();
    qDebug() << "[Zalo] Step8 error_code:" << errCode8
             << "msg:" << outerObj.property("error_message").toString();

    // Decrypt data field
    QString encData = outerObj.property("data").toString();
    qDebug() << "[Zalo] Step8 encrypted data (first60):" << encData.left(60);
    qDebug() << "[Zalo] Step8 pendingEncryptKey:" << m_pendingEncryptKey;

    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
        qDebug() << "[Zalo] Step8 decrypted (first100):" << decrypted.left(100);
    }

    if (decrypted.isEmpty()) {
        qDebug() << "[Zalo Error] Step8: decrypt returned empty";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8 decrypt failed");
        return;
    }

    QScriptEngine eng;
    eng.globalObject().setProperty("__dec", decrypted);
    eng.evaluate("var __info = null;"
                 "try {"
                 "  var tmp = JSON.parse(__dec);"
                 "  if (tmp && tmp.data) { __info = tmp.data; }"
                 "  else { __info = tmp; }"
                 "} catch(e) { __info = null; }");

    QScriptValue info = eng.globalObject().property("__info");
    if (!info.isValid() || info.isNull() || info.isUndefined() || !info.isObject()) {
        qDebug() << "[Zalo Error] Step8: info object invalid";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8: invalid info object");
        return;
    }

    m_secretKey   = info.property("zpw_enk").toString();
    m_uid         = info.property("uid").toString();
    m_displayName = info.property("display_name").toString();

    qDebug() << "[Zalo] secretKey (first20):" << m_secretKey.left(20);
    qDebug() << "[Zalo] info keys check - zpw_enk empty?:" << m_secretKey.isEmpty()
             << "uid empty?:" << m_uid.isEmpty();
    qDebug() << "[Zalo] uid:" << m_uid << "name:" << m_displayName;

    if (m_secretKey.isEmpty() || m_uid.isEmpty()) {
        qDebug() << "[Zalo Error] Step8: missing zpw_enk or uid";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8: missing key/uid");
        return;
    }

    // Extract service URLs từ zpw_service_map_v3 — dùng .property() chain
    m_chatServiceUrl.clear();
    m_groupServiceUrl.clear();
    m_profileServiceUrl.clear();
    m_groupPollServiceUrl.clear();
    m_friendServiceUrl.clear();
    m_fileServiceUrl.clear();

    QScriptValue svcMap = info.property("zpw_service_map_v3");
    if (svcMap.isObject()) {
        QScriptValue chatArr   = svcMap.property("chat");
        QScriptValue groupArr  = svcMap.property("group");
        QScriptValue profArr   = svcMap.property("profile");
        QScriptValue pollArr   = svcMap.property("group_poll");
        QScriptValue friendArr = svcMap.property("friend");
        QScriptValue fileArr   = svcMap.property("file");

        if (chatArr.isArray())   m_chatServiceUrl      = chatArr.property(0).toString();
        if (groupArr.isArray())  m_groupServiceUrl     = groupArr.property(0).toString();
        if (profArr.isArray())   m_profileServiceUrl   = profArr.property(0).toString();
        if (pollArr.isArray())   m_groupPollServiceUrl = pollArr.property(0).toString();
        if (friendArr.isArray()) m_friendServiceUrl    = friendArr.property(0).toString();
        if (fileArr.isArray())   m_fileServiceUrl      = fileArr.property(0).toString();
    }

    // Extract WebSocket URLs
    m_zpwWsUrls.clear();
    QScriptValue wsArr = info.property("zpw_ws");
    if (wsArr.isArray()) {
        int len = wsArr.property("length").toInt32();
        for (int i = 0; i < len && i < 10; ++i) {
            QString wsUrl = wsArr.property(i).toString();
            if (!wsUrl.isEmpty())
                m_zpwWsUrls << wsUrl;
        }
    }

    qDebug() << "[Zalo] chat:"        << m_chatServiceUrl;
    qDebug() << "[Zalo] group:"       << m_groupServiceUrl;
    qDebug() << "[Zalo] profile:"     << m_profileServiceUrl;
    qDebug() << "[Zalo] group_poll:"  << m_groupPollServiceUrl;
    qDebug() << "[Zalo] friend:"      << m_friendServiceUrl;
    qDebug() << "[Zalo] file:"        << m_fileServiceUrl;
    qDebug() << "[Zalo] zpw_ws count:" << m_zpwWsUrls.size()
             << (m_zpwWsUrls.isEmpty() ? QString() : m_zpwWsUrls[0]);

    step9_getServerInfo();
}

void ZaloService::step9_getServerInfo()
{
    qDebug() << "[Zalo] Step9: getServerInfo";
    QVariantMap params;
    params["imei"]           = m_imei;
    params["type"]           = QString::number(API_TYPE);
    params["client_version"] = QString::number(API_VERSION);
    params["computer_name"]  = QString("Web");
    params["signkey"]        = buildSignKey("getserverinfo", params);

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getServerInfo", params);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onStep9Done()));
}

void ZaloService::onStep9Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) { parseCookiesFromReply(reply); reply->deleteLater(); }
    m_loggedIn = true;
    emit loggedInChanged();
    saveSession();
    emit loginSuccess(m_uid, m_displayName);
    m_listenTimer->start(8000);
    connectWebSocket();
}

// ─── WebSocket RFC 6455 over QSslSocket ───────────────────────────────────
// BB10 NDK 10.3 dùng Qt4 — không có QtWebSockets.
// Implement thủ công: TLS handshake → HTTP Upgrade → binary framing.
// Zalo WS protocol: header 4 bytes [version(1B) cmd(2B LE) subCmd(1B)] + payload
// Source: zca-js listen.js getHeader() + sendWs()
//   cmd=1   subCmd=1: server→client cipherKey (AES-GCM key)
//   cmd=2   subCmd=1: client→server PING (keepalive)
//   cmd=501 subCmd=0: user message  (msgs[], encrypt=AES-GCM)
//   cmd=521 subCmd=0: group message (groupMsgs[], encrypt=AES-GCM)
//   cmd=510 subCmd=1: request/response old DM messages
//   cmd=511 subCmd=1: request/response old Group messages

// ─── WS helpers ──────────────────────────────────────────────────────────────
// Gửi WS binary frame theo format zca-js sendWs():
//   [version(1B), cmd_lo(1B), cmd_hi(1B), subCmd(1B), JSON data...]
void ZaloService::sendWsRequest(int cmd, int subCmd, const QString &jsonData)
{
    if (!m_webSocket || !m_wsConnected) return;
    static int reqId = 0;
    QString json = jsonData.trimmed();
    if (json.endsWith("}")) {
        json.chop(1);
        json += QString(",\"req_id\":\"req_%1\"}").arg(reqId++);
    }
    QByteArray payload;
    payload.append((char)1);                   // version
    payload.append((char)(cmd & 0xFF));        // cmd lo
    payload.append((char)((cmd >> 8) & 0xFF)); // cmd hi
    payload.append((char)(subCmd & 0xFF));     // subCmd
    payload.append(json.toUtf8());
    m_webSocket->write(maskWsFrame(0x2, payload));
}

void ZaloService::sendWsPing()
{
    if (!m_webSocket || !m_wsConnected) return;
    // Ping: cmd=2 subCmd=1 data={"eventId": <timestamp>}
    QString json = QString("{\"eventId\":%1}").arg(QDateTime::currentMSecsSinceEpoch());
    QByteArray payload;
    payload.append((char)1);    // version
    payload.append((char)2);    // cmd=2 lo
    payload.append((char)0);    // cmd=2 hi
    payload.append((char)1);    // subCmd=1
    payload.append(json.toUtf8());
    m_webSocket->write(maskWsFrame(0x2, payload));
}

void ZaloService::connectWebSocket()
{
    if (m_zpwWsUrls.isEmpty()) {
        qDebug() << "[Zalo WS] No zpw_ws URLs, skip";
        return;
    }
    disconnectWebSocket();

    m_wsUrlIndex = 0;
    m_wsUrls = m_zpwWsUrls;

    QUrl url(m_wsUrls[m_wsUrlIndex]);
    // Thêm query params như zca-js
    url.addQueryItem("t",        QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.addQueryItem("zpw_ver",  QString::number(API_VERSION));
    url.addQueryItem("zpw_type", QString::number(API_TYPE));

    qDebug() << "[Zalo WS] Connecting to:" << url.toString().left(80);

    m_webSocket = new QSslSocket(this);
    m_wsBuffer.clear();
    m_wsHandshakeSent = false;
    m_wsConnected     = false;
    m_wsCipherKey.clear();

    connect(m_webSocket, SIGNAL(connected()),          this, SLOT(onWsConnected()));
    connect(m_webSocket, SIGNAL(encrypted()),          this, SLOT(onWsEncrypted()));
    connect(m_webSocket, SIGNAL(readyRead()),          this, SLOT(onWsReadyRead()));
    connect(m_webSocket, SIGNAL(disconnected()),       this, SLOT(onWsDisconnected()));
    connect(m_webSocket, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(onWsSslErrors(QList<QSslError>)));

    bool useSsl = (url.scheme() == "wss" || url.scheme() == "https");
    int  port   = url.port(useSsl ? 443 : 80);

    if (useSsl)
        m_webSocket->connectToHostEncrypted(url.host(), port);
    else
        m_webSocket->connectToHost(url.host(), port);

    m_webSocket->setProperty("wsUrl", url.toString());
}

void ZaloService::disconnectWebSocket()
{
    if (m_webSocket) {
        m_webSocket->disconnect();
        m_webSocket->abort();
        m_webSocket->deleteLater();
        m_webSocket = 0;
    }
    m_wsConnected     = false;
    m_wsHandshakeSent = false;
    m_wsCipherKey.clear();
    m_wsBuffer.clear();
}

void ZaloService::onWsConnected()
{
    // Plain TCP connected (non-SSL) — gửi HTTP Upgrade ngay
    if (!m_webSocket) return;
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::onWsEncrypted()
{
    // TLS handshake xong — gửi HTTP Upgrade
    if (!m_webSocket) return;
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::sendWsHandshake(const QUrl &url)
{
    if (m_wsHandshakeSent) return;
    m_wsHandshakeSent = true;

    // Tạo Sec-WebSocket-Key ngẫu nhiên (16 bytes base64)
    QByteArray keyBytes;
    for (int i = 0; i < 16; ++i)
        keyBytes.append((char)(qrand() % 256));
    QString wsKey = keyBytes.toBase64();

    // Expected Accept = base64(SHA1(key + "258EAFA5-..."))
    QByteArray raw = (wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toUtf8();
    m_wsExpectedAccept = QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toBase64();

    QString path = url.path();
    if (path.isEmpty()) path = "/";
    if (!url.encodedQuery().isEmpty()) path += "?" + url.encodedQuery();

    QString handshake =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + url.host() + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + wsKey + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: https://chat.zalo.me\r\n"
        "User-Agent: " + m_userAgent + "\r\n"
        "Cookie: " + buildCookieHeader() + "\r\n"
        "Accept-Language: en-US,en;q=0.9\r\n"
        "\r\n";

    m_webSocket->write(handshake.toUtf8());
    qDebug() << "[Zalo WS] HTTP Upgrade sent to" << url.host() << path.left(60);
}

void ZaloService::onWsSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    m_webSocket->ignoreSslErrors();
}

void ZaloService::onWsReadyRead()
{
    if (!m_webSocket) return;
    m_wsBuffer += m_webSocket->readAll();

    if (!m_wsConnected) {
        // Đang chờ HTTP 101 Switching Protocols
        int headerEnd = 0;
        if (parseWsHandshakeResponse(m_wsBuffer, headerEnd)) {
            qDebug() << "[Zalo WS] Upgraded OK, WebSocket connected";
            m_wsConnected = true;
            m_wsBuffer    = m_wsBuffer.mid(headerEnd);
            // Server sẽ tự gửi cmd=1 handshake ngay sau khi connect
        } else if (m_wsBuffer.contains("\r\n\r\n")) {
            // HTTP response nhưng không phải 101
            qDebug() << "[Zalo WS] Upgrade failed:" << m_wsBuffer.left(200);
            disconnectWebSocket();
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(5000);
            return;
        } else {
            return; // Chưa nhận đủ header
        }
    }

    // Parse WebSocket frames từ m_wsBuffer
    while (m_wsBuffer.size() >= 2) {
        bool  fin    = (m_wsBuffer[0] & 0x80) != 0;
        int   opcode = (m_wsBuffer[0] & 0x0F);
        bool  masked = (m_wsBuffer[1] & 0x80) != 0;
        quint64 payLen = (m_wsBuffer[1] & 0x7F);

        int headerSize = 2;
        if (payLen == 126) {
            if (m_wsBuffer.size() < 4) break;
            payLen = ((quint8)m_wsBuffer[2] << 8) | (quint8)m_wsBuffer[3];
            headerSize = 4;
        } else if (payLen == 127) {
            if (m_wsBuffer.size() < 10) break;
            payLen = 0;
            for (int i = 0; i < 8; ++i)
                payLen = (payLen << 8) | (quint8)m_wsBuffer[2 + i];
            headerSize = 10;
        }

        if (masked) headerSize += 4;
        if ((int)(headerSize + payLen) > m_wsBuffer.size()) break; // Chưa đủ

        QByteArray payload = m_wsBuffer.mid(headerSize, payLen);
        if (masked) {
            QByteArray maskKey = m_wsBuffer.mid(headerSize - 4, 4);
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload[i] ^ maskKey[i % 4];
        }

        m_wsBuffer = m_wsBuffer.mid(headerSize + payLen);

        Q_UNUSED(fin);
        handleWsFrame(opcode, payload);
    }
}

bool ZaloService::parseWsHandshakeResponse(const QByteArray &data, int &headerEnd)
{
    int idx = data.indexOf("\r\n\r\n");
    if (idx < 0) return false;
    headerEnd = idx + 4;
    QString resp = QString::fromUtf8(data.left(headerEnd));
    return resp.contains("101") && resp.contains("Upgrade", Qt::CaseInsensitive);
}

void ZaloService::handleWsFrame(int opcode, const QByteArray &payload)
{
    if (opcode == 0x8) { // Close
        qDebug() << "[Zalo WS] Server sent Close frame";
        onWsDisconnected();
        return;
    }
    if (opcode == 0x9) { // Ping → Pong
        QByteArray pong = maskWsFrame(0xA, payload);
        m_webSocket->write(pong);
        return;
    }
    if (opcode != 0x2 && opcode != 0x1) return; // Chỉ xử lý binary/text

    handleWsMessage(opcode, payload);
}

void ZaloService::handleWsMessage(int /*opcode*/, const QByteArray &payload)
{
    // Zalo WS header: version(1B) + cmd(2B LE) + subCmd(1B) = 4 bytes
    // Source: zca-js listen.js buffer[0]=version, readUInt16LE(1)=cmd, buffer[3]=subCmd
    if (payload.size() < 4) return;

    quint8  version = (quint8)payload[0];
    qint32  cmd     = (qint32)((quint8)payload[1] | ((quint8)payload[2] << 8));
    qint32  subCmd  = (qint32)((quint8)payload[3]);
    QByteArray data = payload.mid(4);
    Q_UNUSED(version);

    if (cmd == 1 && subCmd == 1) {
        QVariantMap parsed = jsonToMap(data);
        QString keyB64 = parsed["key"].toString();
        m_wsCipherKey = QByteArray::fromBase64(keyB64.toUtf8());
        qDebug() << "[Zalo WS] Handshake OK, cipherKey len:" << m_wsCipherKey.size();

        // Gửi PING ngay (cmd=2 subCmd=1) theo zca-js
        sendWsPing();
        // Bắt đầu ping timer 25s
        if (m_listenTimer) m_listenTimer->start(25000);
        if (!m_pendingDmThreadIds.isEmpty()) {
            QString req510 = QString("{\"first\":true,\"lastId\":null,\"toid\":\"%1\",\"preIds\":[]}")
                             .arg(m_pendingDmThreadIds.head());
            sendWsRequest(510, 1, req510);
            qDebug() << "[Zalo WS] WS ready, auto-fetch DM toid=" << m_pendingDmThreadIds.head();
        }
        return;
    }

    // cmd=501 (DM mới) / cmd=521 (group mới): new real-time message
    if ((cmd == 501 || cmd == 521) && subCmd == 0) {
        bool isGroup = (cmd == 521);

        // WS event: JSON { data: "<base64>", encrypt: 0|1|2|3 }
        // encrypt=0: plaintext JSON, encrypt=2/3: AES-GCM
        QVariantMap outer = jsonToMap(data);
        QVariantMap d;
        int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
        if (encType == 0) {
            // Không mã hoá
            d = outer.contains("data") && outer["data"].type() == QVariant::Map
                ? outer["data"].toMap() : outer;
        } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
            QString rawB64 = outer["data"].toString();
            if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
            QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
            qDebug() << "[Zalo WS] GCM decrypt cmd501: keyLen=" << m_wsCipherKey.size()
                     << "cipherLen=" << cipherBytes.size()
                     << "keyHex8=" << m_wsCipherKey.left(8).toHex()
                     << "ivHex8=" << cipherBytes.left(8).toHex();
            QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
            qDebug() << "[Zalo WS] GCM result501: plainLen=" << plain.size();
            if (encType == 2 && !plain.isEmpty()) {
                // Format: gzip (1f 8b ...) — windowBits = 15+16
                QByteArray inflated;
                inflated.resize(plain.size() * 8 + 4096);
                z_stream zs;
                memset(&zs, 0, sizeof(zs));
                zs.next_in  = (Bytef*)plain.constData();
                zs.avail_in = plain.size();
                bool inflateOk = false;
                if (inflateInit2(&zs, 15 + 16) == Z_OK) {
                    int outPos = 0, ret2 = Z_OK;
                    do {
                        if (outPos >= inflated.size())
                            inflated.resize(inflated.size() * 2);
                        zs.next_out  = (Bytef*)(inflated.data() + outPos);
                        zs.avail_out = inflated.size() - outPos;
                        ret2 = inflate(&zs, Z_SYNC_FLUSH);
                        outPos = zs.total_out;
                    } while ((ret2 == Z_OK || ret2 == Z_BUF_ERROR) && zs.avail_in > 0);
                    inflateEnd(&zs);
                    if ((ret2 == Z_STREAM_END || ret2 == Z_OK) && zs.total_out > 0) {
                        inflated.resize(zs.total_out);
                        inflateOk = true;
                    }
                }
                if (inflateOk) {
                    qDebug() << "[Zalo WS] inflated (first150):" << QString::fromUtf8(inflated.left(150));
                    QVariantMap parsed = jsonToMap(inflated);
                    // zca-js: inflate result is direct JSON, no "data" wrapper
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed; // direct struct: { ms:[], msgs:[], ... }
                } else {
                    qDebug() << "[Zalo WS] inflate FAILED, trying raw plain";
                    qDebug() << "[Zalo WS] plain (first150):" << QString::fromUtf8(plain.left(150));
                    QVariantMap parsed = jsonToMap(plain);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                }
            } else if (!plain.isEmpty()) {
                qDebug() << "[Zalo WS] encType=3 plain (first150):" << QString::fromUtf8(plain.left(150));
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qDebug() << "[Zalo WS] decrypt returned empty for encType=" << encType;
            }
        } else {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            if (dec.isEmpty() || dec.trimmed() == "{}")
                dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
            QVariantMap r = jsonToMap(dec.toUtf8());
            d = r.contains("data") ? r["data"].toMap() : r;
        }

        // zca-js real-time: field "ms" (not "msgs") for cmd=501/521
        QVariantList msgs;
        if (isGroup) {
            // zca-js cmd=521: parsedData.groupMsgs
            msgs = d["groupMsgs"].toList();
            if (msgs.isEmpty()) msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
        } else {
            // zca-js cmd=501: parsedData.msgs
            msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
            if (msgs.isEmpty()) msgs = d["groupMsgs"].toList(); // fallback
        }
        // Zalo sometimes wraps in d["data"]
        if (msgs.isEmpty()) {
            QVariantMap dd = d["data"].toMap();
            msgs = isGroup ? dd["groupMsgs"].toList() : dd["msgs"].toList();
            if (msgs.isEmpty()) msgs = isGroup ? dd["msgs"].toList() : dd["ms"].toList();
            if (msgs.isEmpty()) msgs = dd["ms"].toList();
        }
        qDebug() << "[Zalo WS] cmd=501/521 encType=" << (outer.contains("encrypt") ? outer["encrypt"].toInt() : 1)
                 << "d.keys=" << d.keys() << "msgs.size=" << msgs.size() << "isGroup=" << isGroup;

        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap m = msgs[i].toMap();

            QString rawUidFrom = m["uidFrom"].toString();
            QString rawIdTo    = m["idTo"].toString();
            bool isSelf = (rawUidFrom == "0"); // "0" = tin của mình

            // Resolve "0" → m_uid
            QString uidFrom = isSelf      ? m_uid : rawUidFrom;
            QString idTo    = (rawIdTo == "0") ? m_uid : rawIdTo;

            QString threadId;
            if (isGroup) {
                threadId = idTo;
            } else {
                // zca-js Message.js: threadId = uidFrom=="0" ? idTo : uidFrom
                threadId = isSelf ? idTo : uidFrom;
            }
            if (threadId.isEmpty() || threadId == m_uid) continue;

            QString msgId = m["msgId"].toString();
            if (!msgId.isEmpty()) {
                if (m_seenMsgIds.contains(msgId)) continue;
                m_seenMsgIds.insert(msgId);
            }

            QVariantMap out;
            out["msgId"]    = msgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = isGroup;
            out["isMine"]   = isSelf;
            out["msgType"]  = m["msgType"].toInt();

            int mt = m["msgType"].toInt();
            // Also try alternate field names Zalo WS uses
            if (mt == 0) {
                mt = m["type"].toInt();
                if (mt == 0) mt = m["mt"].toInt();
                if (mt == 0) mt = m["msgtype"].toInt();
            }
            QString rawContent = m["content"].toString();
            // Debug: dump paramsExt and previewThumb to find photo data
            if (rawContent.isEmpty()) {
                QString paramsExtStr = m["paramsExt"].toString();
                QString previewThumb = m["previewThumb"].toString();
                qDebug() << "[Zalo WS] msg keys=" << m.keys()
                         << "msgType=" << m["msgType"].toInt()
                         << "type=" << m["type"].toInt()
                         << "mt=" << m["mt"].toInt()
                         << "paramsExt(100)=" << paramsExtStr.left(100)
                         << "previewThumb(60)=" << previewThumb.left(60);
            }

            // Zalo WS real-time photo: msgType may be 0 but photo URLs are in paramsExt/previewThumb
            if (mt == 2 || rawContent.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContent);
                if (normalized != rawContent && !normalized.isEmpty()) {
                    rawContent = normalized;
                    mt = 2;
                    out["msgType"] = 2;
                    qDebug() << "[Zalo WS] photo detected via paramsExt/previewThumb: content=" << rawContent.left(80);
                    // FULL MESSAGE DUMP để debug URL ảnh thật
                    qDebug() << "[Zalo WS] PHOTO MSG FULL KEYS=" << m.keys();
                    for (const QString &k : m.keys()) {
                        QString v = m[k].toString();
                        if (!v.isEmpty() && v.length() < 500)
                            qDebug() << "[Zalo WS] PHOTO MSG" << k << "=" << v.left(200);
                    }
                }
            }
            if (mt == 2) {
                // Normalize photo content to {"normalUrl":"...","thumbUrl":"...","hdUrl":"..."}
                // WS may deliver via content JSON (href/thumb), top-level, or in "attach" sub-object
                QString nUrl, hUrl, tUrl;
                if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContent.toUtf8());
                    nUrl = cm["normalUrl"].toString();
                    hUrl = cm["hdUrl"].toString();
                    tUrl = cm["thumbUrl"].toString();
                    if (nUrl.isEmpty()) nUrl = cm["href"].toString();
                    if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
                    if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
                }
                // Check top-level fields
                if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumb"].toString();
                // Check "attach" sub-object (Zalo WS real-time delivery)
                if (nUrl.isEmpty()) {
                    QVariantMap att = m["attach"].toMap();
                    if (att.isEmpty()) {
                        // attach may be a JSON string
                        QString attStr = m["attach"].toString();
                        if (!attStr.isEmpty() && attStr.startsWith("{"))
                            att = jsonToMap(attStr.toUtf8());
                    }
                    if (!att.isEmpty()) {
                        if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                        if (hUrl.isEmpty()) hUrl = att["hdUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = att["thumbUrl"].toString();
                        if (nUrl.isEmpty()) nUrl = att["href"].toString();
                        if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = att["thumb"].toString();
                    }
                }
                // Check "params" sub-object
                if (nUrl.isEmpty()) {
                    QVariantMap prm = m["params"].toMap();
                    if (prm.isEmpty()) {
                        QString prmStr = m["params"].toString();
                        if (!prmStr.isEmpty() && prmStr.startsWith("{"))
                            prm = jsonToMap(prmStr.toUtf8());
                    }
                    if (!prm.isEmpty()) {
                        if (nUrl.isEmpty()) nUrl = prm["normalUrl"].toString();
                        if (hUrl.isEmpty()) hUrl = prm["hdUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = prm["thumbUrl"].toString();
                        if (nUrl.isEmpty()) nUrl = prm["href"].toString();
                        if (tUrl.isEmpty()) tUrl = prm["thumb"].toString();
                    }
                }

                if (nUrl.isEmpty()) nUrl = hUrl;
                if (nUrl.isEmpty()) nUrl = tUrl;
                if (tUrl.isEmpty()) tUrl = nUrl;
                if (hUrl.isEmpty()) hUrl = nUrl;

                if (!nUrl.isEmpty()) {
                    rawContent = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
                                 .arg(nUrl).arg(tUrl).arg(hUrl);
                }
                // Debug: print all keys so we can diagnose when URLs still missing
                if (nUrl.isEmpty()) {
                    qDebug() << "[Zalo WS] photo msgType=2 but no URL found. m.keys=" << m.keys()
                             << "content=" << rawContent.left(100);
                }
                // If nUrl is NOT a real HTTP URL (it's a Zalo protobuf thumbnail):
                // 1. Decode thumbnail immediately for fast preview.
                // 2. Call fetchMsgDetail (converimgs) to get the real full-size HTTP URL.
                if (!nUrl.isEmpty() && !nUrl.startsWith("http") && !msgId.isEmpty()) {
                    qDebug() << "[Zalo WS] photo has protobuf thumb (not HTTP URL), decoding thumbnail msgId=" << msgId;
                    downloadImageMessage(msgId, nUrl, threadId);
                    // Schedule full-res fetch via converimgs so we replace the 24x24 thumb
                    bool grp = out["isGroup"].toBool();
                    if (!m_pendingPhotoMsgIds.contains(msgId))
                        fetchMsgDetail(msgId, threadId, grp);
                }
            }
            out["content"] = rawContent;

            qDebug() << "[Zalo WS] new msg from" << uidFrom
                     << "thread" << threadId << "content=" << out["content"].toString().left(60);
            dbSaveMessage(out, threadId);
            emit newMessage(threadId, out);
            if (!isSelf && threadId != m_activeThreadId && !m_mutedThreads.contains(threadId)) {
                QString senderName = out["dName"].toString();
                if (senderName.isEmpty()) senderName = "Unknown";
                int mt = out["msgType"].toInt();
                QString msgPreview = (mt == 2) ? "[Photo]" : out["content"].toString().left(80);
                if (msgPreview.isEmpty()) msgPreview = "[Message]";
                bool isGrp = out["isGroup"].toBool();
                QString notifTitle = isGrp ? m_groupNames.value(threadId, "Zalo10") : "Zalo10";
                sendHubNotification(notifTitle, senderName + ": " + msgPreview, threadId);
            }
            if (threadId == m_activeThreadId && !msgId.isEmpty()) {
                qint64 newNum = msgId.toLongLong();
                qint64 curNum = m_lastPollMsgId.toLongLong();
                if (newNum > curNum) m_lastPollMsgId = msgId;
            }
        }
        return;
    }

    // cmd=510 subCmd=1: server trả lời old messages DM (response của requestOldMessages)
    // (zca-js: emit "old_messages", responseMsgs, ThreadType.User)
    if (cmd == 510 && subCmd == 1) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d;
        int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
        if (encType == 0) {
            d = outer.contains("data") ? outer["data"].toMap() : outer;
        } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
            QString rawB64 = outer["data"].toString();
            if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
            QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
            QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
            if (encType == 2 && !plain.isEmpty()) {
                // Format: gzip (1f 8b ...) — windowBits = 15+16
                QByteArray inflated;
                inflated.resize(plain.size() * 8 + 4096);
                z_stream zs2; memset(&zs2, 0, sizeof(zs2));
                zs2.next_in = (Bytef*)plain.constData(); zs2.avail_in = plain.size();
                bool inflateOk2 = false;
                if (inflateInit2(&zs2, 15 + 16) == Z_OK) {
                    int outPos2 = 0, r2 = Z_OK;
                    do {
                        if (outPos2 >= inflated.size()) inflated.resize(inflated.size() * 2);
                        zs2.next_out  = (Bytef*)(inflated.data() + outPos2);
                        zs2.avail_out = inflated.size() - outPos2;
                        r2 = inflate(&zs2, Z_SYNC_FLUSH);
                        outPos2 = zs2.total_out;
                    } while ((r2 == Z_OK || r2 == Z_BUF_ERROR) && zs2.avail_in > 0);
                    inflateEnd(&zs2);
                    if ((r2 == Z_STREAM_END || r2 == Z_OK) && zs2.total_out > 0) {
                        inflated.resize(zs2.total_out);
                        inflateOk2 = true;
                    }
                }
                if (inflateOk2) {
                    qDebug() << "[Zalo WS] inflated (first150):" << QString::fromUtf8(inflated.left(150));
                    QVariantMap parsed = jsonToMap(inflated);
                    // zca-js: inflate result is direct JSON, no "data" wrapper
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed; // direct struct: { ms:[], msgs:[], ... }
                } else {
                    qDebug() << "[Zalo WS] inflate FAILED, trying raw plain";
                    qDebug() << "[Zalo WS] plain (first150):" << QString::fromUtf8(plain.left(150));
                    QVariantMap parsed = jsonToMap(plain);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                }
            } else if (!plain.isEmpty()) {
                qDebug() << "[Zalo WS] encType=3 plain (first150):" << QString::fromUtf8(plain.left(150));
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qDebug() << "[Zalo WS] decrypt returned empty for encType=" << encType;
            }
        } else {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            if (dec.isEmpty() || dec.trimmed() == "{}")
                dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
            QVariantMap r = jsonToMap(dec.toUtf8());
            d = r.contains("data") ? r["data"].toMap() : r;
        }

        QVariantList rawMsgs = d["msgs"].toList();
        if (rawMsgs.isEmpty()) rawMsgs = d["ms"].toList();
        qDebug() << "[Zalo WS] old_messages DM count:" << rawMsgs.size()
                 << "d.keys=" << d.keys() << "activeThread:" << m_activeThreadId;

        // ── Xác định emitThread ────────────────────────────────────────────
        QString emitThread;
        if (!m_pendingDmThreadIds.isEmpty()) {
            emitThread = m_pendingDmThreadIds.dequeue();
        } else {
            emitThread = m_activeThreadId;
        }
        qDebug() << "[Zalo WS] old_messages: emitThread=" << emitThread
                 << "msgs=" << rawMsgs.size();

        if (emitThread.isEmpty()) return;

        // FIX: validate msgs belong to emitThread (guard against stale queue responses)
        if (!rawMsgs.isEmpty()) {
            bool anyMatch = false;
            for (int vi = 0; vi < rawMsgs.size(); ++vi) {
                QVariantMap vm = rawMsgs[vi].toMap();
                QString vFrom = vm["uidFrom"].toString();
                QString vTo   = vm["idTo"].toString();
                if (vFrom == emitThread || vTo == emitThread
                    || (vFrom == m_uid && vTo == emitThread)
                    || (vTo == m_uid && vFrom == emitThread)) {
                    anyMatch = true; break;
                }
            }
            if (!anyMatch) {
                qDebug() << "[Zalo WS] cmd=510 stale response, discarding (emitThread=" << emitThread << ")";
                return;
            }
        }

        QVariantList msgs;
        qint64 maxMsgNum = -1;
        QString newestMsgId;

        for (int i = 0; i < rawMsgs.size(); ++i) {
            QVariantMap m = rawMsgs[i].toMap();
            QString msgId      = m["msgId"].toString();
            QString rawUidFrom = m["uidFrom"].toString();
            bool isMine = (rawUidFrom == "0" || rawUidFrom == m_uid);
            QString uidFrom = (rawUidFrom == "0") ? m_uid : rawUidFrom;

            QVariantMap out;
            out["msgId"]    = msgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = false;
            out["isMine"]   = isMine;
            out["msgType"]  = m["msgType"].toInt();

            int mtH = m["msgType"].toInt();
            if (mtH == 0) {
                mtH = m["type"].toInt();
                if (mtH == 0) mtH = m["mt"].toInt();
            }
            QString rawContentH = m["content"].toString();
            // Normalize photo: also handles paramsExt/previewThumb (msgType may be 0)
            if (mtH == 2 || rawContentH.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContentH);
                if (normalized != rawContentH && !normalized.isEmpty()) {
                    rawContentH = normalized;
                    mtH = 2;
                    out["msgType"] = 2;
                }
            }

            // Check if this is a pending photo detail response (from fetchMsgDetail) - no longer WS-based
            // Just check if photo has no real HTTP URL and schedule HTTP fetch
            if ((mtH == 2 || out["msgType"].toInt() == 2) && !msgId.isEmpty()) {
                QString checkUrl;
                if (!rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContentH.toUtf8());
                    checkUrl = cm["normalUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["hdUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["thumbUrl"].toString();
                }
                if (checkUrl.isEmpty() || !checkUrl.startsWith("http")) {
                    if (!m_pendingPhotoMsgIds.contains(msgId) && !checkUrl.isEmpty()) {
                        qDebug() << "[Zalo WS] old_messages photo protobuf thumb, decoding msgId=" << msgId;
                        // Decode thumbnail for immediate preview
                        downloadImageMessage(msgId, checkUrl, emitThread);
                        // Also fetch full-res via converimgs
                        bool grp = out["isGroup"].toBool();
                        fetchMsgDetail(msgId, emitThread, grp);
                    }
                }
            }
            out["content"] = rawContentH;
            msgs.append(out);

            if (!msgId.isEmpty()) {
                qint64 num = msgId.toLongLong();
                if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
                if (isMine) m_seenMsgIds.insert(msgId);
            }
        }

        if (!newestMsgId.isEmpty()) m_lastPollMsgId = newestMsgId;

        for (int i = 0; i < msgs.size(); ++i)
            dbSaveMessage(msgs[i].toMap(), emitThread);
        if (!newestMsgId.isEmpty())
            m_threadLastMsgId[emitThread] = newestMsgId;

        emit messagesReady(emitThread, msgs);
    }
}

QByteArray ZaloService::maskWsFrame(int opcode, const QByteArray &data)
{
    QByteArray frame;
    frame.append((char)(0x80 | opcode)); // FIN + opcode
    quint32 maskKey = qrand();
    QByteArray mask(4, 0);
    mask[0] = (maskKey >> 24) & 0xFF;
    mask[1] = (maskKey >> 16) & 0xFF;
    mask[2] = (maskKey >>  8) & 0xFF;
    mask[3] = (maskKey      ) & 0xFF;

    int len = data.size();
    if (len < 126) {
        frame.append((char)(0x80 | len));
    } else if (len < 65536) {
        frame.append((char)(0x80 | 126));
        frame.append((char)(len >> 8));
        frame.append((char)(len & 0xFF));
    } else {
        frame.append((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append((char)((len >> (8*i)) & 0xFF));
    }
    frame += mask;
    for (int i = 0; i < len; ++i)
        frame.append(data[i] ^ mask[i % 4]);
    return frame;
}

void ZaloService::onWsDisconnected()
{
    qDebug() << "[Zalo WS] Disconnected";
    m_wsConnected = false;
    m_wsCipherKey.clear();
    if (!m_loggedIn) return;
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(5000);
}

void ZaloService::onWsReconnectTimer()
{
    qDebug() << "[Zalo WS] Reconnecting...";
    connectWebSocket();
}

void ZaloService::fetchConversations(){
    if (!m_loggedIn) return;

    QVariantMap qp;
    qp["zpw_ver"]  = QString::number(API_VERSION);
    qp["zpw_type"] = QString::number(API_TYPE);

    QString base = m_groupPollServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupPollServiceUrl;
    QString urlStr = buildRawUrl(base + "/api/group/getlg/v4", qp);
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
        return;
    }

    qDebug() << "[Zalo] fetchConvo raw (first200):" << raw.left(200);
    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo Error] fetchConvo error_code:" << ec << root["error_message"].toString();
        emit conversationsReady(QVariantList());
        return;
    }

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

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo Error] groupDetails outer error:" << ec << root["error_message"].toString();
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] groupDetails decrypted (first200):" << dec.left(200);

    QVariantMap outer = jsonToMap(dec.toUtf8());
    int ec2 = outer["error_code"].toInt();
    if (ec2 != 0) {
        qDebug() << "[Zalo Error] groupDetails inner error:" << ec2 << outer["error_message"].toString();
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
        }
    }

    qDebug() << "[Zalo] groupDetails found" << threads.size() << "groups with names";
    if (!threads.isEmpty())
        emit conversationsReady(threads);
}

void ZaloService::downloadAvatar(const QString &threadId, const QString &url)
{
    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;

    // Check cache theo cả full URL và base URL
    if (m_avatarCache.contains(url)) {
        emit avatarReady(threadId, m_avatarCache[url]);
        return;
    }
    if (m_avatarCache.contains(baseUrl)) {
        emit avatarReady(threadId, m_avatarCache[baseUrl]);
        return;
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

    QString fname = "/tmp/avatar_" + md5Hex(url) + ".jpg";
    QFile f(fname);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    QString localPath = "file://" + fname;
    m_avatarCache[url] = localPath;
    int qmark = url.indexOf('?');
    if (qmark > 0) m_avatarCache[url.left(qmark)] = localPath;
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
    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchFriends:" << root["error_message"].toString();
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

    if (!threads.isEmpty()) {
        emit friendsReady(threads);
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
    }
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
        int itemType = item["recommItemType"].toInt();

        if (i < 5)
            qDebug() << "[Zalo] fetchInvites item[" << i << "] recommItemType=" << itemType
                     << "dataInfo keys=" << item["dataInfo"].toMap().keys();

        if (itemType != 1) continue;

        QVariantMap info = item["dataInfo"].toMap();
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

void ZaloService::fetchMessages(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn) return;

    if (isGroup) {
        // Group: dùng HTTP GET /api/group/history (theo zca-js getGroupChatHistory.ts)
        QVariantMap innerParams;
        innerParams["grid"]  = threadId;
        innerParams["count"] = 50;

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
        QString urlStr = m_groupServiceUrl + "/api/group/history"
                       + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE)
                       + "&params=" + QUrl::toPercentEncoding(encParams);

        qDebug() << "[Zalo] fetchMessages group GET" << urlStr.left(100) << "isGroup:" << isGroup;
        QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
        reply->setProperty("threadId", threadId);
        reply->setProperty("isGroup",  true);
        connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDone()));
    } else {
        // DM: dùng WebSocket cmd=510 requestOldMessages (theo zca-js listen.js)
        // HTTP /api/message/getmsglist không tồn tại → 404
        m_pendingDmThreadIds.enqueue(threadId);
        if (!m_wsConnected || !m_webSocket) {
            qDebug() << "[Zalo] fetchMessages DM: WS not ready, connecting for" << threadId;
            if (!m_zpwWsUrls.isEmpty()) connectWebSocket();
            return;
        }
        QString lastId = m_threadLastMsgId.value(threadId, "0");
        QString req510 = QString("{\"first\":true,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                         .arg(lastId).arg(threadId);
        sendWsRequest(510, 1, req510);
        qDebug() << "[Zalo] fetchMessages DM: sent WS cmd=510 toid=" << threadId << "lastId=" << lastId;
    }
}

void ZaloService::onFetchMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid    = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchMessages raw (first200):" << raw.left(200);
    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchMessages error:" << root["error_message"].toString();
        emit messagesReady(tid, QVariantList());
        return;
    }

    QVariantList msgs;
    bool isGroup = reply->property("isGroup").toBool();

    QString dec2 = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchMessages decrypted (first150):" << dec2.left(150);

    QVariantMap outer2 = jsonToMap(dec2.toUtf8());
    QVariantMap d2;
    if (outer2.contains("data") && outer2["data"].type() == QVariant::Map)
        d2 = outer2["data"].toMap();
    else
        d2 = outer2;

    QVariantList rawMsgs = isGroup ? d2["groupMsgs"].toList() : d2["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["data"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d2["msgList"].toList();
    qDebug() << "[Zalo] fetchMessages rawMsgs count:" << rawMsgs.size() << "d2 keys:" << d2.keys() << "myUid:" << m_uid;

    qint64 maxMsgNum = -1;
    QString newestMsgId;

    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();
        QVariantMap out;
        QString msgId    = m["msgId"].toString();
        QString uidFrom  = m["uidFrom"].toString();
        bool    isMine   = (uidFrom == m_uid);
        out["msgId"]    = msgId;
        out["senderId"] = uidFrom;
        out["dName"]    = m["dName"].toString();
        out["ts"]       = m["ts"].toString();
        out["isGroup"]  = isGroup;
        out["isMine"]   = isMine;
        out["msgType"]  = m["msgType"].toInt();
        int mt = m["msgType"].toInt();
        QString rawContent = m["content"].toString();
        if (mt == 2) rawContent = normalizePhotoContent(m, rawContent);
        out["content"]  = rawContent;
        msgs.append(out);
        if (i < 5)
            qDebug() << "[Zalo] msg[" << i << "] msgId=" << msgId
                     << "uidFrom=" << uidFrom << "isMine=" << isMine
                     << "content=" << m["content"].toString().left(30);

        if (!msgId.isEmpty()) {
            qint64 num = msgId.toLongLong();
            if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
        }
    }
    if (!newestMsgId.isEmpty())
        m_lastPollMsgId = newestMsgId;

    m_seenMsgIds.clear();
    for (int i = 0; i < msgs.size(); ++i) {
        QVariantMap mm = msgs[i].toMap();
        if (mm["isMine"].toBool()) {
            QString mid = mm["msgId"].toString();
            if (!mid.isEmpty()) m_seenMsgIds.insert(mid);
        }
    }

    qDebug() << "[Zalo] fetchMessages found" << msgs.size() << "msgs, lastPollMsgId=" << m_lastPollMsgId;

    // Lưu vào SQLite persistent
    for (int i = 0; i < msgs.size(); ++i)
        dbSaveMessage(msgs[i].toMap(), tid);

    emit messagesReady(tid, msgs);
}

// Fetch full message detail for a specific msgId to get real photo URLs.
// Called when a real-time WS photo arrives with only a protobuf previewThumb and no HTTP URL.
// Uses converimgs endpoint on chat/group service (NOT file service which returns 404).
void ZaloService::fetchMsgDetail(const QString &msgId, const QString &threadId, bool isGroup)
{
    if (!m_loggedIn || msgId.isEmpty() || threadId.isEmpty()) return;

    // Track this msgId as pending
    m_pendingPhotoMsgIds.insert(msgId);

    // Use CHAT service for DM, GROUP service for group — file service does NOT have getmsgdetail
    QString serviceBase = isGroup ? m_groupServiceUrl : m_chatServiceUrl;
    if (serviceBase.isEmpty()) serviceBase = m_chatServiceUrl;

    qDebug() << "[Zalo] fetchMsgDetail: serviceBase=" << serviceBase << "msgId=" << msgId;

    // Correct endpoint: /api/message/converimgs (DM) or /api/group/converimgs (group)
    // Params: toid/grid + msgId (as lastId anchor) + count
    QVariantMap params;
    if (isGroup) {
        params["grid"]   = threadId;
        params["lastId"] = msgId.toLongLong();
        params["count"]  = 1;
    } else {
        params["toid"]   = threadId;
        params["lastId"] = msgId.toLongLong();
        params["count"]  = 1;
        params["imei"]   = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString endpoint  = isGroup
        ? (serviceBase + "/api/group/converimgs")
        : (serviceBase + "/api/message/converimgs");

    QString urlStr = endpoint
        + "?zpw_ver=" + QString::number(API_VERSION)
        + "&zpw_type=" + QString::number(API_TYPE)
        + "&params="   + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchMsgDetail GET" << endpoint.left(80) << "msgId=" << msgId;

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("msgId",    msgId);
    reply->setProperty("threadId", threadId);
    reply->setProperty("isGroup",  isGroup);
    reply->setProperty("tryIndex", 0);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDetailDone()));
}

void ZaloService::onFetchMsgDetailDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString msgId    = reply->property("msgId").toString();
    QString threadId = reply->property("threadId").toString();
    bool isGroup     = reply->property("isGroup").toBool();
    Q_UNUSED(isGroup)
    QByteArray raw   = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchMsgDetail response (first300):" << raw.left(300);

    if (raw.trimmed().startsWith("<")) {
        qDebug() << "[Zalo] fetchMsgDetail: got HTML error (endpoint not found)";
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo] fetchMsgDetail error:" << root["error_message"].toString();
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchMsgDetail decrypted (first300):" << dec.left(300);

    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap d = outer.contains("data") ? outer["data"].toMap() : outer;

    // converimgs response: {"imgs":[{"msgId":"...","normalUrl":"...","hdUrl":"...","thumbUrl":"..."},...]}
    // or direct list in "data"
    QString photoUrl;
    QString normalizedContent;

    QVariantList imgs = d["imgs"].toList();
    if (imgs.isEmpty()) imgs = d["data"].toList();
    if (imgs.isEmpty()) imgs = d["msgList"].toList();

    for (int i = 0; i < imgs.size(); ++i) {
        QVariantMap img = imgs[i].toMap();
        QString id = img["msgId"].toString();
        // Match by msgId or take first result (we only requested count=1 around this msgId)
        if (id == msgId || id.isEmpty() || imgs.size() == 1) {
            photoUrl = img["normalUrl"].toString();
            if (photoUrl.isEmpty()) photoUrl = img["hdUrl"].toString();
            if (photoUrl.isEmpty()) photoUrl = img["thumbUrl"].toString();
            if (photoUrl.isEmpty()) photoUrl = img["href"].toString();
            if (!photoUrl.isEmpty()) {
                QString hUrl = img["hdUrl"].toString();
                QString tUrl = img["thumbUrl"].toString();
                if (hUrl.isEmpty()) hUrl = photoUrl;
                if (tUrl.isEmpty()) tUrl = photoUrl;
                normalizedContent = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
                    .arg(photoUrl).arg(tUrl).arg(hUrl);
                break;
            }
        }
    }

    // Fallback: maybe response is a single object directly
    if (photoUrl.isEmpty() && d.contains("normalUrl")) {
        photoUrl = d["normalUrl"].toString();
        if (photoUrl.isEmpty()) photoUrl = d["hdUrl"].toString();
        if (!photoUrl.isEmpty()) {
            normalizedContent = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
                .arg(photoUrl)
                .arg(d["thumbUrl"].toString().isEmpty() ? photoUrl : d["thumbUrl"].toString())
                .arg(d["hdUrl"].toString().isEmpty() ? photoUrl : d["hdUrl"].toString());
        }
    }

    if (photoUrl.isEmpty() || !photoUrl.startsWith("http")) {
        qDebug() << "[Zalo] fetchMsgDetail: no valid HTTP URL found. d.keys=" << d.keys()
                 << "imgs.size=" << imgs.size();
        return;
    }

    qDebug() << "[Zalo] fetchMsgDetail: got URL for msgId=" << msgId << "->" << photoUrl.left(80);

    // Update DB content with real URLs
    if (m_db && !normalizedContent.isEmpty()) {
        const char *sql = "UPDATE messages SET content=?, msgType=2 WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, normalizedContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, msgId.toUtf8().constData(),             -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Download the real image
    downloadImageMessage(msgId, photoUrl, threadId);
}

void ZaloService::sendMessage(const QString &threadId, const QString &content, bool isGroup)
{
    if (!m_loggedIn) return;

    QVariantMap msgData;
    msgData["message"]  = content;
    msgData["clientId"] = QString::number(QDateTime::currentMSecsSinceEpoch());
    msgData["ttl"]      = 0;
    if (isGroup) {
        msgData["visibility"] = 0;
        msgData["grid"]       = threadId;
        // group không gửi imei (theo zca-js)
    } else {
        msgData["toid"] = threadId;
        msgData["imei"] = m_imei;
    }

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(msgData)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    // 1-1 dùng /api/message/sms, group dùng /api/group/sendmsg (theo zca-js)
    QString base = isGroup ? m_groupServiceUrl + "/api/group/sendmsg"
                           : m_chatServiceUrl  + "/api/message/sms";

    QString urlStr = base + "?zpw_ver=" + QString::number(API_VERSION)
                          + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest sendReq = buildRequest(urlStr, "https://chat.zalo.me/");
    sendReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] sendMessage POST" << urlStr << "isGroup:" << isGroup;
    QNetworkReply *reply = m_manager->post(sendReq, body);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendMsgDone()));
}

void ZaloService::onSendMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool hasError = (reply->error() != QNetworkReply::NoError);
    QString tid   = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendMessage response:" << raw.left(200);
    emit messageSent(!hasError, tid);
}

// ─── Send Photo ──────────────────────────────────────────────────────────────
// Two-step: 1) upload to file[0]/api/{message|group}/photo_original/upload
//           2) send message via {chat|group}/api/{message|group}/photo
void ZaloService::sendPhoto(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Zalo] sendPhoto: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString ext      = path.section('.', -1).toLower();
    QString mime     = (ext == "png") ? "image/png" : (ext == "webp") ? "image/webp" : "image/jpeg";
    QString filename = path.section('/', -1);
    qint64  ts       = QDateTime::currentMSecsSinceEpoch();
    QString boundary = "----ZaloBoundary" + QString::number(ts);

    // Params in query string (AES-encrypted) per zca-js uploadAttachment.ts
    QVariantMap p;
    if (isGroup) p["grid"] = threadId;
    else         p["toid"] = threadId;
    p["totalChunk"] = 1;
    p["fileName"]   = filename;
    p["clientId"]   = (qint64)ts;
    p["totalSize"]  = (int)fileData.size();
    p["imei"]       = m_imei;
    p["isE2EE"]     = 0;
    p["jxl"]        = 0;
    p["chunkId"]    = 1;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(p)));

    // file service URL: m_fileServiceUrl (file[0]), fallback to regex on chatServiceUrl
    QString fileBase = m_fileServiceUrl;
    if (fileBase.isEmpty()) {
        fileBase = m_chatServiceUrl;
        QRegExp rx("tt-chat\\d+-wpa");
        fileBase.replace(rx, "tt-files-wpa");
    }
    QString upEndpoint = isGroup ? "group" : "message";
    QString urlStr = fileBase + "/api/" + upEndpoint + "/photo_original/upload"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0"
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QByteArray body;
    body += ("--" + boundary + "\r\n").toUtf8();
    body += ("Content-Disposition: form-data; name=\"chunkContent\"; filename=\"" + filename + "\"\r\n").toUtf8();
    body += ("Content-Type: " + mime + "\r\n\r\n").toUtf8();
    body += fileData + "\r\n";
    body += ("--" + boundary + "--\r\n").toUtf8();

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);

    qDebug() << "[Zalo] sendPhoto upload POST" << urlStr.left(120) << "size:" << fileData.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId",  threadId);
    reply->setProperty("localPath", "file://" + path);
    reply->setProperty("isGroup",   isGroup);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendPhotoDone()));
}

void ZaloService::onSendPhotoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok         = (reply->error() == QNetworkReply::NoError);
    QString tid     = reply->property("threadId").toString();
    QString localPath = reply->property("localPath").toString();
    bool isGroup    = reply->property("isGroup").toBool();
    QByteArray raw  = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendPhoto upload response:" << raw.left(300);

    if (!ok) { emit messageSent(false, tid); return; }

    // Upload response: {"error_code":0,"data":"AES_ENCRYPTED"} — same pattern as other APIs
    QVariantMap outer = jsonToMap(raw);
    if (outer["error_code"].toInt() != 0) {
        qDebug() << "[Zalo] sendPhoto upload error:" << outer["error_message"].toString();
        emit messageSent(false, tid);
        return;
    }
    QString decStr = aesDecryptBase64(m_secretKey, outer["data"].toString());
    qDebug() << "[Zalo] sendPhoto upload decrypted:" << decStr.left(200);

    // Decrypted string is {"error_code":0,"data":{"normalUrl":...,"photoId":...}}
    // Parse the outer wrapper, then get the inner data map
    QVariantMap decOuter = jsonToMap(decStr.toUtf8());
    QVariantMap uploadData;
    QVariant dataVariant = decOuter["data"];
    if (dataVariant.type() == QVariant::Map) {
        uploadData = dataVariant.toMap();
    } else {
        // Fallback: try parsing data as JSON string
        uploadData = jsonToMap(dataVariant.toString().toUtf8());
    }

    qDebug() << "[Zalo] sendPhoto upload keys:" << uploadData.keys();

    QString normalUrl = uploadData["normalUrl"].toString();
    QString thumbUrl  = uploadData["thumbUrl"].toString();
    QString hdUrl     = uploadData["hdUrl"].toString();
    QString photoId   = uploadData["photoId"].toString();
    if (thumbUrl.isEmpty()) thumbUrl = hdUrl;
    if (thumbUrl.isEmpty()) thumbUrl = normalUrl;

    if (normalUrl.isEmpty()) {
        qDebug() << "[Zalo] sendPhoto: upload OK but no normalUrl, raw:" << raw.left(200);
        emit messageSent(false, tid);
        return;
    }

    // Step 2: send photo message
    // zca-js: file[0]/api/{message|group}/photo_original/send
    // params: photoId, clientId, desc, width, height, toid|grid,
    //         rawUrl=normalUrl, hdUrl, thumbUrl, hdSize=totalSize,
    //         oriUrl (group only), normalUrl (DM only),
    //         zsource=-1, ttl=0, jcp
    qint64 ts2 = QDateTime::currentMSecsSinceEpoch();
    QVariantMap mp;
    mp["photoId"]   = photoId;
    mp["clientId"]  = QString::number(ts2);
    mp["desc"]      = "";
    mp["width"]     = 0;
    mp["height"]    = 0;
    mp["rawUrl"]    = normalUrl;
    mp["hdUrl"]     = hdUrl.isEmpty() ? normalUrl : hdUrl;
    mp["thumbUrl"]  = thumbUrl;
    mp["hdSize"]    = uploadData.value("totalSize", "0").toString();
    mp["zsource"]   = -1;
    mp["ttl"]       = 0;
    mp["jcp"]       = "{\"convertible\":\"jxl\"}";
    if (isGroup) {
        mp["grid"]     = tid;
        mp["oriUrl"]   = normalUrl;
    } else {
        mp["toid"]      = tid;
        mp["normalUrl"] = normalUrl;
    }

    QString encMsg = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(mp)));
    QByteArray body2 = "params=" + QUrl::toPercentEncoding(encMsg);

    // Use file service URL (same base as upload)
    QString fileBase2 = m_fileServiceUrl;
    if (fileBase2.isEmpty()) {
        fileBase2 = m_chatServiceUrl;
        QRegExp rx2("tt-chat\\d+-wpa");
        fileBase2.replace(rx2, "tt-files-wpa");
    }
    QString sendEndpoint = isGroup ? "group" : "message";
    QString msgUrl = fileBase2 + "/api/" + sendEndpoint + "/photo_original/send"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&nretry=0";

    QNetworkRequest req2 = buildRequest(msgUrl, "https://chat.zalo.me/");
    req2.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QString contentJson = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}")
                          .arg(normalUrl).arg(thumbUrl)
                          .arg(hdUrl.isEmpty() ? normalUrl : hdUrl);

    qDebug() << "[Zalo] sendPhoto send-msg POST" << msgUrl.left(100);
    QNetworkReply *r2 = m_manager->post(req2, body2);
    r2->setProperty("threadId",    tid);
    r2->setProperty("localPath",   localPath);
    r2->setProperty("isGroup",     isGroup);
    r2->setProperty("contentJson", contentJson);
    connect(r2, SIGNAL(finished()), this, SLOT(onSendPhotoMsgDone()));
}

void ZaloService::onSendPhotoMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok             = (reply->error() == QNetworkReply::NoError);
    QString tid         = reply->property("threadId").toString();
    QString localPath   = reply->property("localPath").toString();
    bool isGroup        = reply->property("isGroup").toBool();
    QString contentJson = reply->property("contentJson").toString();
    QByteArray raw      = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendPhoto send-msg response:" << raw.left(300);

    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        if (outer["error_code"].toInt() == 0) {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            QVariantMap data = jsonToMap(dec.toUtf8());
            qint64 msgIdInt = data["msgId"].toLongLong();
            QString msgId = (msgIdInt != 0) ? QString::number(msgIdInt)
                                            : QString::number(QDateTime::currentMSecsSinceEpoch());
            QVariantMap out;
            out["msgId"]      = msgId;
            out["content"]    = contentJson;
            out["msgType"]    = 2;
            out["isMine"]     = true;
            out["isGroup"]    = isGroup;
            out["senderId"]   = m_uid;
            out["dName"]      = m_displayName;
            out["ts"]         = QString::number(QDateTime::currentMSecsSinceEpoch());
            out["localImage"] = localPath;
            m_seenMsgIds.insert(msgId);
            dbSaveMessage(out, tid);
            emit newMessage(tid, out);
        } else {
            ok = false;
            qDebug() << "[Zalo] sendPhoto send-msg error_code:" << outer["error_code"].toInt()
                     << outer["error_message"].toString();
        }
    }
    emit messageSent(ok, tid);
}

// ─── sendFile: gửi file thường (non-image) ────────────────────────────────
void ZaloService::sendFile(const QString &threadId, const QString &localFilePath, bool isGroup)
{
    if (!m_loggedIn) return;

    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Zalo] sendFile: cannot open" << path;
        emit messageSent(false, threadId);
        return;
    }
    QByteArray fileData = file.readAll();
    file.close();

    QString filename = path.section('/', -1);
    QString clientId = QString::number(QDateTime::currentMSecsSinceEpoch());
    QString boundary = "----ZaloFileBoundary" + clientId;

    // Detect MIME
    QString ext  = filename.section('.', -1).toLower();
    QString mime = "application/octet-stream";
    if (ext == "pdf")  mime = "application/pdf";
    else if (ext == "doc" || ext == "docx") mime = "application/msword";
    else if (ext == "xls" || ext == "xlsx") mime = "application/vnd.ms-excel";
    else if (ext == "ppt" || ext == "pptx") mime = "application/vnd.ms-powerpoint";
    else if (ext == "zip" || ext == "rar")  mime = "application/zip";
    else if (ext == "mp3" || ext == "m4a")  mime = "audio/mpeg";
    else if (ext == "mp4" || ext == "mov")  mime = "video/mp4";
    else if (ext == "txt")                  mime = "text/plain";

    // Build multipart body
    QByteArray bnd = ("--" + boundary).toUtf8();
    QByteArray body;
    body += bnd + "\r\nContent-Disposition: form-data; name=\"clientId\"\r\n\r\n" + clientId.toUtf8() + "\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"ttl\"\r\n\r\n0\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"fileName\"\r\n\r\n" + filename.toUtf8() + "\r\n";
    body += bnd + "\r\nContent-Disposition: form-data; name=\"totalSize\"\r\n\r\n"
          + QByteArray::number(fileData.size()) + "\r\n";
    if (isGroup) {
        body += bnd + "\r\nContent-Disposition: form-data; name=\"grid\"\r\n\r\n" + threadId.toUtf8() + "\r\n";
        body += bnd + "\r\nContent-Disposition: form-data; name=\"visibility\"\r\n\r\n0\r\n";
    } else {
        body += bnd + "\r\nContent-Disposition: form-data; name=\"toid\"\r\n\r\n" + threadId.toUtf8() + "\r\n";
        body += bnd + "\r\nContent-Disposition: form-data; name=\"imei\"\r\n\r\n" + m_imei.toUtf8() + "\r\n";
    }
    // File data
    body += "--" + boundary.toUtf8() + "\r\n";
    body += "Content-Disposition: form-data; name=\"fileContent\"; filename=\"" + filename.toUtf8() + "\"\r\n";
    body += "Content-Type: " + mime.toUtf8() + "\r\n\r\n";
    body += fileData + "\r\n";
    body += "--" + boundary.toUtf8() + "--\r\n";

    QVariantMap fileParams;
    if (isGroup) {
        fileParams["grid"]       = threadId;
        fileParams["visibility"] = 0;
    } else {
        fileParams["toid"]       = threadId;
        fileParams["imei"]       = m_imei;
    }
    fileParams["clientId"]  = clientId;
    fileParams["ttl"]       = 0;
    fileParams["fileName"]  = filename;
    fileParams["totalSize"] = fileData.size();

    QString encParamsFile = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(fileParams)));

    // zca-js: file upload endpoint
    QString base = isGroup ? m_groupServiceUrl + "/api/group/sendfile"
                           : m_chatServiceUrl  + "/api/message/sendfile";
    QString urlStr = base
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params="   + QUrl::toPercentEncoding(encParamsFile);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);

    qDebug() << "[Zalo] sendFile POST" << urlStr.left(100) << "name:" << filename << "size:" << fileData.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onSendFileDone()));
}

void ZaloService::onSendFileDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    bool ok  = (reply->error() == QNetworkReply::NoError);
    QString tid = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] sendFile response:" << raw.left(300);
    emit messageSent(ok, tid);
}

// ─── Download image message thumbnail for display ───────────────────────────
static QByteArray makeJpegMarker(quint8 marker, const QByteArray &data)
{
    QByteArray r;
    r += (char)0xFF;
    r += (char)marker;
    quint16 len = (quint16)(data.size() + 2);
    r += (char)((len >> 8) & 0xFF);
    r += (char)(len & 0xFF);
    r += data;
    return r;
}

void ZaloService::downloadImageMessage(const QString &msgId, const QString &url, const QString &threadId)
{
    if (url.isEmpty() || msgId.isEmpty()) return;

    // Handle base64-encoded inline image (previewThumb from Zalo WS real-time photo)
    // These start with base64 data, not "http"
    if (!url.startsWith("http")) {
        if (m_avatarCache.contains(url)) {
            emit imageMsgReady(msgId, m_avatarCache[url]);
            return;
        }
        QByteArray imgData = QByteArray::fromBase64(url.toUtf8());
        if (!imgData.isEmpty()) {
            // Detect format from magic bytes
            QString ext;
            if (imgData.size() >= 2 &&
                (unsigned char)imgData[0] == 0xFF && (unsigned char)imgData[1] == 0xD8)
                ext = "jpg";
            else if (imgData.startsWith("\x89PNG"))
                ext = "png";
            else if (imgData.startsWith("GIF"))
                ext = "gif";
            else if (imgData.startsWith("RIFF") && imgData.size() > 12 &&
                     imgData.mid(8, 4) == "WEBP")
                ext = "webp";

            if (ext.isEmpty()) {
                // Check for Zalo proprietary thumbnail format:
                // [03][00][W][00][H][FF DA ...JPEG SOS data...]
                // The SOS data needs a standard JPEG header prepended to be valid.
                if (imgData.size() > 6 &&
                    (unsigned char)imgData[0] == 0x03 &&
                    (unsigned char)imgData[1] == 0x00 &&
                    (unsigned char)imgData[5] == 0xFF &&
                    (unsigned char)imgData[6] == 0xDA)
                {
                    int W = (unsigned char)imgData[2];
                    int H = (unsigned char)imgData[4];
                    QByteArray sosData = imgData.mid(5); // FF DA onwards

                    // Standard JPEG quantization tables (quality ~50)
                    static const quint8 lumaQ[64] = {
                        16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
                        14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
                        18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
                        49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99
                    };
                    static const quint8 chromaQ[64] = {
                        17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
                        24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
                        99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99
                    };
                    // Standard Huffman tables
                    static const quint8 dcLumLen[16]  = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
                    static const quint8 dcLumVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 dcChrLen[16]  = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
                    static const quint8 dcChrVal[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
                    static const quint8 acLumLen[16]  = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
                    static const quint8 acLumVal[162] = {
                        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
                        0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,
                        0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,
                        0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
                        0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,
                        0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,
                        0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
                        0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
                        0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
                        0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,
                        0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
                    };
                    static const quint8 acChrLen[16]  = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
                    static const quint8 acChrVal[162] = {
                        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
                        0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
                        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
                        0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
                        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
                        0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
                        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
                        0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
                        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
                        0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
                        0xf9,0xfa
                    };



                    // DQT
                    QByteArray dqt0; dqt0 += (char)0x00;
                    dqt0 += QByteArray((const char*)lumaQ, 64);
                    QByteArray dqt1; dqt1 += (char)0x01;
                    dqt1 += QByteArray((const char*)chromaQ, 64);

                    // SOF0: 8-bit, 3 components, Y=2x2/qtable0, Cb=1x1/qtable1, Cr=1x1/qtable1
                    QByteArray sof0Data;
                    sof0Data += (char)8;
                    sof0Data += (char)((H >> 8) & 0xFF); sof0Data += (char)(H & 0xFF);
                    sof0Data += (char)((W >> 8) & 0xFF); sof0Data += (char)(W & 0xFF);
                    sof0Data += (char)3;
                    sof0Data += "\x01\x22\x00"  // Y: 2x2 sampling, qtable 0
                                "\x02\x11\x01"  // Cb: 1x1 sampling, qtable 1
                                "\x03\x11\x01"; // Cr: 1x1 sampling, qtable 1

                    // DHT
                    QByteArray dhtDcLum; dhtDcLum += (char)0x00;
                    dhtDcLum += QByteArray((const char*)dcLumLen, 16);
                    dhtDcLum += QByteArray((const char*)dcLumVal, 12);
                    QByteArray dhtDcChr; dhtDcChr += (char)0x01;
                    dhtDcChr += QByteArray((const char*)dcChrLen, 16);
                    dhtDcChr += QByteArray((const char*)dcChrVal, 12);
                    QByteArray dhtAcLum; dhtAcLum += (char)0x10;
                    dhtAcLum += QByteArray((const char*)acLumLen, 16);
                    dhtAcLum += QByteArray((const char*)acLumVal, 162);
                    QByteArray dhtAcChr; dhtAcChr += (char)0x11;
                    dhtAcChr += QByteArray((const char*)acChrLen, 16);
                    dhtAcChr += QByteArray((const char*)acChrVal, 162);

                    QByteArray jpeg;
                    jpeg += "\xFF\xD8";                                    // SOI

                    // APP0 JFIF marker — required by Qt4's QImageReader on BB10
                    // Format: marker(FF E0) + length(00 10) + "JFIF\0" + version(1.1)
                    //         + density_units(0=no units) + Xdensity(0,1) + Ydensity(0,1)
                    //         + thumbnail_size(0,0)
                    static const char app0Data[] = {
                        'J','F','I','F','\x00',  // identifier
                        '\x01','\x01',           // version 1.1
                        '\x00',                  // density units: none
                        '\x00','\x01','\x00','\x01', // X/Y density = 1,1
                        '\x00','\x00'            // no embedded thumbnail
                    };
                    jpeg += makeJpegMarker(0xE0, QByteArray(app0Data, sizeof(app0Data))); // APP0

                    jpeg += makeJpegMarker(0xDB, dqt0);                    // DQT luma
                    jpeg += makeJpegMarker(0xDB, dqt1);                    // DQT chroma
                    jpeg += makeJpegMarker(0xC0, sof0Data);                // SOF0
                    jpeg += makeJpegMarker(0xC4, dhtDcLum);                // DHT DC luma
                    jpeg += makeJpegMarker(0xC4, dhtDcChr);                // DHT DC chroma
                    jpeg += makeJpegMarker(0xC4, dhtAcLum);                // DHT AC luma
                    jpeg += makeJpegMarker(0xC4, dhtAcChr);                // DHT AC chroma
                    jpeg += sosData;                                        // SOS + scan data
                    jpeg += "\xFF\xD9";                                     // EOI

                    // Use msgId in filename so each message has a unique path,
                    // avoiding BB10's image load cache serving stale failed data.
                    QString tmpPath = QDir::tempPath() + "/msgthumb_" +
                                      msgId + ".jpg";
                    QFile::remove(tmpPath); // Remove stale file from previous builds
                    // Transcode to PNG via QImage so BB10's ImageView always gets
                    // a format it can display (avoids BB10 libjpeg compatibility issues).
                    QImage qimg;
                    if (!qimg.loadFromData(jpeg, "JPEG")) {
                        qDebug() << "[Zalo] thumb QImage load failed, saving raw JPEG msgId=" << msgId;
                        // Fall back to saving JPEG directly
                        QFile f(tmpPath);
                        if (f.open(QIODevice::WriteOnly)) { f.write(jpeg); f.close(); }
                    } else {
                        // Save as PNG - universally compatible with BB10 ImageView
                        tmpPath.replace(".jpg", ".png");
                        QFile::remove(tmpPath);
                        qimg.save(tmpPath, "PNG");
                    }
                    QString filePath = "file://" + tmpPath;
                        m_avatarCache[url] = filePath;
                        if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                            const char *sql = "UPDATE messages SET localImage=? WHERE msgId=?";
                            sqlite3_stmt *stmt2 = 0;
                            if (sqlite3_prepare_v2(m_db, sql, -1, &stmt2, 0) == SQLITE_OK) {
                                sqlite3_bind_text(stmt2, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(stmt2, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                                sqlite3_step(stmt2);
                                sqlite3_finalize(stmt2);
                            }
                        }
                        qDebug() << "[Zalo] decoded Zalo thumb" << W << "x" << H << "msgId=" << msgId << "->" << tmpPath;
                        emit imageMsgReady(msgId, filePath);
                        return;
                }
                qDebug() << "[Zalo] downloadImageMessage: unrecognised format, msgId=" << msgId
                         << "first bytes=" << imgData.left(4).toHex();
                return;
            } // end if(ext.isEmpty())

            QString tmpPath = QDir::tempPath() + "/msgimg_" +
                              QString::number(qHash(url)) + "." + ext;
            QFile f(tmpPath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(imgData);
                f.close();
                QString filePath = "file://" + tmpPath;
                m_avatarCache[url] = filePath;
                // Persist to DB
                if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
                    const char *sql = "UPDATE messages SET localImage=? WHERE msgId=?";
                    sqlite3_stmt *stmt = 0;
                    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                        sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
                qDebug() << "[Zalo] downloadImageMessage base64 decoded msgId" << msgId << "->" << tmpPath;
                emit imageMsgReady(msgId, filePath);
                return;
            }
        } // end if(!imgData.isEmpty())
        qDebug() << "[Zalo] downloadImageMessage: non-http url and base64 decode failed, msgId=" << msgId;
        return;
    }

    if (m_avatarCache.contains(url)) {
        emit imageMsgReady(msgId, m_avatarCache[url]);
        return;
    }
    if (m_pendingAvatars.contains(url)) return;
    m_pendingAvatars.insert(url);

    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    req.setRawHeader("Accept", "image/webp,image/apng,image/*,*/*;q=0.8");
    QNetworkReply *reply = m_manager->get(req);
    reply->setProperty("msgId",    msgId);
    reply->setProperty("imgUrl",   url);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onImageMsgDownloaded()));
}

void ZaloService::onImageMsgDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString msgId   = reply->property("msgId").toString();
    QString url     = reply->property("imgUrl").toString();
    QString threadId = reply->property("threadId").toString();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_pendingAvatars.remove(url);

    if (data.isEmpty()) {
        qDebug() << "[Zalo] downloadImageMessage empty for msgId" << msgId;
        return;
    }

    QString ext = "jpg";
    QByteArray ct = reply->rawHeader("Content-Type");
    if (ct.contains("png"))  ext = "png";
    if (ct.contains("gif"))  ext = "gif";
    if (ct.contains("webp")) ext = "webp";

    // BB10's ImageView does not support WebP. Transcode any WebP (or
    // unrecognised) image to PNG via QImage so it always displays correctly.
    QByteArray finalData = data;
    if (ext == "webp" || ext == "jpg") {
        QImage img;
        if (img.loadFromData(data)) {
            QByteArray pngBuf;
            QBuffer buf(&pngBuf);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "PNG")) {
                finalData = pngBuf;
                ext = "png";
                qDebug() << "[Zalo] onImageMsgDownloaded: transcoded" << ct << "-> PNG for msgId" << msgId;
            }
        }
    }

    QString tmpPath = QDir::tempPath() + "/zalo_img_" + md5Hex(url) + "." + ext;
    QFile f(tmpPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(finalData); f.close(); }
    QString filePath = "file://" + tmpPath;
    m_avatarCache[url] = filePath;

    // Persist localImage in DB so it survives chat reopen
    if (!msgId.isEmpty() && !threadId.isEmpty() && m_db) {
        const char *sql = "UPDATE messages SET localImage=? WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filePath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    emit imageMsgReady(msgId, filePath);
}

void ZaloService::onQRExpired()
{
    if (!m_loggedIn) {
        emit qrExpired();
        step1_loadLoginPage();
    }
}

void ZaloService::onListenTimer()
{
    if (!m_loggedIn) return;
    // WS keepalive ping (cmd=2 subCmd=1) theo zca-js
    if (m_wsConnected) {
        sendWsPing();
        qDebug() << "[Zalo WS] ping sent";

        if (!m_activeThreadIsGroup && !m_activeThreadId.isEmpty()) {
            QString req510 = QString("{\"first\":false,\"lastId\":\"%1\",\"toid\":\"%2\",\"preIds\":[]}")
                             .arg(m_lastPollMsgId.isEmpty() ? "0" : m_lastPollMsgId)
                             .arg(m_activeThreadId);
            sendWsRequest(510, 1, req510);
            qDebug() << "[Zalo WS] DM incremental poll cmd=510 toid=" << m_activeThreadId
                     << "lastId=" << m_lastPollMsgId;
        }
    } else {
        if (!m_wsReconnectTimer->isActive())
            m_wsReconnectTimer->start(2000);
    }
    if (!m_activeThreadIsGroup || m_activeThreadId.isEmpty()) return;
    QVariantMap params;
    params["zpw_ver"]  = QString::number(API_VERSION);
    params["zpw_type"] = QString::number(API_TYPE);
    QString urlStr = buildRawUrl(m_chatServiceUrl + "/api/message/getrecentgroup", params);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onListenDone()));
}

void ZaloService::onListenDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;

    qDebug() << "[Zalo] listenTimer: poll OK";

    // DM: WS cmd=501 real-time, không cần poll HTTP
    if (!m_activeThreadId.isEmpty() && m_activeThreadIsGroup) {
        QVariantMap innerParams;
        innerParams["grid"]   = m_activeThreadId;
        innerParams["count"]  = 20;
        innerParams["lastId"] = m_lastPollMsgId.isEmpty() ? 0 : m_lastPollMsgId.toLongLong();

        QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
        QString urlStr = m_groupServiceUrl + "/api/group/history"
                       + "?zpw_ver=" + QString::number(API_VERSION)
                       + "&zpw_type=" + QString::number(API_TYPE)
                       + "&params=" + QUrl::toPercentEncoding(encParams);

        QNetworkReply *pollReply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
        pollReply->setProperty("threadId", m_activeThreadId);
        pollReply->setProperty("isGroup",  true);
        connect(pollReply, SIGNAL(finished()), this, SLOT(onPollMsgDone()));
    }
}

void ZaloService::setActiveThread(const QString &threadId, bool isGroup)
{
    bool changed = (m_activeThreadId != threadId);
    m_activeThreadId      = threadId;
    m_activeThreadIsGroup = isGroup;
    if (changed) {
        // Thread mới → reset poll state và clear stale DM request queue
        m_lastPollMsgId.clear();
        m_seenMsgIds.clear();
        m_pendingDmThreadIds.clear(); // FIX: tránh 510 response cũ leak vào thread mới
    }
    qDebug() << "[Zalo] setActiveThread:" << threadId << "isGroup:" << isGroup << "changed:" << changed;
}

void ZaloService::sendHubNotification(const QString &title, const QString &body, const QString &threadId)
{
    bb::platform::Notification *notif = new bb::platform::Notification(this);
    notif->setTitle(title);
    notif->setBody(body);

    bb::system::InvokeRequest req;
    req.setTarget("com.BerryLife.Zalo10.testDev");
    req.setAction("bb.action.OPEN");
    req.setData(threadId.toUtf8());
    notif->setInvokeRequest(req);

    notif->notify();
    qDebug() << "[Zalo] Hub notification sent:" << title << body.left(40);
}

void ZaloService::clearActiveThread()
{
    qDebug() << "[Zalo] clearActiveThread (was:" << m_activeThreadId << ")";
    m_activeThreadId.clear();
    m_lastPollMsgId.clear();
    m_seenMsgIds.clear();
    m_pendingDmThreadIds.clear(); // FIX: clear stale queue to avoid cross-thread msgs
}

void ZaloService::onPollMsgDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid    = reply->property("threadId").toString();
    bool isGroup   = reply->property("isGroup").toBool();
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (tid != m_activeThreadId) return;

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) return;

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap d;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        d = outer["data"].toMap();
    else
        d = outer;

    QVariantList rawMsgs = isGroup ? d["groupMsgs"].toList() : d["msgs"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d["data"].toList();
    if (rawMsgs.isEmpty()) rawMsgs = d["msgList"].toList();
    qDebug() << "[Zalo Poll] rawMsgs count:" << rawMsgs.size() << "lastPollMsgId=" << m_lastPollMsgId;

    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();
        QString msgId = m["msgId"].toString();
        if (msgId.isEmpty()) continue;

        if (m_seenMsgIds.contains(msgId)) continue;
        m_seenMsgIds.insert(msgId);

        qint64 newNum = msgId.toLongLong();
        qint64 curNum = m_lastPollMsgId.toLongLong();
        if (newNum > curNum) m_lastPollMsgId = msgId;

        QVariantMap out;
        out["msgId"]    = msgId;
        out["content"]  = m["content"].toString();
        out["senderId"] = m["uidFrom"].toString();
        out["dName"]    = m["dName"].toString();
        out["ts"]       = m["ts"].toString();
        out["isGroup"]  = isGroup;
        out["isMine"]   = (m["uidFrom"].toString() == m_uid);
        out["msgType"]  = m["msgType"].toInt();

        dbSaveMessage(out, tid);
        emit newMessage(tid, out);
    }
}

static QByteArray resolveKeyUtf8(const QString &keyStr)
{
    QByteArray k = keyStr.toUtf8();
    while (k.size() < 32) k.append('\0');
    return k.left(32);
}

static QByteArray resolveKeyBase64(const QString &keyStr)
{
    QByteArray decoded = QByteArray::fromBase64(keyStr.toUtf8());
    int sz = decoded.size();
    if (sz <= 16) { while (decoded.size() < 16) decoded.append('\0'); return decoded.left(16); }
    if (sz <= 24) { while (decoded.size() < 24) decoded.append('\0'); return decoded.left(24); }
    while (decoded.size() < 32) decoded.append('\0');
    return decoded.left(32);
}

static QByteArray resolveKey(const QString &keyStr)
{
    return resolveKeyBase64(keyStr);
}

QString ZaloService::aesEncryptHex(const QString &keyHex32, const QString &plainText)
{
    QByteArray key  = resolveKeyUtf8(keyHex32);
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toHex().toUpper();
}

QString ZaloService::aesDecryptBase64_256(const QString &keyStr, const QString &cipherB64)
{
    QByteArray key    = resolveKeyUtf8(keyStr);
    QByteArray cipher = QByteArray::fromBase64(
        QUrl::fromPercentEncoding(cipherB64.toUtf8()).toUtf8());
    if (cipher.isEmpty()) return QString();

    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    QByteArray out(cipher.size(), '\0');
    AES_KEY k;
    AES_set_decrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)cipher.constData(),
                    (unsigned char*)out.data(), cipher.size(), &k, iv, AES_DECRYPT);

    if (!out.isEmpty()) {
        int pad = (unsigned char)out[out.size()-1];
        if (pad > 0 && pad <= AES_BLOCK_SIZE) out.chop(pad);
    }
    return QString::fromUtf8(out);
}

QString ZaloService::aesEncryptBase64(const QString &keyStr, const QString &plainText)
{
    QByteArray key  = resolveKeyBase64(keyStr);
    int keyBits = key.size() * 8;
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), keyBits, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toBase64();
}

QString ZaloService::aesEncryptBase64_256(const QString &keyStr, const QString &plainText)
{
    QByteArray key  = resolveKeyUtf8(keyStr);
    qDebug() << "[Zalo] aesEncryptBase64_256(params) keyBytes=" << key.toHex() << "bits=" << (key.size()*8);
    QByteArray data = plainText.toUtf8();
    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    int pad = AES_BLOCK_SIZE - (data.size() % AES_BLOCK_SIZE);
    data.append(QByteArray(pad, (char)pad));

    QByteArray out(data.size(), '\0');
    AES_KEY k;
    AES_set_encrypt_key((const unsigned char*)key.constData(), 256, &k);
    AES_cbc_encrypt((const unsigned char*)data.constData(), (unsigned char*)out.data(), data.size(), &k, iv, AES_ENCRYPT);
    return out.toBase64();
}

// AES-GCM decrypt cho WS event data (zca-js decodeEventData, encryptType=2/3)
// Layout: iv[0:16] + aad[16:32] + ciphertext[32:N-16] + tag[N-16:N]
// encryptType=2: base64(urlencoded(data)) → inflate(plaintext)
// encryptType=3: base64(data)            → plaintext trực tiếp (no inflate)
static QByteArray aesGcmDecrypt(const QByteArray &keyRaw, const QByteArray &cipherBytes)
{
    if (cipherBytes.size() < 48) return QByteArray(); // iv(16)+aad(16)+tag(16) minimum
    QByteArray key = keyRaw;
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) {
        key = QByteArray::fromBase64(keyRaw);
    }
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return QByteArray();

    const unsigned char *iv  = (const unsigned char*)cipherBytes.constData();       // bytes 0-15
    const unsigned char *aad = (const unsigned char*)cipherBytes.constData() + 16;  // bytes 16-31
    int cipherLen = cipherBytes.size() - 32 - 16;
    if (cipherLen <= 0) return QByteArray();
    const unsigned char *cipher = (const unsigned char*)cipherBytes.constData() + 32;
    const unsigned char *tag    = (const unsigned char*)cipherBytes.constData() + 32 + cipherLen;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return QByteArray();

    const EVP_CIPHER *cipher_type = (key.size() == 16) ? EVP_aes_128_gcm()
                                  : (key.size() == 24) ? EVP_aes_192_gcm()
                                  :                      EVP_aes_256_gcm();
    EVP_DecryptInit_ex(ctx, cipher_type, NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL,
        (const unsigned char*)key.constData(), iv);
    // AAD
    int len = 0;
    EVP_DecryptUpdate(ctx, NULL, &len, aad, 16);
    // Decrypt
    QByteArray out(cipherLen + 16, '\0');
    EVP_DecryptUpdate(ctx, (unsigned char*)out.data(), &len,
        cipher, cipherLen);
    int outLen = len;
    // Set tag
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, (unsigned char*)out.data() + outLen, &len);
    EVP_CIPHER_CTX_free(ctx);
    if (ret <= 0) {
        qDebug() << "[Zalo WS] aesGcmDecrypt: EVP_DecryptFinal FAILED ret=" << ret
                 << "keyLen=" << key.size() << "cipherLen=" << cipherLen
                 << "tagHex=" << QByteArray((const char*)tag, 16).toHex();
        return QByteArray(); // tag mismatch
    }
    out.resize(outLen + len);
    return out;
}

QString ZaloService::aesDecryptBase64(const QString &keyStr, const QString &cipherB64)
{
    if (cipherB64.isEmpty()) return QString();
    QByteArray key    = resolveKey(keyStr);
    int keyBits = key.size() * 8;
    QString decoded   = QUrl::fromPercentEncoding(cipherB64.toUtf8());
    QByteArray cipher = QByteArray::fromBase64(decoded.toUtf8());
    if (cipher.isEmpty()) return QString();

    unsigned char iv[AES_BLOCK_SIZE];
    memset(iv, 0, AES_BLOCK_SIZE);

    QByteArray out(cipher.size(), '\0');
    AES_KEY k;
    AES_set_decrypt_key((const unsigned char*)key.constData(), keyBits, &k);
    AES_cbc_encrypt((const unsigned char*)cipher.constData(), (unsigned char*)out.data(), cipher.size(), &k, iv, AES_DECRYPT);

    if (!out.isEmpty()) {
        unsigned char pad = (unsigned char)out.at(out.size() - 1);
        if (pad > 0 && pad <= AES_BLOCK_SIZE) out.chop(pad);
    }
    return QString::fromUtf8(out);
}

QString ZaloService::md5Hex(const QString &input)
{
    return QCryptographicHash::hash(input.toUtf8(), QCryptographicHash::Md5).toHex().toLower();
}

QString ZaloService::md5Hex(const QByteArray &input)
{
    return QCryptographicHash::hash(input, QCryptographicHash::Md5).toHex().toLower();
}

QString ZaloService::randomHexString(int len)
{
    QString r;
    while (r.length() < len) r += QString::number((quint32)qrand(), 16);
    return r.left(len);
}

ZaloService::EncryptedParams ZaloService::buildEncryptedParams(const QVariantMap &data)
{
    EncryptedParams ep;
    ep.enc_ver = "v2";
    qDebug() << "[Zalo] buildEncryptedParams enc_ver=v2 API=" << API_VERSION;

    QString ts = data.contains("ts") ? data["ts"].toString()
                                     : QString::number(QDateTime::currentMSecsSinceEpoch());
    QString firstLaunchTime = QString::number(QDateTime::currentMSecsSinceEpoch());

    QString zcidMsg = QString("%1,%2,%3").arg(API_TYPE).arg(m_imei).arg(firstLaunchTime);
    ep.zcid     = aesEncryptHex(AES_FIXED_KEY, zcidMsg);
    ep.zcid_ext = randomHexString(8).toLower();

    QString n = md5Hex(ep.zcid_ext).toUpper();

    QStringList nEven;
    for (int i = 0; i < n.length(); i += 2) nEven << QString(n[i]);

    QStringList zEven, zOdd;
    for (int i = 0; i < ep.zcid.length(); ++i) {
        if (i % 2 == 0) zEven << QString(ep.zcid[i]);
        else             zOdd  << QString(ep.zcid[i]);
    }
    for (int i = 0, j = zOdd.size() - 1; i < j; ++i, --j) zOdd.swap(i, j);

    QStringList nEven8(nEven.mid(0, 8));
    QStringList zEven12(zEven.mid(0, 12));
    QStringList zOdd12(zOdd.mid(0, 12));
    ep.encryptKey = nEven8.join("") + zEven12.join("") + zOdd12.join("");

    ep.encryptedData = aesEncryptBase64_256(ep.encryptKey, QString::fromUtf8(mapToJson(data)));
    qDebug() << "[Zalo] buildEncryptedParams encryptKey=" << ep.encryptKey << "zcid_ext=" << ep.zcid_ext;
    return ep;
}

QString ZaloService::buildSignKey(const QString &type, const QVariantMap &params)
{
    QStringList keys = params.keys();
    keys.sort();
    QString a = "zsecure" + type;
    for (int i = 0; i < keys.size(); ++i) a += params[keys[i]].toString();
    return md5Hex(a);
}

QString ZaloService::generateIMEI()
{
    return generateUUIDv4() + "-" + md5Hex(m_userAgent);
}

QString ZaloService::generateUUIDv4()
{
    return QUuid::createUuid().toString().remove('{').remove('}').toLower();
}

QString ZaloService::generateRandomUserAgent()
{
    static const int chromeMajors[] = { 118, 119, 120, 121, 122, 123, 124, 125 };
    static const int majorCount = 8;

    // Windows versions
    static const char* winVersions[] = {
        "Windows NT 10.0; Win64; x64",
        "Windows NT 10.0; WOW64",
        "Windows NT 6.1; Win64; x64",
        "Windows NT 6.3; Win64; x64"
    };
    static const int winCount = 4;

    int major    = chromeMajors[qrand() % majorCount];
    int minor    = qrand() % 10;
    int build    = 4000 + qrand() % 2000;
    int patch    = qrand() % 150;
    QString win  = QString::fromLatin1(winVersions[qrand() % winCount]);

    return QString("Mozilla/5.0 (%1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%2.%3.%4.%5 Safari/537.36")
           .arg(win).arg(major).arg(minor).arg(build).arg(patch);
}

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
        qDebug() << "[Zalo] refreshSessionKey network error:" << reply->errorString();
        reply->deleteLater();
        m_loggedIn = true;
        emit loggedInChanged();
        m_listenTimer->start(8000);
        emit loginSuccess(m_uid, m_displayName);
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
                    if (c.isArray())  m_chatServiceUrl      = c.property(0).toString();
                    if (g.isArray())  m_groupServiceUrl     = g.property(0).toString();
                    if (p.isArray())  m_profileServiceUrl   = p.property(0).toString();
                    if (gp.isArray()) m_groupPollServiceUrl = gp.property(0).toString();
                    if (f.isArray())  m_friendServiceUrl    = f.property(0).toString();
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
    emit loginSuccess(m_uid, m_displayName);
}

void ZaloService::dbSaveMessage(const QVariantMap &msg, const QString &threadId)
{
    if (!m_db || threadId.isEmpty()) return;
    QString msgId = msg["msgId"].toString();
    if (msgId.isEmpty()) return;

    // Skip messages older than the last clear timestamp for this thread
    const char *sqlCheck = "SELECT clearedAt FROM cleared_threads WHERE threadId=?";
    sqlite3_stmt *chk = 0;
    if (sqlite3_prepare_v2(m_db, sqlCheck, -1, &chk, 0) == SQLITE_OK) {
        sqlite3_bind_text(chk, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(chk) == SQLITE_ROW) {
            qint64 clearedAt = QString::fromUtf8((const char*)sqlite3_column_text(chk, 0)).toLongLong();
            qint64 msgTs     = msg["ts"].toString().toLongLong();
            sqlite3_finalize(chk);
            if (msgTs <= clearedAt) return; // message predates or equals clear time
        } else {
            sqlite3_finalize(chk);
        }
    }

    const char *sql =
        "INSERT OR REPLACE INTO messages "
        "(msgId,threadId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1,  msgId.toUtf8().constData(),                       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,  threadId.toUtf8().constData(),                    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3,  msg["content"].toString().toUtf8().constData(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4,  msg["senderId"].toString().toUtf8().constData(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5,  msg["dName"].toString().toUtf8().constData(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6,  msg["ts"].toString().toUtf8().constData(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 7,  msg["isMine"].toBool() ? 1 : 0);
    sqlite3_bind_int (stmt, 8,  msg["isGroup"].toBool() ? 1 : 0);
    sqlite3_bind_int (stmt, 9,  msg["msgType"].toInt());
    sqlite3_bind_text(stmt, 10, msg["localImage"].toString().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QVariantList ZaloService::dbLoadMessages(const QString &threadId)
{
    QVariantList result;
    if (!m_db || threadId.isEmpty()) return result;

    const char *sql =
        "SELECT msgId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage "
        "FROM messages WHERE threadId=? "
        "ORDER BY CAST(ts AS INTEGER) ASC LIMIT 200;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m["msgId"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        m["content"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        m["senderId"]   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        m["dName"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        m["ts"]         = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        m["isMine"]     = (sqlite3_column_int(stmt, 5) == 1);
        m["isGroup"]    = (sqlite3_column_int(stmt, 6) == 1);
        m["msgType"]    = sqlite3_column_int(stmt, 7);
        m["localImage"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 8));
        result.append(m);
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] dbLoadMessages" << threadId << "rows:" << result.size();
    return result;
}
