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

// Lưu trữ local dùng SQLite: cache tin nhắn, preset quick-message, và các
// tiện ích export/import/clear cache dữ liệu.

void ZaloService::markMessageRecalled(const QString &threadId, const QString &msgId)
{
    if (msgId.isEmpty()) return;
    if (m_db) {
        const char *sql =
            "UPDATE messages SET "
            "recalledOriginalContent = CASE WHEN (recalledOriginalContent IS NULL OR recalledOriginalContent = '') "
            "THEN content ELSE recalledOriginalContent END, "
            "content='', msgType=99 WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    qDebug() << "[Zalo] message recalled msgId=" << msgId << "thread=" << threadId;
    emit messageRecalled(threadId, msgId);
}

// "Xóa cho tôi" (chat.delete), khác với markMessageRecalled's "chat.undo"
// (thu hồi). Hai cái này không được dùng chung logic:
//   - Thu hồi: tin nhắn mất ở CẢ 2 phía, hiện placeholder thay thế — nên
//     là UPDATE + tag.
//   - Xóa cho tôi: tin nhắn chỉ biến mất ở màn hình của MÌNH, không có
//     placeholder, phía kia không bị ảnh hưởng gì — nên là DELETE cứng.
// Caller (xử lý chat.delete ở ZaloService_WebSocket.cpp) chỉ nên gọi hàm
// này khi chính mình là người thực hiện xóa.
void ZaloService::markMessageDeletedForMe(const QString &threadId, const QString &msgId)
{
    if (msgId.isEmpty()) return;
    if (m_db) {
        const char *sql = "DELETE FROM messages WHERE msgId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        // Tombstone msgId này để không bị dbSaveMessage() vô tình insert lại
        // sau này (vd lần resync cmd=510 tiếp theo khi mở lại thread). Nếu
        // không có bước này, "xóa cho tôi" chỉ xóa được row local, còn
        // server vẫn trả tin đó về ở lần resync kế, khiến nó hiện lại.
        const char *sqlTomb = "INSERT OR REPLACE INTO deleted_messages (msgId, threadId, deletedAt) VALUES (?, ?, ?)";
        sqlite3_stmt *stmtTomb = 0;
        if (sqlite3_prepare_v2(m_db, sqlTomb, -1, &stmtTomb, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmtTomb, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmtTomb, 2, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmtTomb, 3, QString::number(QDateTime::currentMSecsSinceEpoch()).toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmtTomb);
            sqlite3_finalize(stmtTomb);
        }
    }
    qDebug() << "[Zalo] message deleted-for-me msgId=" << msgId << "thread=" << threadId << "(tombstoned)";
    emit messageDeletedLocally(threadId, msgId);
}

bool ZaloService::isMessageDeletedForMe(const QString &msgId) const
{
    if (!m_db || msgId.isEmpty()) return false;
    const char *sql = "SELECT 1 FROM deleted_messages WHERE msgId=?";
    sqlite3_stmt *stmt = 0;
    bool found = false;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    return found;
}

void ZaloService::dbSaveMessage(const QVariantMap &msg, const QString &threadId)
{
    if (!m_db || threadId.isEmpty()) return;
    QString msgId = msg["msgId"].toString();
    if (msgId.isEmpty()) return;

    // Không bao giờ hồi sinh tin nhắn user đã xóa cứng qua "xóa cho tôi":
    // server vẫn trả nó về ở mỗi lần resync, nên phải check tombstone trước.
    if (isMessageDeletedForMe(msgId)) {
        qDebug() << "[Zalo] dbSaveMessage: skipping tombstoned (deleted-for-me) msgId=" << msgId;
        return;
    }

    // Skip messages older than the last clear timestamp for this thread
    const char *sqlCheck = "SELECT clearedAt FROM cleared_threads WHERE threadId=?";
    sqlite3_stmt *chk = 0;
    if (sqlite3_prepare_v2(m_db, sqlCheck, -1, &chk, 0) == SQLITE_OK) {
        sqlite3_bind_text(chk, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(chk) == SQLITE_ROW) {
            qint64 clearedAt = QString::fromUtf8((const char*)sqlite3_column_text(chk, 0)).toLongLong();
            qint64 msgTs     = msg["ts"].toString().toLongLong();
            sqlite3_finalize(chk);
            if (msgTs <= clearedAt) return; // message predates or equals clear time
        } else {
            sqlite3_finalize(chk);
        }
    }

    // UPDATE-then-INSERT (thay vì INSERT OR REPLACE) để re-save tin nhắn
    // đã có (vd re-fetch/re-sync) không xóa mất recalledOriginalContent —
    // INSERT OR REPLACE xóa rồi tạo lại row, reset mọi cột không nằm trong
    // VALUES. Cũng tránh dùng "ON CONFLICT ... DO UPDATE" (SQLite 3.24+) vì
    // bản SQLite bundle cũ trên BB10 có thể không hỗ trợ.
    //
    // localImage/imgWidth/imgHeight dùng CASE WHEN để giữ giá trị DB cũ khi
    // map của caller không có chúng (rỗng/0): hầu hết chỗ gọi dbSaveMessage()
    // build map từ payload WS thô, không có các field này — chúng được set
    // riêng và bất đồng bộ bởi downloadImageMessage()/onImageMsgDownloaded().
    // Không có guard này, re-save cùng msgId (vd resync cmd=510 patch tin
    // thành "recalled" rồi save lại) sẽ vô tình xóa path cache ảnh đã tải.
    const char *sqlUpdate =
        "UPDATE messages SET threadId=?,content=?,senderId=?,dName=?,ts=?,"
        "isMine=?,isGroup=?,msgType=?,"
        "localImage = CASE WHEN ?='' THEN localImage ELSE ? END,"
        "imgWidth   = CASE WHEN ?=0  THEN imgWidth   ELSE ? END,"
        "imgHeight  = CASE WHEN ?=0  THEN imgHeight  ELSE ? END,"
        "cliMsgId   = CASE WHEN ?='' THEN cliMsgId   ELSE ? END,"
        "quoteMsgId      = CASE WHEN ?='' THEN quoteMsgId      ELSE ? END,"
        "quoteContent    = CASE WHEN ?='' THEN quoteContent    ELSE ? END,"
        "quoteSenderName = CASE WHEN ?='' THEN quoteSenderName ELSE ? END,"
        "quoteMsgType    = CASE WHEN ?=0  THEN quoteMsgType    ELSE ? END,"
        "quoteOwnerId    = CASE WHEN ?='' THEN quoteOwnerId    ELSE ? END "
        "WHERE msgId=?";
    sqlite3_stmt *upd = 0;
    bool updated = false;
    if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &upd, 0) == SQLITE_OK) {
        QByteArray localImageUtf8 = msg["localImage"].toString().toUtf8();
        QByteArray cliMsgIdUtf8   = msg["cliMsgId"].toString().toUtf8();
        QByteArray quoteMsgIdUtf8   = msg["quoteMsgId"].toString().toUtf8();
        QByteArray quoteContentUtf8 = msg["quoteContent"].toString().toUtf8();
        QByteArray quoteSenderUtf8  = msg["quoteSenderName"].toString().toUtf8();
        QByteArray quoteOwnerUtf8   = msg["quoteOwnerId"].toString().toUtf8();
        int imgWidthVal  = msg["imgWidth"].toInt();
        int imgHeightVal = msg["imgHeight"].toInt();
        int quoteMsgTypeVal = msg["quoteMsgType"].toInt();
        sqlite3_bind_text(upd, 1,  threadId.toUtf8().constData(),                    -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 2,  msg["content"].toString().toUtf8().constData(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 3,  msg["senderId"].toString().toUtf8().constData(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 4,  msg["dName"].toString().toUtf8().constData(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 5,  msg["ts"].toString().toUtf8().constData(),        -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (upd, 6,  msg["isMine"].toBool() ? 1 : 0);
        sqlite3_bind_int (upd, 7,  msg["isGroup"].toBool() ? 1 : 0);
        sqlite3_bind_int (upd, 8,  msg["msgType"].toInt());
        sqlite3_bind_text(upd, 9,  localImageUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 10, localImageUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (upd, 11, imgWidthVal);
        sqlite3_bind_int (upd, 12, imgWidthVal);
        sqlite3_bind_int (upd, 13, imgHeightVal);
        sqlite3_bind_int (upd, 14, imgHeightVal);
        sqlite3_bind_text(upd, 15, cliMsgIdUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 16, cliMsgIdUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 17, quoteMsgIdUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 18, quoteMsgIdUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 19, quoteContentUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 20, quoteContentUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 21, quoteSenderUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 22, quoteSenderUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (upd, 23, quoteMsgTypeVal);
        sqlite3_bind_int (upd, 24, quoteMsgTypeVal);
        sqlite3_bind_text(upd, 25, quoteOwnerUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 26, quoteOwnerUtf8.constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 27, msgId.toUtf8().constData(),                        -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
        updated = sqlite3_changes(m_db) > 0;
        sqlite3_finalize(upd);
    }
    if (updated) return;

    const char *sql =
        "INSERT INTO messages "
        "(msgId,threadId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight,cliMsgId,"
        "quoteMsgId,quoteContent,quoteSenderName,quoteMsgType,quoteOwnerId) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1,  msgId.toUtf8().constData(),                       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,  threadId.toUtf8().constData(),                    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3,  msg["content"].toString().toUtf8().constData(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4,  msg["senderId"].toString().toUtf8().constData(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5,  msg["dName"].toString().toUtf8().constData(),     -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6,  msg["ts"].toString().toUtf8().constData(),        -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 7,  msg["isMine"].toBool() ? 1 : 0);
    sqlite3_bind_int (stmt, 8,  msg["isGroup"].toBool() ? 1 : 0);
    sqlite3_bind_int (stmt, 9,  msg["msgType"].toInt());
    sqlite3_bind_text(stmt, 10, msg["localImage"].toString().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 11, msg["imgWidth"].toInt());
    sqlite3_bind_int (stmt, 12, msg["imgHeight"].toInt());
    sqlite3_bind_text(stmt, 13, msg["cliMsgId"].toString().toUtf8().constData(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, msg["quoteMsgId"].toString().toUtf8().constData(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, msg["quoteContent"].toString().toUtf8().constData(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, msg["quoteSenderName"].toString().toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 17, msg["quoteMsgType"].toInt());
    sqlite3_bind_text(stmt, 18, msg["quoteOwnerId"].toString().toUtf8().constData(),    -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QVariantList ZaloService::dbLoadMessages(const QString &threadId)
{
    QVariantList result;
    if (!m_db || threadId.isEmpty()) return result;

    const char *sql =
        "SELECT msgId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight,recalledOriginalContent,cliMsgId,"
        "quoteMsgId,quoteContent,quoteSenderName,quoteMsgType,quoteOwnerId "
        "FROM messages WHERE threadId=? "
        "ORDER BY CAST(ts AS INTEGER) ASC LIMIT 200;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m["msgId"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        m["content"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        m["senderId"]   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        m["dName"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        m["ts"]         = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        m["isMine"]     = (sqlite3_column_int(stmt, 5) == 1);
        m["isGroup"]    = (sqlite3_column_int(stmt, 6) == 1);
        m["msgType"]    = sqlite3_column_int(stmt, 7);
        m["localImage"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 8));
        m["imgWidth"]   = sqlite3_column_int(stmt, 9);
        m["imgHeight"]  = sqlite3_column_int(stmt, 10);
        m["recalledOriginalContent"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 11));
        m["cliMsgId"]   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 12));
        m["quoteMsgId"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 13));
        m["quoteContent"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 14));
        m["quoteSenderName"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 15));
        m["quoteMsgType"]    = sqlite3_column_int(stmt, 16);
        m["quoteOwnerId"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 17));
        result.append(m);
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] dbLoadMessages" << threadId << "rows:" << result.size();
    return result;
}

// Load hết reaction cho toàn bộ tin nhắn trong 1 thread, trong 1 query
// duy nhất — {msgId: {uid: {icon, ts}}} — thay vì 1 query/tin nhắn.
QVariantMap ZaloService::dbLoadThreadReactions(const QString &threadId)
{
    QVariantMap byMsg;
    if (!m_db || threadId.isEmpty()) return byMsg;

    const char *sql = "SELECT msgId,uid,icon,ts FROM message_reactions WHERE threadId=?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return byMsg;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QString msgId = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        QString uid   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        QString icon  = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        qint64  ts    = sqlite3_column_int64(stmt, 3);

        QVariantMap forMsg = byMsg.value(msgId).toMap();
        QVariantMap rec;
        rec["icon"] = icon;
        rec["ts"]   = (double)ts; // QML Date.now()-style millis, kept as a plain number
        forMsg[uid] = rec;
        byMsg[msgId] = forMsg;
    }
    sqlite3_finalize(stmt);
    return byMsg;
}

// Thêm/thay reaction của (uid,msgId). Dùng cho cả action optimistic của
// mình (reactMessage()) lẫn reaction đến từ WS (handler cmd 501/521).
void ZaloService::dbSaveReaction(const QString &threadId, const QString &msgId, const QString &uid, const QString &icon)
{
    if (!m_db || msgId.isEmpty() || uid.isEmpty() || icon.isEmpty()) return;
    const char *sql =
        "INSERT OR REPLACE INTO message_reactions (msgId,threadId,uid,icon,ts) VALUES (?,?,?,?,?);";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, uid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, icon.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, QDateTime::currentMSecsSinceEpoch());
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Removes (uid,msgId)'s reaction entirely (un-react).
void ZaloService::dbRemoveReaction(const QString &msgId, const QString &uid)
{
    if (!m_db || msgId.isEmpty() || uid.isEmpty()) return;
    const char *sql = "DELETE FROM message_reactions WHERE msgId=? AND uid=?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, msgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, uid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Toàn bộ row mọi thread, cũ nhất trước — dùng cho exportData(). Khác
// dbLoadMessages() (theo thread, giới hạn 200 row cho hiển thị), không
// LIMIT vì export cần là bản backup đầy đủ.
QVariantList ZaloService::dbLoadAllMessages() const
{
    QVariantList result;
    if (!m_db) return result;

    const char *sql =
        "SELECT msgId,threadId,content,senderId,dName,ts,isMine,isGroup,msgType,"
        "localImage,imgWidth,imgHeight,recalledOriginalContent "
        "FROM messages ORDER BY threadId, CAST(ts AS INTEGER) ASC;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m["msgId"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        m["threadId"]   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        m["content"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        m["senderId"]   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 3));
        m["dName"]      = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 4));
        m["ts"]         = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 5));
        m["isMine"]     = (sqlite3_column_int(stmt, 6) == 1);
        m["isGroup"]    = (sqlite3_column_int(stmt, 7) == 1);
        m["msgType"]    = sqlite3_column_int(stmt, 8);
        m["localImage"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 9));
        m["imgWidth"]   = sqlite3_column_int(stmt, 10);
        m["imgHeight"]  = sqlite3_column_int(stmt, 11);
        m["recalledOriginalContent"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 12));
        result.append(m);
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] dbLoadAllMessages rows:" << result.size();
    return result;
}

