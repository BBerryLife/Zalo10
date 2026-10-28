#include "core/phonesync/PhoneSyncCommon.hpp"

static QString psKey(const PsPart &p) { return QString(p.isGroup ? "g:" : "u:") + p.shortId; }

static QString psQueryString(sqlite3 *db, const char *sql, const QString &arg)
{
    QString out;
    sqlite3_stmt *st = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &st, 0) == SQLITE_OK) {
        if (!arg.isNull()) sqlite3_bind_text(st, 1, arg.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char *c = sqlite3_column_text(st, 0);
            if (c) out = QString::fromUtf8((const char *)c);
        }
        sqlite3_finalize(st);
    }
    return out;
}

void ZaloService::onPhoneSyncTick()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled || rt->stage != 5) return;
    if (!m_db) { phoneSyncFail(QString::fromUtf8("Sync failed: database is not open.")); return; }

    QTime clock;
    clock.start();

    if (rt->phase == 0) {
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS phone_sync_alias ("
            "kind TEXT NOT NULL, shortId TEXT NOT NULL, longId TEXT NOT NULL, name TEXT, "
            "PRIMARY KEY(kind, shortId))", 0, 0, 0);

        QMap<QString, int> cnt;
        for (int i = 0; i < rt->parts.size(); ++i) {
            QSet<QString> seen;
            const QList<PsRow> &rows = rt->parts[i].rows;
            for (int k = 0; k < rows.size(); ++k) seen.insert(rows[k].sender);
            foreach (const QString &s, seen) cnt[s] += 1;
        }
        int best = 0;
        for (QMap<QString,int>::const_iterator it = cnt.constBegin(); it != cnt.constEnd(); ++it)
            if (it.value() > best) { best = it.value(); rt->meShort = it.key(); }
        rt->myName = psQueryString(m_db, "SELECT dName FROM messages WHERE isMine=1 AND dName IS NOT NULL AND dName<>'' LIMIT 1", QString());
        if (rt->myName.isEmpty()) rt->myName = m_displayName;
        if (rt->myName.isEmpty()) rt->myName = QString::fromUtf8("Me");

        sqlite3_stmt *st = 0;
        if (sqlite3_prepare_v2(m_db, "SELECT kind, shortId, longId, name FROM phone_sync_alias", -1, &st, 0) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                QString kind = QString::fromUtf8((const char *)sqlite3_column_text(st, 0));
                QString sh   = QString::fromUtf8((const char *)sqlite3_column_text(st, 1));
                QString lg   = QString::fromUtf8((const char *)sqlite3_column_text(st, 2));
                const unsigned char *nm = sqlite3_column_text(st, 3);
                if (kind == "sender") {
                    rt->senderLong[sh] = lg;
                    if (nm && *nm) rt->senderName[sh] = QString::fromUtf8((const char *)nm);
                } else {
                    rt->threadLong[kind + ":" + sh] = lg;
                }
            }
            sqlite3_finalize(st);
        }
        rt->senderLong[rt->meShort] = m_uid;
        rt->senderName[rt->meShort] = rt->myName;

        sqlite3_prepare_v2(m_db, "SELECT threadId, senderId, dName, isMine FROM messages WHERE msgId=?", -1, &rt->find, 0);
        rt->phase = 1; rt->pi = 0; rt->ri = 0;
    }

    if (rt->phase == 1) {
        while (rt->pi < rt->parts.size()) {
            PsPart &p = rt->parts[rt->pi];
            QString tk = psKey(p);
            while (rt->ri < p.rows.size()) {
                const PsRow &r = p.rows[rt->ri++];
                bool threadKnown = rt->threadLong.contains(tk);
                bool senderKnown = rt->senderLong.contains(r.sender);
                if (!(threadKnown && senderKnown) && r.glb != "0" && rt->find) {
                    sqlite3_reset(rt->find);
                    sqlite3_bind_text(rt->find, 1, r.glb.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                    if (sqlite3_step(rt->find) == SQLITE_ROW) {
                        QString tid = QString::fromUtf8((const char *)sqlite3_column_text(rt->find, 0));
                        QString sid = QString::fromUtf8((const char *)sqlite3_column_text(rt->find, 1));
                        const unsigned char *dn = sqlite3_column_text(rt->find, 2);
                        if (!threadKnown && !tid.isEmpty()) rt->threadLong[tk] = tid;
                        if (!senderKnown && !sid.isEmpty() && sid != "0") {
                            rt->senderLong[r.sender] = sid;
                            if (dn && *dn) rt->senderName[r.sender] = QString::fromUtf8((const char *)dn);
                        }
                    }
                }
                if ((rt->ri & 63) == 0 && clock.elapsed() >= PS_SLICE_MS) break;
            }
            if (rt->ri >= p.rows.size()) { rt->pi++; rt->ri = 0; }
            if (clock.elapsed() >= PS_SLICE_MS) { rt->tick->start(); return; }
        }
        for (int i = 0; i < rt->parts.size(); ++i) {
            PsPart &p = rt->parts[i];
            QString tk = psKey(p);
            if (!p.isGroup && rt->threadLong.contains(tk) && !rt->senderLong.contains(p.shortId)) {
                rt->senderLong[p.shortId] = rt->threadLong[tk];
            }
            if (rt->threadLong.contains(tk)) p.threadLong = rt->threadLong[tk];
        }
        for (QMap<QString,QString>::const_iterator it = rt->senderLong.constBegin(); it != rt->senderLong.constEnd(); ++it) {
            if (rt->senderName.contains(it.key())) continue;
            QString nm = psQueryString(m_db, "SELECT dName FROM messages WHERE senderId=? AND dName<>'' LIMIT 1", it.value());
            if (!nm.isEmpty()) rt->senderName[it.key()] = nm;
        }
        sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
        sqlite3_stmt *st = 0;
        if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO phone_sync_alias (kind, shortId, longId, name) VALUES (?,?,?,?)", -1, &st, 0) == SQLITE_OK) {
            for (QMap<QString,QString>::const_iterator it = rt->threadLong.constBegin(); it != rt->threadLong.constEnd(); ++it) {
                QString kind = it.key().left(1), sh = it.key().mid(2);
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, kind.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, sh.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, it.value().toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 4, "", -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
            }
            for (QMap<QString,QString>::const_iterator it = rt->senderLong.constBegin(); it != rt->senderLong.constEnd(); ++it) {
                if (it.key() == rt->meShort) continue;
                sqlite3_reset(st);
                sqlite3_bind_text(st, 1, "sender", -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 2, it.key().toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 3, it.value().toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(st, 4, rt->senderName.value(it.key()).toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_step(st);
            }
            sqlite3_finalize(st);
        }
        sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

        if (rt->find) { sqlite3_finalize(rt->find); rt->find = 0; }
        sqlite3_prepare_v2(m_db,
            "INSERT OR IGNORE INTO messages (msgId, threadId, content, senderId, dName, ts, isMine, isGroup, msgType, cliMsgId) "
            "VALUES (?,?,?,?,?,?,?,?,?,?)", -1, &rt->ins, 0);
        rt->phase = 2; rt->pi = 0; rt->ri = 0; rt->doneRows = 0;
        emit phoneSyncProgress(62, QString::fromUtf8("Importing messages…"));
        rt->tick->start();
        return;
    }

    if (rt->phase == 2) {
        sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
        while (rt->pi < rt->parts.size()) {
            PsPart &p = rt->parts[rt->pi];
            if (p.threadLong.isEmpty()) { rt->doneRows += p.rows.size(); rt->pi++; rt->ri = 0; continue; }
            if (rt->ri == 0 && !rt->clearedAt.contains(p.threadLong)) {
                rt->clearedAt[p.threadLong] = psQueryString(m_db, "SELECT clearedAt FROM cleared_threads WHERE threadId=?", p.threadLong).toLongLong();
            }
            qint64 cleared = rt->clearedAt.value(p.threadLong);
            while (rt->ri < p.rows.size()) {
                const PsRow &r = p.rows[rt->ri++];
                rt->doneRows++;
                if (r.type != 0 || r.glb == "0" || r.content.isEmpty()) continue;
                qint64 ts = r.ts;
                if (ts > 0 && ts < 1000000000000LL) ts *= 1000;
                if (cleared > 0 && ts <= cleared) continue;
                if (isMessageDeletedForMe(r.glb)) continue;

                bool isMine = (r.sender == rt->meShort);
                QString sid = rt->senderLong.value(r.sender);
                QString sname = rt->senderName.value(r.sender);
                if (sid.isEmpty()) { sid = r.sender; sname = QString::fromUtf8("Member ") + r.sender; }
                int outType = 0;
                if (r.content.startsWith(QChar('{')) && (r.content.contains("\"normalUrl\"") || r.content.contains("\"thumbUrl\"") || r.content.contains("\"hdUrl\"")))
                    outType = 2;

                sqlite3_reset(rt->ins);
                sqlite3_bind_text(rt->ins, 1, r.glb.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rt->ins, 2, p.threadLong.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rt->ins, 3, r.content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rt->ins, 4, sid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rt->ins, 5, sname.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(rt->ins, 6, QString::number((qlonglong)ts).toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (rt->ins, 7, isMine ? 1 : 0);
                sqlite3_bind_int (rt->ins, 8, p.isGroup ? 1 : 0);
                sqlite3_bind_int (rt->ins, 9, outType);
                sqlite3_bind_text(rt->ins, 10, r.cli.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                sqlite3_step(rt->ins);
                if (sqlite3_changes(m_db) > 0) { rt->imported++; rt->importedThreads.insert(p.threadLong); }

                if ((rt->ri & 63) == 0 && clock.elapsed() >= PS_SLICE_MS) break;
            }
            if (rt->ri >= p.rows.size()) { rt->pi++; rt->ri = 0; }
            if (clock.elapsed() >= PS_SLICE_MS) break;
        }
        sqlite3_exec(m_db, "COMMIT", 0, 0, 0);

        if (rt->pi < rt->parts.size()) {
            int pct = 62 + (rt->totalRows > 0 ? (int)(36 * rt->doneRows / rt->totalRows) : 0);
            emit phoneSyncProgress(pct, QString::fromUtf8("Importing messages… (%1)").arg(rt->imported));
            rt->tick->start();
            return;
        }
        rt->phase = 3;
    }

    int unmatched = 0;
    for (int i = 0; i < rt->parts.size(); ++i) if (rt->parts[i].threadLong.isEmpty()) unmatched++;
    int imported = rt->imported;
    int threads = rt->importedThreads.size();
    int total = rt->parts.size();
    QSettings s("BerryLife", "Zalo10");
    s.setValue("phonesync/lastSync_" + m_uid, QDateTime::currentMSecsSinceEpoch());

    psCleanup(rt, m_db);
    rt->timer->deleteLater();
    rt->tick->deleteLater();
    delete rt;
    m_psh.p = 0;

    QString msg = QString::fromUtf8("Imported %1 older message(s) into %2 conversation(s).").arg(imported).arg(threads);
    if (unmatched > 0)
        msg += QString::fromUtf8("\n%1 of %2 conversations could not be matched yet (Zalo10 has not received any message from them). Run the sync again after a few days.").arg(unmatched).arg(total);
    qDebug() << "[PhoneSync] done:" << imported << "messages," << threads << "conversations," << unmatched << "of" << total << "not matched yet";
    emit phoneSyncProgress(100, QString::fromUtf8("Done"));
    if (imported > 0) emit historySynced(imported);
    emit phoneSyncFinished(true, msg);
}

void ZaloService::onPhoneSyncTimeout()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt) return;
    phoneSyncFail(rt->stage <= 2 ? QString::fromUtf8("Sync failed: timed out waiting for confirmation on your phone.")
                                 : QString::fromUtf8("Sync failed: download timed out."));
}
