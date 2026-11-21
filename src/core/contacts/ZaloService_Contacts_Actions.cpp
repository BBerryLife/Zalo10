#include "core/contacts/ContactsCommon.hpp"

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

    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", true);
    connect(reply, SIGNAL(finished()), this, SLOT(onAcceptFriendDone()));
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, false, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.remove(uid);
    emit unblockUserDone(uid, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok && m_db && !tid.isEmpty()) {
        const char *sqlDel = "DELETE FROM messages WHERE threadId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sqlDel, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        QString clearedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
        const char *sqlClear =
            "INSERT OR REPLACE INTO cleared_threads (threadId, clearedAt) VALUES (?,?)";
        if (sqlite3_prepare_v2(m_db, sqlClear, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, clearedAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    emit clearHistoryDone(tid, ok);
}

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
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit leaveGroupDone(gid, ok);
}

// ---- Group management (zca-js createGroup / addUserToGroup / changeGroupName) ----

static QVariantList idList(const QStringList &ids)
{
    QVariantList l;
    for (int i = 0; i < ids.size(); ++i) l.append(ids[i]);
    return l;
}

static QVariantList typeList(int n)
{
    QVariantList l;
    for (int i = 0; i < n; ++i) l.append(-1);
    return l;
}

void ZaloService::createGroup(const QString &name, const QStringList &memberIds)
{
    if (!m_loggedIn) { emit createGroupDone(false, QString(), "Not logged in"); return; }
    if (memberIds.isEmpty()) { emit createGroupDone(false, QString(), "Pick at least one member"); return; }

    const QString nm = name.trimmed();
    QVariantMap p;
    p["clientId"]    = (qint64)QDateTime::currentMSecsSinceEpoch();
    p["gname"]       = nm.isEmpty() ? QString::number(QDateTime::currentMSecsSinceEpoch()) : nm;
    p["gdesc"]       = "__ZNULL__";   // zca-js sends a real null here; swapped below
    p["members"]     = idList(memberIds);
    p["membersTypes"] = typeList(memberIds.size());
    p["nameChanged"] = nm.isEmpty() ? 0 : 1;
    p["createLink"]  = 1;
    p["clientLang"]  = m_language;
    p["imei"]        = m_imei;
    p["zsource"]     = 601;

    QByteArray json = variantToJsonCompact(p);
    json.replace("\"__ZNULL__\"", "null");
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(json));

    // zca-js: POST with the params in the query string and no body.
    QString urlStr = m_groupServiceUrl + "/api/group/create/v2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, QByteArray());
    reply->setProperty("op", "create");
    reply->setProperty("groupName", nm);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupManageDone()));
}

void ZaloService::addGroupMembers(const QString &groupId, const QStringList &memberIds)
{
    if (!m_loggedIn) { emit addGroupMembersDone(groupId, false, 0, "Not logged in"); return; }
    if (groupId.isEmpty() || memberIds.isEmpty()) { emit addGroupMembersDone(groupId, false, 0, "Nobody to add"); return; }

    QVariantMap p;
    p["grid"]        = groupId;
    p["members"]     = idList(memberIds);
    p["memberTypes"] = typeList(memberIds.size());
    p["imei"]        = m_imei;
    p["clientLang"]  = m_language;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(p)));

    QString urlStr = m_groupServiceUrl + "/api/group/invite/v2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, "params=" + QUrl::toPercentEncoding(encParams));
    reply->setProperty("op", "add");
    reply->setProperty("groupId", groupId);
    reply->setProperty("count", memberIds.size());
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupManageDone()));
}

void ZaloService::changeGroupName(const QString &groupId, const QString &name)
{
    if (!m_loggedIn) { emit changeGroupNameDone(groupId, false, "Not logged in"); return; }
    QString nm = name.trimmed();
    if (groupId.isEmpty() || nm.isEmpty()) { emit changeGroupNameDone(groupId, false, "Enter a group name"); return; }

    QVariantMap p;
    p["grid"] = groupId;
    p["gname"] = nm;
    p["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(variantToJsonCompact(p)));

    QString urlStr = m_groupServiceUrl + "/api/group/updateinfo"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *reply = m_manager->post(req, "params=" + QUrl::toPercentEncoding(encParams));
    reply->setProperty("op", "rename");
    reply->setProperty("groupId", groupId);
    reply->setProperty("groupName", nm);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupManageDone()));
}

// One slot for every call above plus changeGroupAvatar() and updateProfileBio() (Profile.cpp): the
// reply carries its "op". Success needs the outer error_code, and the inner one when there is one.
void ZaloService::onGroupManageDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    const QString op   = reply->property("op").toString();
    const QString gid  = reply->property("groupId").toString();
    const QString gnm  = reply->property("groupName").toString();
    const int   asked  = reply->property("count").toInt();
    const bool  netOk  = (reply->error() == QNetworkReply::NoError);
    const QString netErr = reply->errorString();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    bool ok = false;
    QString err;
    QVariantMap data;
    if (!netOk) {
        err = netErr;
    } else if (raw.isEmpty() || raw.contains("<html")) {
        err = "Zalo is busy, try again in a moment";   // 429 comes back as an HTML page
    } else {
        QVariantMap root = jsonToMap(raw);
        if (!root.contains("error_code")) {
            err = "Unexpected reply from Zalo";
        } else if (root["error_code"].toInt() != 0) {
            err = root["error_message"].toString();
            if (err.isEmpty()) err = "Zalo refused the request (" + QString::number(root["error_code"].toInt()) + ")";
        } else {
            QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
            QVariantMap inner = dec.isEmpty() ? QVariantMap() : jsonToMap(dec.toUtf8());
            if (inner.contains("error_code") && inner["error_code"].toInt() != 0) {
                err = inner["error_message"].toString();
                if (err.isEmpty()) err = "Zalo refused the request (" + QString::number(inner["error_code"].toInt()) + ")";
            } else {
                ok = true;
                data = inner["data"].toMap();
                if (data.isEmpty()) data = inner;
            }
        }
    }
    if (!ok) qWarning() << "[Group] op" << op << "failed:" << err;

    if (op == "create") {
        QString newId = data["groupId"].toString();
        if (ok && newId.isEmpty()) { ok = false; err = "Zalo did not return the new group"; }
        if (ok && !gnm.isEmpty()) m_groupNames[newId] = gnm;
        emit createGroupDone(ok, ok ? newId : QString(), err);
    } else if (op == "add") {
        int failed = data["errorMembers"].toList().size();
        if (ok && failed > asked) failed = asked;
        emit addGroupMembersDone(gid, ok, failed, err);
    } else if (op == "rename") {
        if (ok && !gid.isEmpty()) m_groupNames[gid] = gnm;
        emit changeGroupNameDone(gid, ok, err);
    } else if (op == "gavatar") {
        emit changeGroupAvatarDone(gid, ok, err);
    } else if (op == "bio") {
        emit selfBioChanged(ok, err);
    }
}
