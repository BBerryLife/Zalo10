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

// Groups are read in chunks of GROUP_CHUNK ids (most recently active first), one request at a time
// with a short pause, so a 3000-group account never sends one giant request. The list is shown
// from the DB cache straight away (cachedConversations) and replaced when the last chunk arrives.
static const int GROUP_CHUNK = 50;

void ZaloService::fetchGroupDetails(const QStringList &groupIds)
{
    if (groupIds.isEmpty()) return;
    feedsEnsureTables();

    QSet<QString> want;
    for (int i = 0; i < groupIds.size(); ++i) want.insert(groupIds[i]);
    QStringList ordered;
    QSet<QString> used;
    sqlite3_stmt *st = 0;
    if (m_db && sqlite3_prepare_v2(m_db,
            "SELECT threadId FROM messages WHERE isGroup=1 GROUP BY threadId "
            "ORDER BY MAX(CAST(ts AS INTEGER)) DESC", -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *t = sqlite3_column_text(st, 0);
            QString id = t ? QString::fromUtf8(reinterpret_cast<const char*>(t)) : QString();
            if (want.contains(id) && !used.contains(id)) { ordered << id; used.insert(id); }
        }
        sqlite3_finalize(st);
    }
    for (int i = 0; i < groupIds.size(); ++i)
        if (!used.contains(groupIds[i])) ordered << groupIds[i];

    m_groupHadCache = false;
    if (m_db && sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM group_cache", -1, &st, 0) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW) m_groupHadCache = sqlite3_column_int(st, 0) > 0;
        sqlite3_finalize(st);
    }

    m_groupQueue = ordered;
    m_groupPos = 0;
    m_groupRetry = 0;
    m_groupAccum.clear();
    m_groupPartialSent = false;
    requestGroupChunk();
}

void ZaloService::onGroupChunkNext()
{
    requestGroupChunk();
}

void ZaloService::requestGroupChunk()
{
    if (m_groupPos >= m_groupQueue.size()) { finishGroups(true); return; }

    QVariantMap gridVerMapObj;
    int end = qMin(m_groupQueue.size(), m_groupPos + GROUP_CHUNK);
    for (int i = m_groupPos; i < end; ++i)
        gridVerMapObj[m_groupQueue[i]] = 0;
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

// The group list as saved by the last complete fetch (empty before the first one).
QVariantList ZaloService::cachedConversations()
{
    QVariantList out;
    if (!m_db) return out;
    feedsEnsureTables();
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT groupId, name, avatar FROM group_cache ORDER BY name COLLATE NOCASE",
                           -1, &st, 0) != SQLITE_OK) return out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *a = sqlite3_column_text(st, 0);
        const unsigned char *b = sqlite3_column_text(st, 1);
        const unsigned char *c = sqlite3_column_text(st, 2);
        QVariantMap t;
        t["threadId"] = a ? QString::fromUtf8(reinterpret_cast<const char*>(a)) : QString();
        t["name"]     = b ? QString::fromUtf8(reinterpret_cast<const char*>(b)) : QString();
        t["isGroup"]  = true;
        t["avatar"]   = c ? QString::fromUtf8(reinterpret_cast<const char*>(c)) : QString();
        t["unread"]   = 0;
        out.append(t);
    }
    sqlite3_finalize(st);
    return out;
}

void ZaloService::saveGroupCache(const QVariantList &threads)
{
    if (!m_db || threads.isEmpty()) return;
    feedsEnsureTables();
    sqlite3_stmt *ins = 0;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO group_cache (groupId, name, avatar) VALUES (?,?,?)",
                           -1, &ins, 0) != SQLITE_OK) return;
    sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
    sqlite3_exec(m_db, "DELETE FROM group_cache;", 0, 0, 0);
    for (int i = 0; i < threads.size(); ++i) {
        QVariantMap t = threads[i].toMap();
        QByteArray id = t["threadId"].toString().toUtf8();
        QByteArray nm = t["name"].toString().toUtf8();
        QByteArray av = t["avatar"].toString().toUtf8();
        sqlite3_bind_text(ins, 1, id.constData(), id.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, nm.constData(), nm.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, av.constData(), av.size(), SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(m_db, "COMMIT", 0, 0, 0);
}

// complete=false: a chunk kept failing. Whatever was read is merged with the cached list so a bad
// network never makes groups vanish from the screen.
void ZaloService::finishGroups(bool complete)
{
    m_isFetchingConversations = false;
    QVariantList threads = m_groupAccum;
    m_groupAccum.clear();
    m_groupQueue.clear();
    m_groupPos = 0;

    if (!complete) {
        QSet<QString> have;
        for (int i = 0; i < threads.size(); ++i) have.insert(threads[i].toMap()["threadId"].toString());
        QVariantList cached = cachedConversations();
        for (int i = 0; i < cached.size(); ++i)
            if (!have.contains(cached[i].toMap()["threadId"].toString())) threads.append(cached[i]);
    }

    if (threads.isEmpty()) {
        emit conversationsReady(QVariantList());
        return;
    }
    if (complete) saveGroupCache(threads);
    emit conversationsReady(threads);
    startGroupSync();
}

void ZaloService::onGroupDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // 429 (HTML body) or an error: wait and retry the same chunk a few times, then give up.
    bool failed = false;
    QVariantMap root;
    QVariantMap outer;
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qWarning() << "[Contacts] groupDetails: got 429";
        failed = true;
    } else {
        root = jsonToMap(raw);
        int ec = root["error_code"].toInt();
        if (ec != 0) {
            qWarning() << "[Contacts] groupDetails outer error:" << ec << root["error_message"].toString();
            failed = true;
        } else {
            QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
            outer = jsonToMap(dec.toUtf8());
            int ec2 = outer["error_code"].toInt();
            if (ec2 != 0) {
                qWarning() << "[Contacts] groupDetails inner error:" << ec2 << outer["error_message"].toString();
                failed = true;
            }
        }
    }
    if (failed) {
        if (m_groupRetry < 3) {
            ++m_groupRetry;
            QTimer::singleShot(5000 * m_groupRetry, this, SLOT(onGroupChunkNext()));
            return;
        }
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        finishGroups(false);
        return;
    }
    m_groupRetry = 0;

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

    for (int i = 0; i < threads.size(); ++i) m_groupAccum.append(threads[i]);
    m_groupPos += GROUP_CHUNK;

    // No cache yet (first run): show the first chunk at once instead of waiting for all of them.
    if (!m_groupPartialSent && !m_groupHadCache && m_groupPos < m_groupQueue.size() && !m_groupAccum.isEmpty()) {
        m_groupPartialSent = true;
        emit conversationsReady(m_groupAccum);
    }

    if (m_groupPos >= m_groupQueue.size()) {
        finishGroups(true);
        return;
    }
    QTimer::singleShot(400, this, SLOT(onGroupChunkNext()));
}
