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

// Login flows: QR login, cookie-based login, and the legacy multi-step
// (step1..step9) HTML/JS login fallback used when QR isn't available.

void ZaloService::startQRLogin()
{
    m_qrCancelled  = false;
    m_isAutoRenew  = false;
    m_loggedIn                 = false;
    m_isFetchingFriends        = false;
    m_isFetchingConversations  = false;
    m_loginEmitted             = false;
    m_lastFetchFriendsTime     = 0;
    m_lastFetchConvoTime       = 0;
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
    m_keepAliveTimer->stop();
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
    QVariantList qmA    = svcMap["quick_message"].toList();
    QVariantList boardA = svcMap["group_board"].toList();
    QVariantList reactA = svcMap["reaction"].toList();
    if (!chatA.isEmpty())  m_chatServiceUrl  = chatA[0].toString();
    if (!groupA.isEmpty()) m_groupServiceUrl = groupA[0].toString();
    if (!qmA.isEmpty())    m_quickMessageServiceUrl = qmA[0].toString();
    if (!boardA.isEmpty()) m_groupBoardServiceUrl = boardA[0].toString();
    if (!reactA.isEmpty()) m_reactionServiceUrl = reactA[0].toString();

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
    if (!m_loginEmitted) {
        m_loginEmitted = true;
        emit loginSuccess(m_uid, m_displayName);
    } else {
        emit sessionRefreshed();
    }
    m_listenTimer->start(8000);
    m_keepAliveTimer->start(KEEPALIVE_INTERVAL_MS);
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
    m_groupBoardServiceUrl.clear();
    m_friendServiceUrl.clear();
    m_fileServiceUrl.clear();
    m_quickMessageServiceUrl.clear();
    m_reactionServiceUrl.clear();

    QScriptValue svcMap = info.property("zpw_service_map_v3");
    if (svcMap.isObject()) {
        QScriptValue chatArr   = svcMap.property("chat");
        QScriptValue groupArr  = svcMap.property("group");
        QScriptValue profArr   = svcMap.property("profile");
        QScriptValue pollArr   = svcMap.property("group_poll");
        QScriptValue boardArr  = svcMap.property("group_board");
        QScriptValue friendArr = svcMap.property("friend");
        QScriptValue fileArr   = svcMap.property("file");
        QScriptValue qmArr     = svcMap.property("quick_message");
        QScriptValue reactArr  = svcMap.property("reaction");

        if (chatArr.isArray())   m_chatServiceUrl      = chatArr.property(0).toString();
        if (groupArr.isArray())  m_groupServiceUrl     = groupArr.property(0).toString();
        if (profArr.isArray())   m_profileServiceUrl   = profArr.property(0).toString();
        if (pollArr.isArray())   m_groupPollServiceUrl = pollArr.property(0).toString();
        if (boardArr.isArray())  m_groupBoardServiceUrl = boardArr.property(0).toString();
        if (friendArr.isArray()) m_friendServiceUrl    = friendArr.property(0).toString();
        if (fileArr.isArray())   m_fileServiceUrl      = fileArr.property(0).toString();
        if (qmArr.isArray())     m_quickMessageServiceUrl = qmArr.property(0).toString();
        if (reactArr.isArray())  m_reactionServiceUrl  = reactArr.property(0).toString();
    }

    // Extract WebSocket URLs
    m_zpwWsUrls.clear();
    // Xóa luôn m_wsUrls và index xoay vòng: connectWebSocket() chỉ re-seed
    // m_wsUrls từ m_zpwWsUrls khi m_wsUrls rỗng, nên nếu không xóa ở đây,
    // login mới sẽ vẫn giữ nguyên index host mà session TRƯỚC đã dùng dở.
    m_wsUrls.clear();
    m_wsUrlIndex = 0;
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
    qDebug() << "[Zalo] group_board:" << m_groupBoardServiceUrl;
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
    if (!m_loginEmitted) {
        m_loginEmitted = true;
        emit loginSuccess(m_uid, m_displayName);
    } else {
        emit sessionRefreshed();
    }
    m_listenTimer->start(8000);
    m_keepAliveTimer->start(KEEPALIVE_INTERVAL_MS);
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
