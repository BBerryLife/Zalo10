#include "core/auth/AuthCommon.hpp"

void ZaloService::step8_getZaloLoginInfo()
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
    connect(reply, SIGNAL(finished()), this, SLOT(onStep8Done()));
}

void ZaloService::onStep8Done()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Auth] login network error:" << reply->errorString()
                   << "host=" << reply->url().host() << "code=" << (int)reply->error();
        if (m_isAutoRenew && int(reply->error()) < 200 && m_renewRetry < 12) {
            ++m_renewRetry;
            reply->deleteLater();
            QTimer::singleShot(5000, this, SLOT(retryRefreshSessionKey()));
            return;
        }
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QScriptEngine outerEng;
    outerEng.globalObject().setProperty("__raw", QString::fromUtf8(raw));
    outerEng.evaluate("var __o = null; try { __o = JSON.parse(__raw); } catch(e) { __o = null; }");
    QScriptValue outerObj = outerEng.globalObject().property("__o");

    if (!outerObj.isValid() || outerObj.isNull()) {
        qWarning() << "[Auth] login: invalid response";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8 parse failed");
        return;
    }

    int errCode8 = outerObj.property("error_code").toInt32();
    qWarning() << "[Auth] login error_code=" << errCode8 << outerObj.property("error_message").toString();

    QString encData = outerObj.property("data").toString();

    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
    }

    if (decrypted.isEmpty()) {
        qWarning() << "[Auth] login: empty decrypted response";
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
        qWarning() << "[Auth] login: invalid session info";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8: invalid info object");
        return;
    }

    m_secretKey   = info.property("zpw_enk").toString();
    m_uid         = info.property("uid").toString();
    m_displayName = info.property("display_name").toString();

    if (m_secretKey.isEmpty() || m_uid.isEmpty()) {
        qWarning() << "[Auth] login: missing session key or uid";
        if (m_isAutoRenew) emit sessionExpired();
        else               emit loginFailed("Step8: missing key/uid");
        return;
    }

    m_chatServiceUrl.clear();
    m_groupServiceUrl.clear();
    m_profileServiceUrl.clear();
    m_groupPollServiceUrl.clear();
    m_groupBoardServiceUrl.clear();
    m_friendServiceUrl.clear();
    m_fileServiceUrl.clear();
    m_quickMessageServiceUrl.clear();
    m_reactionServiceUrl.clear();
    m_conversationServiceUrl.clear();

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
        QScriptValue convArr   = svcMap.property("conversation");

        if (chatArr.isArray())   m_chatServiceUrl      = chatArr.property(0).toString();
        if (groupArr.isArray())  m_groupServiceUrl     = groupArr.property(0).toString();
        if (profArr.isArray())   m_profileServiceUrl   = profArr.property(0).toString();
        if (pollArr.isArray())   m_groupPollServiceUrl = pollArr.property(0).toString();
        if (boardArr.isArray())  m_groupBoardServiceUrl = boardArr.property(0).toString();
        if (friendArr.isArray()) m_friendServiceUrl    = friendArr.property(0).toString();
        if (fileArr.isArray())   m_fileServiceUrl      = fileArr.property(0).toString();
        if (qmArr.isArray())     m_quickMessageServiceUrl = qmArr.property(0).toString();
        if (reactArr.isArray())  m_reactionServiceUrl  = reactArr.property(0).toString();
        if (convArr.isArray())   m_conversationServiceUrl = convArr.property(0).toString();
    }

    m_zpwWsUrls.clear();
    // Also reset m_wsUrls and the rotation index: connectWebSocket() only re-seeds when
    // m_wsUrls is empty, so a new login would reuse the previous session's host index.
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

    step9_getServerInfo();
}

void ZaloService::step9_getServerInfo()
{
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
    m_renewRetry = 0;
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
