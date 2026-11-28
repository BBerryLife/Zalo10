#include "core/phonesync/PhoneSyncCommon.hpp"

// Manual matching for phone-sync conversations that could not be mapped automatically.
// onPhoneSyncTick() keeps their part files in psPendingDir() with a summary in phone_sync_pending_meta;
// the user picks the friend or group they belong to (settings/PhoneSyncMatchPage.qml), phoneSyncAssign()
// stores the mapping in phone_sync_alias (so later syncs match it automatically) and imports the rows.

namespace {

bool psNameLess(const QVariant &a, const QVariant &b)
{
    return a.toMap().value("name").toString().toLower() < b.toMap().value("name").toString().toLower();
}

}

QVariantList ZaloService::phoneSyncUnmatched()
{
    QVariantList out;
    if (!m_db) return out;
    psEnsureTables(m_db);

    QStringList gone;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db,
            "SELECT kind, shortId, count, lastTs, preview FROM phone_sync_pending_meta ORDER BY lastTs DESC",
            -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            QString kind = QString::fromUtf8((const char *)sqlite3_column_text(st, 0));
            QString sh   = QString::fromUtf8((const char *)sqlite3_column_text(st, 1));
            if (!QFile::exists(psPendingFile(kind, sh))) { gone << kind << sh; continue; }
            const unsigned char *pv = sqlite3_column_text(st, 4);
            QVariantMap m;
            m["kind"]    = kind;
            m["shortId"] = sh;
            m["isGroup"] = (kind == "g");
            m["count"]   = sqlite3_column_int(st, 2);
            m["lastTs"]  = QString::number((qlonglong)sqlite3_column_int64(st, 3));
            m["preview"] = pv ? QString::fromUtf8((const char *)pv) : QString();
            out.append(m);
        }
        sqlite3_finalize(st);
    }
    for (int i = 0; i + 1 < gone.size(); i += 2) psDropPending(m_db, gone[i], gone[i + 1]);
    return out;
}

QVariantList ZaloService::phoneSyncCandidates(bool isGroup) const
{
    QVariantList out;
    if (isGroup) {
        for (QMap<QString, QString>::const_iterator it = m_groupNames.constBegin(); it != m_groupNames.constEnd(); ++it) {
            if (it.key().isEmpty()) continue;
            QVariantMap m;
            m["id"]   = it.key();
            m["name"] = it.value().isEmpty() ? it.key() : it.value();
            out.append(m);
        }
    } else {
        for (int i = 0; i < m_lastFriendThreads.size(); ++i) {
            QVariantMap f = m_lastFriendThreads[i].toMap();
            QString id = f.value("threadId").toString();
            if (id.isEmpty()) continue;
            QVariantMap m;
            m["id"]   = id;
            m["name"] = f.value("name").toString();
            out.append(m);
        }
    }
    qSort(out.begin(), out.end(), psNameLess);
    return out;
}

