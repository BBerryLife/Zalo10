#include "core/contacts/ContactsCommon.hpp"

void ZaloService::fetchConversations(){
    if (!m_loggedIn) return;
    if (m_isFetchingConversations) {
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchConvoTime > 0 && (now - m_lastFetchConvoTime) < FETCH_COOLDOWN_MS) {
        return;
    }
    m_isFetchingConversations = true;

    QVariantMap qp;
    qp["zpw_ver"]  = QString::number(API_VERSION);
    qp["zpw_type"] = QString::number(API_TYPE);

    QString urlStr = buildRawUrl(m_groupServiceUrl + "/api/group/getlg/v4", qp);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchConvoDone()));

}

void ZaloService::onFetchConvoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Contacts] fetchConversations Network Error:" << reply->errorString();
    }

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.isEmpty()) {
        emit conversationsReady(QVariantList());
        m_isFetchingConversations = false;
        return;
    }

    // HTTP 429 (rate limit): the body is HTML, not JSON.
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qWarning() << "[Contacts] fetchConversations: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] fetchConvo error_code:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        // ec=600: zpw_sek expired, session cookies invalid; re-login required.
        if (ec == 600) {
            qWarning() << "[Contacts] fetchConvo: session expired (600), emitting sessionExpired";
            m_loggedIn = false;
            emit loggedInChanged();
            emit sessionExpired();
            return;
        }
        emit conversationsReady(QVariantList());
        return;
    }
    m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch();

    QVariantList threads;
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());

    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

    QVariantMap gridVerMap = inner["gridVerMap"].toMap();

    QStringList groupIds = gridVerMap.keys();

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

    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupDetailsDone()));
}

void ZaloService::onGroupDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qWarning() << "[Contacts] groupDetails: got 429, backing off";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qWarning() << "[Contacts] groupDetails outer error:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());

    QVariantMap outer = jsonToMap(dec.toUtf8());
    int ec2 = outer["error_code"].toInt();
    if (ec2 != 0) {
        qWarning() << "[Contacts] groupDetails inner error:" << ec2 << outer["error_message"].toString();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap inner;
    if (outer.contains("data") && outer["data"].type() == QVariant::Map)
        inner = outer["data"].toMap();
    else
        inner = outer;

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
            m_groupNames[gid] = gname;

            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
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
            m_groupNames[gid] = g["name"].toString();

            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
        }
    }

    if (!threads.isEmpty())
        emit conversationsReady(threads);
    m_isFetchingConversations = false;
    if (!threads.isEmpty())
        startGroupSync();
}
