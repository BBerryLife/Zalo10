#include "core/db/DbCommon.hpp"

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
    emit messageRecalled(threadId, msgId);
}

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
    emit messageDeletedLocally(threadId, msgId);
}

void ZaloService::hideLocalMessage(const QString &threadId, const QString &msgId)
{
    markMessageDeletedForMe(threadId, msgId);
}

bool ZaloService::isLocallyHidden(const QString &msgId) const
{
    return isMessageDeletedForMe(msgId);
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

    // Never resurrect "delete for me" messages: the server returns them on every resync.
    if (isMessageDeletedForMe(msgId)) {
        return;
    }

    const char *sqlCheck = "SELECT clearedAt FROM cleared_threads WHERE threadId=?";
    sqlite3_stmt *chk = 0;
    if (sqlite3_prepare_v2(m_db, sqlCheck, -1, &chk, 0) == SQLITE_OK) {
        sqlite3_bind_text(chk, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(chk) == SQLITE_ROW) {
            qint64 clearedAt = QString::fromUtf8((const char*)sqlite3_column_text(chk, 0)).toLongLong();
            qint64 msgTs     = msg["ts"].toString().toLongLong();
            sqlite3_finalize(chk);
            if (msgTs <= clearedAt) return;
        } else {
            sqlite3_finalize(chk);
        }
    }

    // UPDATE-then-INSERT, not INSERT OR REPLACE: a re-save must keep recalledOriginalContent,
    // and localImage/imgWidth/imgHeight keep their old values when the caller's map lacks them.
    // No ON CONFLICT DO UPDATE: the bundled SQLite is too old.
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
    return dbLoadMessagesPage(threadId, QString(), 200);
}

// Uses <= beforeTs so messages sharing a timestamp at a page boundary are not lost;
// QML dedups by msgId.
QVariantList ZaloService::dbLoadMessagesPage(const QString &threadId, const QString &beforeTs, int limit)
{
    QVariantList result;
    if (!m_db || threadId.isEmpty()) return result;
    if (limit < 1) limit = 1;
    qint64 before = beforeTs.toLongLong();
    if (before > 0 && before < 1000000000000LL) before *= 1000;

    const char *sqlPaged =
        "SELECT msgId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight,recalledOriginalContent,cliMsgId,"
        "quoteMsgId,quoteContent,quoteSenderName,quoteMsgType,quoteOwnerId FROM ("
        "SELECT msgId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight,recalledOriginalContent,cliMsgId,"
        "quoteMsgId,quoteContent,quoteSenderName,quoteMsgType,quoteOwnerId "
        "FROM messages WHERE threadId=? AND (?=0 OR (CASE WHEN CAST(ts AS INTEGER) < 1000000000000 "
        "THEN CAST(ts AS INTEGER) * 1000 ELSE CAST(ts AS INTEGER) END) <= ?) "
        "ORDER BY CAST(ts AS INTEGER) DESC, msgId DESC LIMIT ?"
        ") ORDER BY CAST(ts AS INTEGER) ASC, msgId ASC;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sqlPaged, -1, &stmt, 0) != SQLITE_OK) return result;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, before);
    sqlite3_bind_int64(stmt, 3, before);
    sqlite3_bind_int(stmt, 4, limit);
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
    return result;
}

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
        rec["ts"]   = (double)ts;
        forMsg[uid] = rec;
        byMsg[msgId] = forMsg;
    }
    sqlite3_finalize(stmt);
    return byMsg;
}

QVariantMap ZaloService::getThreadMessageStates(const QString &threadId)
{
    QVariantMap out;
    if (!m_db || threadId.isEmpty()) return out;
    const char *sql = "SELECT msgId,state FROM message_states WHERE threadId=?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return out;
    sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out[QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0))] = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool ZaloService::dbAdvanceMessageStates(const QString &threadId, int state,
                                         const QString &msgId, const QString &altMsgId)
{
    if (!m_db || threadId.isEmpty() || (msgId.isEmpty() && altMsgId.isEmpty())) return false;

    // Only our own messages: a "seen" event may be us reading the other side on another device.
    qint64 upToTs = -1;
    bool   found  = false;
    {
        const char *sql = "SELECT CAST(ts AS INTEGER), isMine FROM messages "
                          "WHERE threadId=? AND (msgId=? OR msgId=?) LIMIT 1;";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, altMsgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                found = (sqlite3_column_int(stmt, 1) == 1);
                upToTs = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
    }
    if (!found) {
        return false;
    }

    const char *sql =
        "INSERT OR REPLACE INTO message_states (msgId,threadId,state) "
        "SELECT m.msgId, m.threadId, ? FROM messages m "
        "WHERE m.threadId=? AND m.isMine=1 AND CAST(m.ts AS INTEGER) <= ? "
        "AND COALESCE((SELECT s.state FROM message_states s WHERE s.msgId=m.msgId), 1) < ?;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) return false;
    sqlite3_bind_int  (stmt, 1, state);
    sqlite3_bind_text (stmt, 2, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, upToTs);
    sqlite3_bind_int  (stmt, 4, state);
    sqlite3_step(stmt);
    int changed = sqlite3_changes(m_db);
    sqlite3_finalize(stmt);
    return changed > 0;
}

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
    return result;
}

QVariantMap ZaloService::getThreadLastMessages() const
{
    QVariantMap result;
    if (!m_db) return result;

    const char *sql =
        "SELECT threadId, content, dName, isMine, msgType, recalledOriginalContent, "
        "MAX(CAST(ts AS INTEGER)) AS maxTs, msgId "
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
        QString lastMsgId = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 7));
        m["msgId"] = lastMsgId;
        int state = 1;
        if (m["isMine"].toBool() && !lastMsgId.isEmpty()) {
            sqlite3_stmt *st = 0;
            if (sqlite3_prepare_v2(m_db, "SELECT state FROM message_states WHERE msgId=?;", -1, &st, 0) == SQLITE_OK) {
                sqlite3_bind_text(st, 1, lastMsgId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(st) == SQLITE_ROW) state = sqlite3_column_int(st, 0);
                sqlite3_finalize(st);
            }
        }
        m["state"] = state;
        result[threadId] = m;
    }
    sqlite3_finalize(stmt);
    return result;
}
