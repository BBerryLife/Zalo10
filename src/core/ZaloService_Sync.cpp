#include "core/ZaloService.hpp"
#include "utils/ZaloServiceUtils.hpp"

#include <QSettings>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QDateTime>
#include <QStringList>
#include <QTimer>
#include <QDebug>
#include <sqlite3.h>

namespace {
const int SYNC_MAX_PAGES     = 80;
const int SYNC_MAX_MSGS      = 8000;
const int SYNC_PAGE_DELAY_MS = 400;
const int SYNC_TIMEOUT_MS    = 15000;
const int SYNC_RESTART_GAP_MS = 120000;
const int GROUP_SYNC_COUNT   = 100;
const int GROUP_SYNC_DELAY_MS = 250;
}

void ZaloService::fetchPinConversations()
{
    if (!m_loggedIn) return;
    if (m_conversationServiceUrl.isEmpty()) {
        qDebug() << "[Sync] fetchPinConversations: no zpwServiceMap.conversation url (re-login to refresh it)";
        return;
    }

    QVariantMap innerParams;
    innerParams["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_conversationServiceUrl + "/api/pinconvers/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onPinConversationsDone()));
}

void ZaloService::onPinConversationsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qWarning() << "[Sync] fetchPinConversations:" << root["error_message"].toString();
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    QVariantMap outer = jsonToMap(dec.toUtf8());

    QVariantList convs;
    if (outer["data"].type() == QVariant::Map)
        convs = outer["data"].toMap()["conversations"].toList();
    if (convs.isEmpty())
        convs = outer["conversations"].toList();

    QSet<QString> ids;
    for (int i = 0; i < convs.size(); ++i) {
        QString c = convs[i].toString();
        if (c.isEmpty()) continue;
        ids.insert(c);
        if (c.size() > 1 && (c[0] == QChar('u') || c[0] == QChar('g')
                             || c[0] == QChar('U') || c[0] == QChar('G'))) {
            bool allDigits = true;
            for (int k = 1; k < c.size(); ++k) {
                if (!c[k].isDigit()) { allDigits = false; break; }
            }
            if (allDigits) ids.insert(c.mid(1));
        }
    }
    m_pinnedIds = ids;
    emit pinnedChanged();
}

QStringList ZaloService::pinnedThreadIds() const
{
    QStringList out;
    QSet<QString>::const_iterator it = m_pinnedIds.constBegin();
    for (; it != m_pinnedIds.constEnd(); ++it) out << *it;
    return out;
}

bool ZaloService::dbMessageExists(const QString &msgId) const
{
    if (!m_db || msgId.isEmpty()) return false;
    sqlite3_stmt *stmt = 0;
    bool found = false;
    if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM messages WHERE msgId=? LIMIT 1", -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    return found;
}

void ZaloService::startHistorySync()
{
    if (!m_loggedIn || m_sync.active) return;
    if (!m_wsConnected || !m_webSocket || m_wsCipherKey.isEmpty()) {
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sync.lastStartMs > 0 && (now - m_sync.lastStartMs) < SYNC_RESTART_GAP_MS) {
        return;
    }
    m_sync.lastStartMs = now;

    QSettings s("BerryLife", "Zalo10");
    QString cursor = s.value("sync/dmCursor_" + m_uid, "0").toString();
    bool dbEmpty = true;
    if (m_db) {
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM messages LIMIT 1", -1, &stmt, 0) == SQLITE_OK) {
            dbEmpty = (sqlite3_step(stmt) != SQLITE_ROW);
            sqlite3_finalize(stmt);
        }
    }
    if (dbEmpty) cursor = "0";

    m_sync.active    = true;
    m_sync.awaiting  = false;
    m_sync.pages     = 0;
    m_sync.totalMsgs = 0;
    m_sync.retries   = 0;
    m_sync.cursor    = cursor;
    m_sync.threads.clear();
    m_sync.groupRan  = false;

    if (!m_sync.watchdog) {
        m_sync.watchdog = new QTimer(this);
        m_sync.watchdog->setSingleShot(true);
        connect(m_sync.watchdog, SIGNAL(timeout()), this, SLOT(onSyncWatchdog()));
    }
    sendSyncPage();
}

void ZaloService::sendSyncPage()
{
    if (!m_sync.active) return;
    if (!m_wsConnected || !m_webSocket) {
        finishDmSync("websocket not connected");
        return;
    }
    QString req = QString("{\"first\":true,\"lastId\":\"%1\",\"preIds\":[]}").arg(m_sync.cursor);
    m_sync.awaiting = true;
    sendWsRequest(510, 1, req);
    if (m_sync.watchdog) m_sync.watchdog->start(SYNC_TIMEOUT_MS);
}

void ZaloService::onSyncStartDeferred()
{
    startHistorySync();
}

void ZaloService::onSyncNextPage()
{
    if (m_sync.active && !m_sync.awaiting) sendSyncPage();
}