// Tin nhắn mới nhất đã lưu local của mỗi thread, dùng để khôi phục preview
// "last message" trên chat list khi mở app (API friends/conversations của
// server không kèm field last-message, nên không có bước này thì
// ChatsTab.qml/GroupsTab.qml sẽ hiện "chưa có tin nhắn" mỗi lần restart dù
// đầy đủ lịch sử đã có trong SQLite).
//
// Dựa vào hành vi bare-column của SQLite: khi query có đúng 1 aggregate
// MIN()/MAX() cùng GROUP BY, các cột không aggregate sẽ lấy từ đúng row
// tạo ra giá trị MIN/MAX đó — nên 1 lượt query cho luôn content/dName/
// isMine/msgType của row có ts lớn nhất mỗi threadId. Không cần window
// function hay subquery tương quan, để tương thích bản SQLite cũ trên BB10.
QVariantMap ZaloService::getThreadLastMessages() const
{
    QVariantMap result;
    if (!m_db) return result;

    const char *sql =
        "SELECT threadId, content, dName, isMine, msgType, recalledOriginalContent, "
        "MAX(CAST(ts AS INTEGER)) AS maxTs "
        "FROM messages GROUP BY threadId;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QString threadId = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        if (threadId.isEmpty()) continue;
        QVariantMap m;
        m["content"]                 = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        m["dName"]                   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        m["isMine"]                  = (sqlite3_column_int(stmt, 3) == 1);
        m["msgType"]                 = sqlite3_column_int(stmt, 4);
        m["recalledOriginalContent"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 5));
        m["ts"]                      = QString::number((qint64)sqlite3_column_int64(stmt, 6));
        result[threadId] = m;
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] getThreadLastMessages: found last message for" << result.size() << "threads";
    return result;
}

