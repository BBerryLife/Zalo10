#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"

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

// Đồng bộ hội thoại + lịch sử tin nhắn từ cloud lúc khởi động, và danh sách
// hội thoại đã ghim. zca-js chưa có đồng bộ đầy đủ (issue #129), nhưng WS
// cmd=510 trả luồng tin DM toàn cục (lastId dùng chung cho mọi thread) và
// HTTP group/history trả lịch sử từng nhóm — ghép lại đủ để lấp SQLite, từ
// đó danh sách Chats/Groups tự có preview + thứ tự đúng (ChatsTab/GroupsTab
// dựng từ getThreadLastMessages()).

namespace {
const int SYNC_MAX_PAGES     = 80;     // trần số trang DM mỗi lần chạy (cursor được lưu, lần sau chạy tiếp)
const int SYNC_MAX_MSGS      = 8000;   // trần số tin DM mỗi lần chạy
const int SYNC_PAGE_DELAY_MS = 400;    // nghỉ giữa 2 trang để không dồn máy BB10 / server
const int SYNC_TIMEOUT_MS    = 15000;  // không thấy response trong khoảng này -> gửi lại
const int SYNC_RESTART_GAP_MS = 120000; // không chạy lại quá dày khi WS reconnect liên tục
const int GROUP_SYNC_COUNT   = 100;    // số tin xin cho mỗi nhóm
const int GROUP_SYNC_DELAY_MS = 250;
}

// ─── Hội thoại đã ghim ──────────────────────────────────────────────────

void ZaloService::fetchPinConversations()
{
    if (!m_loggedIn) return;
    if (m_conversationServiceUrl.isEmpty()) {
        qDebug() << "[Zalo] fetchPinConversations: no zpwServiceMap.conversation url (re-login to refresh it)";
        return;
    }

    // GET + params AES trong query, giống zca-js getPinConversations
    QVariantMap innerParams;
    innerParams["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_conversationServiceUrl + "/api/pinconvers/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchPinConversations GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onPinConversationsDone()));
}

void ZaloService::onPinConversationsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchPinConversations raw (first200):" << raw.left(200);
    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchPinConversations:" << root["error_message"].toString();
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchPinConversations decrypted (first300):" << dec.left(300);
    QVariantMap outer = jsonToMap(dec.toUtf8());

    QVariantList convs;
    if (outer["data"].type() == QVariant::Map)
        convs = outer["data"].toMap()["conversations"].toList();
    if (convs.isEmpty())
        convs = outer["conversations"].toList();

    // Chưa xác nhận id ghim có tiền tố u/g (kiểu "u123"/"g456") hay không, nên
    // giữ cả dạng gốc lẫn dạng đã bỏ tiền tố — so khớp với threadId được cả 2.
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
    qDebug() << "[Zalo] pinned conversations:" << convs.size() << "ids:" << m_pinnedIds.size();
    emit pinnedChanged();
}

QStringList ZaloService::pinnedThreadIds() const
{
    QStringList out;
    QSet<QString>::const_iterator it = m_pinnedIds.constBegin();
    for (; it != m_pinnedIds.constEnd(); ++it) out << *it;
    return out;
}

// ─── Đồng bộ DM (WS cmd=510, luồng toàn cục) ────────────────────────────

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
        qDebug() << "[Zalo Sync] WS not ready, DM sync skipped";
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sync.lastStartMs > 0 && (now - m_sync.lastStartMs) < SYNC_RESTART_GAP_MS) {
        qDebug() << "[Zalo Sync] ran" << (now - m_sync.lastStartMs) << "ms ago, skipping";
        return;
    }
    m_sync.lastStartMs = now;

    // Cursor = msgId lớn nhất đã đồng bộ (lastId của cmd=510 = "lấy các tin sau mốc
    // này"). Lần đầu là "0" -> toàn bộ lịch sử; các lần sau chỉ lấy phần mới.
    // Nếu DB local trống (xóa dữ liệu) mà cursor còn thì bỏ cursor để lấy lại từ đầu.
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
    qDebug() << "[Zalo Sync] DM sync start, cursor=" << m_sync.cursor << "dbEmpty=" << dbEmpty;
    sendSyncPage();
}

