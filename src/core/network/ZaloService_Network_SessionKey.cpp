#include "core/network/NetworkCommon.hpp"

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