// Prefix filename Zalo10 ghi vào "/tmp" cho ảnh cache (xem comment
// clearCache() vì sao dùng literal "/tmp" thay vì QDir::tempPath()).
// Gom về 1 chỗ để exportData() và clearCache() thống nhất định nghĩa
// file cache là gì.
QStringList ZaloService::cacheFilePatterns() const
{
    QStringList pats;
    pats << "avatar_" << "msgthumb_" << "msgimg_" << "zalo_img_" << "qr.png";
    return pats;
}

// ---------------------------------------------------------------------------
// Persistent avatar cache (avatar_meta table)
// ---------------------------------------------------------------------------
// threadId -> (urlHash, localPath). Đây là cách avatar cache sống qua được
// restart app và logout/login: m_avatarCache (RAM) được build lại từ bảng
// này lúc khởi động, và mỗi lần downloadAvatar() gọi đều check bảng này
// trước khi gọi network. Chỉ clearCache() mới xóa bảng này, nên avatar của
// 1 người chỉ tải lại khi thực sự đổi, dù app restart bao nhiêu lần.

bool ZaloService::avatarMetaLookup(const QString &threadId, QString &urlHashOut, QString &localPathOut) const
{
    if (!m_db || threadId.isEmpty()) return false;
    const char *sql = "SELECT urlHash, localPath FROM avatar_meta WHERE threadId=?";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        urlHashOut   = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        localPathOut = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

void ZaloService::avatarMetaUpsert(const QString &threadId, const QString &urlHash, const QString &localPath)
{
    if (!m_db || threadId.isEmpty()) return;
    // UPDATE-then-INSERT, cùng pattern với dbSaveMessage(), tránh dùng
    // "ON CONFLICT ... DO UPDATE" (SQLite bundle cũ trên BB10 có thể không hỗ trợ).
    const char *sqlUpdate = "UPDATE avatar_meta SET urlHash=?, localPath=?, updatedAt=? WHERE threadId=?";
    sqlite3_stmt *upd = 0;
    bool updated = false;
    QString now = QString::number(QDateTime::currentMSecsSinceEpoch());
    if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &upd, 0) == SQLITE_OK) {
        sqlite3_bind_text(upd, 1, urlHash.toUtf8().constData(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 2, localPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 3, now.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 4, threadId.toUtf8().constData(),  -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
        updated = sqlite3_changes(m_db) > 0;
        sqlite3_finalize(upd);
    }
    if (updated) return;

    const char *sqlInsert = "INSERT INTO avatar_meta (threadId, urlHash, localPath, updatedAt) VALUES (?,?,?,?)";
    sqlite3_stmt *ins = 0;
    if (sqlite3_prepare_v2(m_db, sqlInsert, -1, &ins, 0) == SQLITE_OK) {
        sqlite3_bind_text(ins, 1, threadId.toUtf8().constData(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 2, urlHash.toUtf8().constData(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 3, localPath.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 4, now.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_finalize(ins);
    }
}

// Chạy 1 lần trong constructor của ZaloService. Duyệt hết avatar_meta,
// log số avatar cache còn valid trên đĩa vs số bị stale (file bị xóa, vd
// do "Clear Cache" hoặc OS dọn tmp). Quyết định dùng lại avatar thật sự
// nằm ở downloadAvatar() qua avatarMetaLookup() + QFile::exists() mỗi lần
// gọi, nên không cần preload hết row vào m_avatarCache ở đây.
void ZaloService::loadAvatarCacheFromDb()
{
    if (!m_db) return;
    const char *sql = "SELECT localPath FROM avatar_meta";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return;
    int warmed = 0, stale = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QString localPath = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
        QString fsPath = localPath;
        if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
        if (!fsPath.isEmpty() && QFile::exists(fsPath)) warmed++;
        else stale++;
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] loadAvatarCacheFromDb:" << warmed << "cached avatars still on disk,"
             << stale << "stale entries (will be re-fetched on demand)";
}

// ---------------------------------------------------------------------------
// Quick Messages ("/command" canned replies). Global list, not tied to any
// one conversation — same SQLite DB used for message history (m_db).
// ---------------------------------------------------------------------------

QVariantList ZaloService::getQuickMessages() const
{
    QVariantList result;
    if (!m_db) return result;

    const char *sql = "SELECT id,name,content FROM quick_messages ORDER BY name COLLATE NOCASE ASC;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        QVariantMap m;
        m["id"]      = sqlite3_column_int(stmt, 0);
        m["name"]    = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        m["content"] = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        result.append(m);
    }
    sqlite3_finalize(stmt);
    return result;
}

int ZaloService::addQuickMessage(const QString &name, const QString &content)
{
    QString trimmedName = name.trimmed();
    QString trimmedContent = content.trimmed();
    if (!m_db || trimmedName.isEmpty() || trimmedContent.isEmpty()) return -1;

    const char *sql = "INSERT INTO quick_messages (name,content,createdAt) VALUES (?,?,?);";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, trimmedName.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trimmedContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, QString::number(QDateTime::currentMSecsSinceEpoch()).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    // SQLITE_CONSTRAINT nghĩa là idx_qm_name từ chối tên trùng (không phân biệt hoa/thường).
    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(m_db);
}

bool ZaloService::updateQuickMessage(int id, const QString &name, const QString &content)
{
    QString trimmedName = name.trimmed();
    QString trimmedContent = content.trimmed();
    if (!m_db || id < 0 || trimmedName.isEmpty() || trimmedContent.isEmpty()) return false;

    const char *sql = "UPDATE quick_messages SET name=?, content=? WHERE id=?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, trimmedName.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, trimmedContent.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 3, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return false; // trùng tên với row khác
    return sqlite3_changes(m_db) > 0;
}

bool ZaloService::deleteQuickMessage(int id)
{
    if (!m_db || id < 0) return false;

    const char *sql = "DELETE FROM quick_messages WHERE id=?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    sqlite3_step(stmt);
    bool changed = sqlite3_changes(m_db) > 0;
    sqlite3_finalize(stmt);
    return changed;
}

// ---------------------------------------------------------------------------
// Data export / import / cache management (Settings → mục "Data")
// ---------------------------------------------------------------------------
//
// File output của exportData(), dưới <destDir>/zalo10/:
//   zalo10_data_<timestamp>.json            — luôn được ghi
//   zalo10_data_<timestamp>_images/         — chỉ khi includeImages=true,
//                                              và chỉ nếu còn ít nhất 1 ảnh
//                                              tồn tại
//
// Log debug đi vào thư mục "log" riêng — <destDir>/zalo10/log/ — tách khỏi
// data export để user gửi log báo lỗi không vô tình gửi kèm cả lịch sử tin nhắn.
QVariantMap ZaloService::exportData(const QString &destDir)
{
    QVariantMap out;
    out["success"] = false;

    QString zaloDir = destDir + "/zalo10";
    QDir dir;
    if (!dir.exists(zaloDir) && !dir.mkpath(zaloDir)) {
        out["error"] = "Could not create export folder";
        return out;
    }

    QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString baseName = "zalo10_data_" + stamp;
    QString jsonPath = zaloDir + "/" + baseName + ".json";

    // Export chỉ có text: không copy file ảnh. Ảnh nằm ở cache tmp, sẽ mất
    // ngay khi app update/reinstall, nên bundle theo cũng vô nghĩa. Tin
    // nhắn có ảnh giữ nguyên text (nếu có), thêm tag "[Photo]" để đọc vẫn
    // tự nhiên; bỏ hẳn localImage/imgWidth/imgHeight.
    QVariantList rawMessages = dbLoadAllMessages();
    QVariantList messages;
    messages.reserve(rawMessages.size());
    for (int i = 0; i < rawMessages.size(); ++i) {
        QVariantMap m = rawMessages[i].toMap();
        if (!m["localImage"].toString().isEmpty()) {
            QString content = m["content"].toString();
            m["content"] = content.isEmpty() ? "[Photo]" : (content + " [Photo]");
        }
        m.remove("localImage");
        m.remove("imgWidth");
        m.remove("imgHeight");
        messages.append(m);
    }
    QVariantList quickMsgs = getQuickMessages();

    QVariantMap root;
    root["app"]           = "Zalo10";
    root["exportVersion"] = 1;
    root["exportedAt"]    = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["messages"]      = messages;
    root["quickMessages"] = quickMsgs;

    QByteArray json = variantToJsonPretty(root);
    QFile jf(jsonPath);
    if (!jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out["error"] = "Could not write export file";
        return out;
    }
    jf.write(json);
    jf.close();

    qDebug() << "[Zalo] exportData: wrote" << messages.size() << "messages,"
              << quickMsgs.size() << "quick messages ->" << jsonPath;

    out["success"]     = true;
    out["path"]         = jsonPath;
    out["messageCount"] = messages.size();
    return out;
}

QVariantMap ZaloService::importData(const QString &jsonFilePath)
{
    QVariantMap out;
    out["success"] = false;
    out["importedMessages"] = 0;
    out["skippedMessages"] = 0;
    out["importedQuickMessages"] = 0;
    out["skippedQuickMessages"] = 0;

    QString path = jsonFilePath;
    if (path.startsWith("file://")) path = path.mid(7);

    QFile jf(path);
    if (!jf.open(QIODevice::ReadOnly)) {
        out["error"] = "Could not open file";
        return out;
    }
    QByteArray raw = jf.readAll();
    jf.close();

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty() || !root.contains("messages")) {
        out["error"] = "File is not a valid Zalo10 export";
        return out;
    }

    if (!m_db) {
        out["error"] = "Local database unavailable";
        return out;
    }

    // msgId đã có sẵn — bỏ qua trùng ("data cũ luôn thắng").
    QSet<QString> existingIds;
    {
        const char *sql = "SELECT msgId FROM messages;";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW)
                existingIds.insert(QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0)));
            sqlite3_finalize(stmt);
        }
    }

    int imported = 0, skipped = 0;
    QVariantList messages = root["messages"].toList();
    for (int i = 0; i < messages.size(); ++i) {
        QVariantMap m = messages[i].toMap();
        QString msgId = m["msgId"].toString();
        QString threadId = m["threadId"].toString();
        if (msgId.isEmpty() || threadId.isEmpty()) { skipped++; continue; }
        if (existingIds.contains(msgId)) { skipped++; continue; }

        // exportData() không bundle file ảnh (chỉ export text), nên row
        // import không có localImage; tin nhắn có ảnh sẽ tới đây dạng text
        // thường kết thúc bằng "[Photo]".
        const char *sql =
            "INSERT INTO messages "
            "(msgId,threadId,content,senderId,dName,ts,isMine,isGroup,msgType,"
            "localImage,imgWidth,imgHeight,recalledOriginalContent) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1,  msgId.toUtf8().constData(),                          -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2,  threadId.toUtf8().constData(),                       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3,  m["content"].toString().toUtf8().constData(),        -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4,  m["senderId"].toString().toUtf8().constData(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 5,  m["dName"].toString().toUtf8().constData(),          -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6,  m["ts"].toString().toUtf8().constData(),             -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (stmt, 7,  m["isMine"].toBool() ? 1 : 0);
            sqlite3_bind_int (stmt, 8,  m["isGroup"].toBool() ? 1 : 0);
            sqlite3_bind_int (stmt, 9,  m["msgType"].toInt());
            sqlite3_bind_text(stmt, 10, "",                                                  -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (stmt, 11, 0);
            sqlite3_bind_int (stmt, 12, 0);
            sqlite3_bind_text(stmt, 13, m["recalledOriginalContent"].toString().toUtf8().constData(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_DONE && sqlite3_changes(m_db) > 0) {
                imported++;
                existingIds.insert(msgId);
            } else {
                skipped++;
            }
        } else {
            skipped++;
        }
    }

    // Quick messages: match by name (case-insensitive), same "existing wins" rule.
    int qmImported = 0, qmSkipped = 0;
    QVariantList quickMsgs = root.value("quickMessages").toList();
    QSet<QString> existingNames;
    {
        const char *sql = "SELECT name FROM quick_messages;";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW)
                existingNames.insert(QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0)).toLower());
            sqlite3_finalize(stmt);
        }
    }
    for (int i = 0; i < quickMsgs.size(); ++i) {
        QVariantMap qm = quickMsgs[i].toMap();
        QString name = qm["name"].toString().trimmed();
        QString content = qm["content"].toString().trimmed();
        if (name.isEmpty() || content.isEmpty()) { qmSkipped++; continue; }
        if (existingNames.contains(name.toLower())) { qmSkipped++; continue; }

        int newId = addQuickMessage(name, content);
        if (newId >= 0) { qmImported++; existingNames.insert(name.toLower()); }
        else qmSkipped++;
    }

    qDebug() << "[Zalo] importData:" << imported << "messages imported," << skipped << "skipped,"
              << qmImported << "quick messages imported," << qmSkipped << "skipped";

    out["success"] = true;
    out["importedMessages"] = imported;
    out["skippedMessages"] = skipped;
    out["importedQuickMessages"] = qmImported;
    out["skippedQuickMessages"] = qmSkipped;
    return out;
}