void ZaloService::sendSyncPage()
{
    if (!m_sync.active) return;
    if (!m_wsConnected || !m_webSocket) {
        finishDmSync("websocket not connected");
        return;
    }
    // Giống zca-js requestOldMessages(): first:true + lastId, KHÔNG có toid
    // -> luồng DM của mọi thread (khác fetchMessages() theo từng thread).
    QString req = QString("{\"first\":true,\"lastId\":\"%1\",\"preIds\":[]}").arg(m_sync.cursor);
    m_sync.awaiting = true;
    sendWsRequest(510, 1, req);
    if (m_sync.watchdog) m_sync.watchdog->start(SYNC_TIMEOUT_MS);
    qDebug() << "[Zalo Sync] DM page request #" << (m_sync.pages + 1) << "lastId=" << m_sync.cursor;
}

// Hoãn 1 nhịp sau handshake để request cmd=510 theo thread (nếu user đang mở chat)
// đi trước, rồi mới chạy đồng bộ nền.
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
        qDebug() << "[Zalo Sync] no response in" << SYNC_TIMEOUT_MS << "ms, retry" << m_sync.retries;
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

    // Chia batch theo thread (đối phương của cuộc trò chuyện), giữ nguyên thứ tự.
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

    // 1 transaction cho cả trang: mỗi dbSaveMessage() là UPDATE+INSERT riêng, commit
    // từng dòng trên flash của BB10 sẽ rất chậm.
    bool tx = (m_db && sqlite3_exec(m_db, "BEGIN", 0, 0, 0) == SQLITE_OK);
    QMap<QString, QVariantList>::const_iterator it = byThread.constBegin();
    for (; it != byThread.constEnd(); ++it) {
        processOldDmBatch(it.value(), it.key(), true);
        m_sync.threads.insert(it.key());
    }
    if (tx) sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

    m_sync.totalMsgs += rawMsgs.size();
    qint64 curNum = m_sync.cursor.toLongLong();
    qDebug() << "[Zalo Sync] DM page" << m_sync.pages << "msgs=" << rawMsgs.size()
             << "threads=" << byThread.size() << "new=" << fresh
             << "minId=" << minId << "maxId=" << maxId << "cursor=" << m_sync.cursor;

    if (rawMsgs.isEmpty()) {
        finishDmSync("caught up (empty page)");
        return;
    }
    if (maxId <= curNum) {
        // Không có tin nào mới hơn cursor -> server không tiến thêm; dừng để khỏi lặp vô hạn.
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
    qDebug() << "[Zalo Sync] DM sync done:" << why << "pages=" << m_sync.pages
             << "msgs=" << m_sync.totalMsgs << "threads=" << m_sync.threads.size();
    int total = m_sync.totalMsgs;
    m_sync.active   = false;
    m_sync.awaiting = false;
    if (m_sync.watchdog) m_sync.watchdog->stop();
    if (total > 0) emit historySynced(total);
    startGroupSync();
}

// ─── Đồng bộ nhóm (HTTP group/history, lần lượt từng nhóm) ──────────────

void ZaloService::startGroupSync()
{
    // Chạy sau pha DM (hoặc ngay khi danh sách nhóm sẵn sàng nếu DM sync không
    // chạy), mỗi lần khởi động đúng 1 lượt.
    if (m_sync.active || m_sync.groupActive || m_sync.groupRan || !m_loggedIn) return;
    if (m_groupNames.isEmpty()) return;  // chưa có danh sách nhóm; fetchConversations() xong sẽ gọi lại

    m_sync.groupQueue.clear();
    QMap<QString, QString>::const_iterator it = m_groupNames.constBegin();
    for (; it != m_groupNames.constEnd(); ++it) m_sync.groupQueue << it.key();
    m_sync.groupActive = true;
    m_sync.groupDone   = 0;
    m_sync.groupMsgs   = 0;
    qDebug() << "[Zalo Sync] group sync start, groups=" << m_sync.groupQueue.size();
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
    reply->setProperty("syncGroup", true);   // onFetchMsgDone(): chế độ im lặng + nối tiếp hàng đợi
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchMsgDone()));
}

void ZaloService::finishGroupSync()
{
    qDebug() << "[Zalo Sync] group sync done: groups=" << m_sync.groupDone
             << "msgs=" << m_sync.groupMsgs;
    int total = m_sync.groupMsgs;
    m_sync.groupActive = false;
    m_sync.groupRan    = true;
    m_sync.groupQueue.clear();
    if (total > 0) emit historySynced(total);
}