int ZaloService::phoneSyncAssign(const QString &kind, const QString &shortId, const QString &threadId)
{
    if (!m_db || shortId.isEmpty() || threadId.isEmpty()) return -1;
    if (kind != "u" && kind != "g") return -1;
    if (m_psh.p) return -1;                       // a sync is running; it owns the tables right now
    psEnsureTables(m_db);
    bool isGroup = (kind == "g");

    QSettings s("BerryLife", "Zalo10");
    QString meShort = s.value("phonesync/meShort_" + m_uid).toString();

    QString threadName;
    if (isGroup) {
        threadName = m_groupNames.value(threadId);
    } else {
        for (int i = 0; i < m_lastFriendThreads.size(); ++i) {
            QVariantMap f = m_lastFriendThreads[i].toMap();
            if (f.value("threadId").toString() == threadId) { threadName = f.value("name").toString(); break; }
        }
    }

    // 1. Remember the mapping (also as a sender alias for 1-1: that person's phone id in group chats).
    sqlite3_stmt *al = 0;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO phone_sync_alias (kind, shortId, longId, name) VALUES (?,?,?,?)",
                           -1, &al, 0) == SQLITE_OK) {
        sqlite3_bind_text(al, 1, kind.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(al, 2, shortId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(al, 3, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(al, 4, "", -1, SQLITE_TRANSIENT);
        sqlite3_step(al);
        if (!isGroup) {
            sqlite3_reset(al);
            sqlite3_bind_text(al, 1, "sender", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(al, 2, shortId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(al, 3, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(al, 4, threadName.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(al);
        }
        sqlite3_finalize(al);
    }

    // 2. Sender mapping: every known phone sender alias, us, and (1-1) the other person.
    QMap<QString, QString> senderLong, senderName;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT shortId, longId, name FROM phone_sync_alias WHERE kind='sender'", -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            QString sh = QString::fromUtf8((const char *)sqlite3_column_text(st, 0));
            senderLong[sh] = QString::fromUtf8((const char *)sqlite3_column_text(st, 1));
            const unsigned char *nm = sqlite3_column_text(st, 2);
            if (nm && *nm) senderName[sh] = QString::fromUtf8((const char *)nm);
        }
        sqlite3_finalize(st);
    }
    QString myName = m_displayName.isEmpty() ? QString::fromUtf8("Me") : m_displayName;
    if (!meShort.isEmpty()) { senderLong[meShort] = m_uid; senderName[meShort] = myName; }
    if (!isGroup) {
        senderLong[shortId] = threadId;
        if (!threadName.isEmpty()) senderName[shortId] = threadName;
    }

    qint64 cleared = psQueryString(m_db, "SELECT clearedAt FROM cleared_threads WHERE threadId=?", threadId).toLongLong();

    // 3. Import the rows of the kept part file (same columns PsDecodeThread reads).
    sqlite3 *part = 0;
    QString partPath = psPendingFile(kind, shortId);
    if (!QFile::exists(partPath)
        || sqlite3_open_v2(partPath.toUtf8().constData(), &part, SQLITE_OPEN_READONLY, 0) != SQLITE_OK) {
        if (part) sqlite3_close(part);
        qWarning() << "[PhoneSync] manual match: kept conversation file missing";
        psDropPending(m_db, kind, shortId);
        return -1;
    }
    int imported = 0;
    QSet<qint64> stickers;
    QMap<int, int> typeSeen, typeOut;     // diagnostics, as in onPhoneSyncTick()
    QMap<int, QString> typeKeys;
    sqlite3_stmt *ins = 0;
    if (sqlite3_prepare_v2(m_db, psInsertSql(), -1, &ins, 0) != SQLITE_OK) { sqlite3_close(part); return -1; }
    sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
    if (sqlite3_prepare_v2(part,
            "SELECT GlbMsgId, CliMsgId, SenderId, TimeStamp, MsgType, MsgContent FROM ChatContent ORDER BY TimeStamp",
            -1, &st, 0) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            QString glb    = QString::number((qlonglong)sqlite3_column_int64(st, 0));
            QString cli    = QString::number((qlonglong)sqlite3_column_int64(st, 1));
            QString sender = QString::number((qlonglong)sqlite3_column_int64(st, 2));
            qint64  ts     = (qint64)sqlite3_column_int64(st, 3);
            if (ts > 0 && ts < 1000000000000LL) ts *= 1000;
            int     type   = sqlite3_column_int(st, 4);
            const unsigned char *c = sqlite3_column_text(st, 5);
            QString content = c ? QString::fromUtf8((const char *)c) : QString();

            if (glb.isEmpty() || glb == "0" || content.isEmpty()) continue;
            typeSeen[type]++;
            if (!typeKeys.contains(type))
                typeKeys[type] = content.trimmed().startsWith(QChar('{'))
                    ? QString("json:%1 len %2").arg(psJsonKeys(content)).arg(content.size()) : QString("text");
            if (cleared > 0 && ts <= cleared) continue;
            if (isMessageDeletedForMe(glb)) continue;
            int outType = 0; QString outContent; qint64 stk = 0;
            if (!psConvertRow(type, content, outType, outContent, stk)) continue;
            if (stk > 0) stickers.insert(stk);

            bool isMine = (!meShort.isEmpty() && sender == meShort);
            QString sid = senderLong.value(sender);
            QString sname = senderName.value(sender);
            if (sid.isEmpty()) { sid = sender; sname = QString::fromUtf8("Member ") + sender; }
            else if (sname.isEmpty()) sname = memberDisplayName(sid);

            if (psInsertMessage(m_db, ins, glb, threadId, outContent, sid, sname, ts, isMine, isGroup, outType, cli)) {
                imported++;
                typeOut[outType]++;
            }
        }
        sqlite3_finalize(st);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(m_db, "COMMIT", 0, 0, 0);
    sqlite3_close(part);

    phoneSyncDismiss(kind, shortId);

    QList<qint64> ids = stickers.toList();
    for (int i = 0; i < ids.size() && i < 150; ++i) downloadSticker(QString::number(ids[i]));

    {
        QString seen, out;
        for (QMap<int,int>::const_iterator it = typeSeen.constBegin(); it != typeSeen.constEnd(); ++it)
            seen += QString("%1:%2[%3] ").arg(it.key()).arg(it.value()).arg(typeKeys.value(it.key()));
        for (QMap<int,int>::const_iterator it = typeOut.constBegin(); it != typeOut.constEnd(); ++it)
            out += QString("%1:%2 ").arg(it.key()).arg(it.value());
        qDebug() << "[PhoneSync] manual match phone types:" << seen << "-> msgType:" << out;
    }
    qDebug() << "[PhoneSync] manual match:" << (isGroup ? "group" : "1-1") << "rows imported=" << imported;
    if (imported > 0) emit historySynced(imported);
    return imported;
}

void ZaloService::phoneSyncDismiss(const QString &kind, const QString &shortId)
{
    if (!m_db) return;
    psEnsureTables(m_db);
    psDropPending(m_db, kind, shortId);
}