int ZaloService::clearCache()
{
    // 1. Xóa hết file ảnh cache app ghi vào "/tmp/".
    //    LƯU Ý: quét literal "/tmp", không phải QDir::tempPath() — trên máy
    //    BB10 này 2 cái khác nhau (QDir::tempPath() là scratch dir bị xóa
    //    mỗi lần restart app; "/tmp" mới là chỗ persistent mọi nơi ghi cache
    //    trong app thực sự dùng). Sau khi xóa, localImage path lưu trong
    //    bảng messages sẽ trỏ tới file không còn tồn tại — đây là chủ ý
    //    (exportData() đã check QFile::exists() trước khi bundle ảnh).
    QDir tmp("/tmp");
    QStringList patterns = cacheFilePatterns();
    int deleted = 0;
    QStringList allFiles = tmp.entryList(QDir::Files);
    for (int i = 0; i < allFiles.size(); ++i) {
        const QString &fname = allFiles[i];
        bool match = false;
        for (int p = 0; p < patterns.size(); ++p) {
            if (fname.startsWith(patterns[p]) || fname == patterns[p]) { match = true; break; }
        }
        if (match && tmp.remove(fname)) deleted++;
    }

    // Xóa luôn map/set avatar cache trong RAM để app không tiếp tục trả về
    // file:// path vừa bị xóa.
    m_avatarCache.clear();
    m_pendingAvatars.clear();
    m_pendingAvatarWaiters.clear();

    // 2. Xóa sạch lịch sử tin nhắn local. cleared_threads và quick_messages
    //    giữ nguyên, đây là settings/preferences của user, không phải cache.
    //    avatar_meta THÌ bị xóa — vì file avatar của nó vừa bị xóa ở bước
    //    trên, giữ lại row cũ sẽ khiến downloadAvatar() tưởng nhầm file
    //    (đã mất) vẫn còn khớp với avatar URL hiện tại.
    if (m_db) {
        sqlite3_exec(m_db, "DELETE FROM messages;", 0, 0, 0);
        sqlite3_exec(m_db, "DELETE FROM avatar_meta;", 0, 0, 0);
        sqlite3_exec(m_db, "VACUUM;", 0, 0, 0);
    }

    qDebug() << "[Zalo] clearCache: deleted" << deleted << "cache files, wiped message history";
    return deleted;
}

