#include "core/auth/AuthCommon.hpp"

void ZaloService::step1_loadLoginPage()
{
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

    step2_getLoginInfo();
}

void ZaloService::step2_getLoginInfo()
{
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
        qWarning() << "[Auth] onQRImageFetched: network error:" << reply->errorString();
        emit qrCodeReady("", m_qrCode);
        reply->deleteLater();
        return;
    }

    QByteArray imgData = reply->readAll();
    reply->deleteLater();

    if (imgData.isEmpty()) {
        emit qrCodeReady("", m_qrCode);
        return;
    }

    QString tempPath = QDir::tempPath() + "/qr.png";
    QFile imgFile(tempPath);
    if (imgFile.open(QIODevice::WriteOnly)) {
        imgFile.write(imgData);
        imgFile.close();
        emit qrCodeReady("file://" + tempPath, m_qrCode);
    } else {
        qWarning() << "[Auth] onQRImageFetched: cannot write file";
        emit qrCodeReady("", m_qrCode);
    }
}

void ZaloService::step5_waitingScan()
{
    if (m_qrCancelled) return;
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

    QVariantMap root = jsonToMap(raw);
    int errorCode    = root["error_code"].toInt();

    if      (errorCode == 8)   { step6_waitingConfirm(); }
    else if (errorCode == -13) { emit loginFailed("Tu choi xac nhan"); }
    else if (errorCode == 0)   { m_qrExpireTimer->stop(); step7_checkSession(); }
    else { emit loginFailed(QString("Confirm error: %1").arg(errorCode)); }
}

void ZaloService::step7_checkSession()
{
    if (ZJson::nativeMode()) {
        qDebug() << "[Auth] session check skipped (headless service)";
        emit sessionExpired();
        return;
    }

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

        QString redirStr = redirectUrl.toString();
        QString redirReferer = redirStr.contains("jr.zaloapp.com") ? "https://id.zalo.me/"
                             : redirStr.contains("jr.chat.zalo.me") ? "https://jr.zaloapp.com/"
                             : "https://id.zalo.me/";
        QNetworkRequest redirReq = buildRequest(redirStr, redirReferer);
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