void ZaloService::onSyncWatchdog()
{
    if (!m_sync.active || !m_sync.awaiting) return;
    if (m_sync.retries < 2 && m_wsConnected) {
        m_sync.retries++;
        qDebug() << "[Sync] no response in" << SYNC_TIMEOUT_MS << "ms, retry" << m_sync.retries;
        sendSyncPage();
        return;
    }
    finishDmSync("timeout");
}

void ZaloService::handleSyncPage(const QVariantList &rawMsgs)
{
    m_sync.awaiting = false;
    if (m_sync.watchdog) m_sync.watchdog->stop();
    m_sync.retries = 0;
    m_sync.pages++;

    QMap<QString, QVariantList> byThread;
    qint64 maxId = 0, minId = 0;
    int fresh = 0;
    for (int i = 0; i < rawMsgs.size(); ++i) {
        QVariantMap m = rawMsgs[i].toMap();
        QString msgId = m["msgId"].toString();
        qint64 num = msgId.toLongLong();
        if (num > maxId) maxId = num;
        if (num > 0 && (minId == 0 || num < minId)) minId = num;

        QString from = m["uidFrom"].toString();
        QString to   = m["idTo"].toString();
        if (from == "0") from = m_uid;
        if (to   == "0") to   = m_uid;
        QString tid = (from == m_uid) ? to : from;
        if (tid.isEmpty()) continue;
        if (!dbMessageExists(msgId)) fresh++;
        byThread[tid].append(m);
    }

    bool tx = (m_db && sqlite3_exec(m_db, "BEGIN", 0, 0, 0) == SQLITE_OK);
    QMap<QString, QVariantList>::const_iterator it = byThread.constBegin();
    for (; it != byThread.constEnd(); ++it) {
        processOldDmBatch(it.value(), it.key(), true);
        m_sync.threads.insert(it.key());
    }
    if (tx) sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

    m_sync.totalMsgs += rawMsgs.size();
    qint64 curNum = m_sync.cursor.toLongLong();

    if (rawMsgs.isEmpty()) {
        finishDmSync("caught up (empty page)");
        return;
    }
    if (maxId <= curNum) {
        finishDmSync("no forward progress");
        return;
    }
    m_sync.cursor = QString::number(maxId);
    QSettings s("BerryLife", "Zalo10");
    s.setValue("sync/dmCursor_" + m_uid, m_sync.cursor);

    if (m_sync.pages >= SYNC_MAX_PAGES || m_sync.totalMsgs >= SYNC_MAX_MSGS) {
        finishDmSync("per-run cap reached (will resume next launch)");
        return;
    }
    QTimer::singleShot(SYNC_PAGE_DELAY_MS, this, SLOT(onSyncNextPage()));
}

void ZaloService::finishDmSync(const QString &why)
{
    qDebug() << "[Sync] DM sync done:" << why << "pages=" << m_sync.pages
             << "msgs=" << m_sync.totalMsgs << "threads=" << m_sync.threads.size();
    int total = m_sync.totalMsgs;
    m_sync.active   = false;
    m_sync.awaiting = false;
    if (m_sync.watchdog) m_sync.watchdog->stop();
    if (total > 0) emit historySynced(total);
    startGroupSync();
}

void ZaloService::startGroupSync()
{
    if (m_sync.active || m_sync.groupActive || m_sync.groupRan || !m_loggedIn) return;
    if (m_groupNames.isEmpty()) return;

    m_sync.groupQueue.clear();
    QMap<QString, QString>::const_iterator it = m_groupNames.constBegin();
    for (; it != m_groupNames.constEnd(); ++it) m_sync.groupQueue << it.key();
    m_sync.groupActive = true;
    m_sync.groupDone   = 0;
    m_sync.groupMsgs   = 0;
    onSyncGroupNext();
}

void ZaloService::onSyncGroupNext()
{
    if (!m_sync.groupActive) return;
    if (!m_loggedIn || m_sync.groupQueue.isEmpty()) {
        finishGroupSync();
        return;
    }
    QString gid = m_sync.groupQueue.takeFirst();
    m_sync.groupDone++;
    fetchGroupHistorySync(gid);
}

void ZaloService::fetchGroupHistorySync(const QString &groupId)
{
    QVariantMap innerParams;
    innerParams["grid"]  = groupId;
    innerParams["count"] = GROUP_SYNC_COUNT;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_groupServiceUrl + "/api/group/history"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("threadId",  groupId);
    reply->setProperty("isGroup",   true);
    reply->setProperty("syncGroup", true);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDone()));
}

void ZaloService::finishGroupSync()
{
    qDebug() << "[Sync] group sync done: groups=" << m_sync.groupDone
             << "msgs=" << m_sync.groupMsgs;
    int total = m_sync.groupMsgs;
    m_sync.groupActive = false;
    m_sync.groupRan    = true;
    m_sync.groupQueue.clear();
    if (total > 0) emit historySynced(total);
}
