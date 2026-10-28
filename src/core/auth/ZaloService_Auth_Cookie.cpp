#include "core/auth/AuthCommon.hpp"

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
    m_userAgent = generateRandomUserAgent();
    m_imei = generateIMEI();
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

    if (!token.trimmed().isEmpty()) {
        m_externalToken = token.trimmed();
    } else {
        m_externalToken.clear();
    }

    cookieStep1_getZaloLoginInfo();
}

void ZaloService::cookieStep1_getZaloLoginInfo()
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

    QVariantMap root = jsonToMap(raw);
    int errCode = root["error_code"].toInt();
    qWarning() << "[Auth] cookie login failed, error_code=" << errCode << root["error_message"].toString();

    QVariantMap info;
    QVariant dataVariant = root["data"];
    if (dataVariant.type() == QVariant::Map) {
        info = dataVariant.toMap();
    } else {
        QString encData = dataVariant.toString();
        if (errCode != 0) {
            emit loginFailed(QString("Cookie loi %1 - %2").arg(errCode).arg(root["error_message"].toString()));
            return;
        }
        if (encData.isEmpty()) {
            emit loginFailed("Cookie: response data rong");
            return;
        }
        QString decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
        QVariantMap root2 = jsonToMap(decrypted.toUtf8());
        info = root2["data"].toMap();
        if (info.isEmpty()) info = root2;
    }

    m_secretKey   = info["zpw_enk"].toString();
    m_uid         = info["uid"].toString();
    m_displayName = info["display_name"].toString();

    if (!m_externalToken.isEmpty()) {
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
