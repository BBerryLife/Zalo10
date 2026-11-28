#include "core/phonesync/PhoneSyncCommon.hpp"

static QString psKey(const PsPart &p) { return QString(p.isGroup ? "g:" : "u:") + p.shortId; }

void ZaloService::onPhoneSyncTick()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled || rt->stage != 5) return;
    if (!m_db) { phoneSyncFail(QString::fromUtf8("Sync failed: database is not open.")); return; }

    QTime clock;
    clock.start();

    if (rt->phase == 0) {
        psEnsureTables(m_db);

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
        if (!rt->meShort.isEmpty()) {
            // Manual matching later (phoneSyncAssign) needs to know which phone sender is us.
            QSettings ms("BerryLife", "Zalo10");
            ms.setValue("phonesync/meShort_" + m_uid, rt->meShort);
        }

        // Phone ids vs Zalo ids: log only lengths/flags (never the ids) to see whether they are related.
        {
            qulonglong u = m_uid.toULongLong();
            qDebug() << "[PhoneSync] own id: phone" << rt->meShort.size() << "digits, zalo" << m_uid.size()
                     << "digits, equal=" << (rt->meShort == m_uid) << "suffix=" << m_uid.endsWith(rt->meShort)
                     << "low32=" << ((qulonglong)(u & 0xFFFFFFFFULL) == rt->meShort.toULongLong());
            QMap<int, int> partLen, friendLen;
            QSet<QString> known;
            for (int i = 0; i < m_friendUids.size(); ++i) { known.insert(m_friendUids[i]); friendLen[m_friendUids[i].size()]++; }
            for (QMap<QString, QString>::const_iterator it = m_groupNames.constBegin(); it != m_groupNames.constEnd(); ++it) known.insert(it.key());
            int direct = 0;
            for (int i = 0; i < rt->parts.size(); ++i) {
                PsPart &p = rt->parts[i];
                partLen[p.shortId.size()]++;
                if (known.contains(p.shortId)) {
                    direct++;
                    if (!rt->threadLong.contains(psKey(p))) rt->threadLong[psKey(p)] = p.shortId;
                }
            }
            QString pl, fl;
            for (QMap<int,int>::const_iterator it = partLen.constBegin(); it != partLen.constEnd(); ++it)
                pl += QString("%1:%2 ").arg(it.key()).arg(it.value());
            for (QMap<int,int>::const_iterator it = friendLen.constBegin(); it != friendLen.constEnd(); ++it)
                fl += QString("%1:%2 ").arg(it.key()).arg(it.value());
            qDebug() << "[PhoneSync] id lengths (len:count) parts:" << pl << "friends:" << fl
                     << "groups:" << m_groupNames.size() << "directMatches=" << direct;
            int partsWithLong = 0, partsHit = 0, totalHit = 0;
            for (int i = 0; i < rt->parts.size(); ++i) {
                const PsPart &pp = rt->parts[i];
                if (!pp.longIds.isEmpty()) partsWithLong++;
                int h = 0;
                for (QSet<QString>::const_iterator it = pp.longIds.constBegin(); it != pp.longIds.constEnd(); ++it)
                    if (known.contains(*it)) h++;
                if (h) { partsHit++; totalHit += h; }
            }
            qDebug() << "[PhoneSync] long ids inside message contents: parts with any=" << partsWithLong
                     << "parts mentioning a known friend/group=" << partsHit << "hits=" << totalHit;
        }

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
            if (clock.elapsed() >= PS_SLICE_MS) {
                int pct = 55 + (rt->parts.isEmpty() ? 0 : (int)(7 * rt->pi / rt->parts.size()));
                emit phoneSyncProgress(pct, QString::fromUtf8("Matching conversations… (%1/%2)").arg(rt->pi).arg(rt->parts.size()));
                rt->tick->start();
                return;
            }
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
        sqlite3_prepare_v2(m_db, psInsertSql(), -1, &rt->ins, 0);
        rt->phase = 2; rt->pi = 0; rt->ri = 0; rt->doneRows = 0;
        emit phoneSyncProgress(62, QString::fromUtf8("Importing messages…"));
        rt->tick->start();
        return;
    }

    if (rt->phase == 2) {
        sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
        while (rt->pi < rt->parts.size()) {
            PsPart &p = rt->parts[rt->pi];
            if (p.threadLong.isEmpty()) {
                // Not matched automatically: keep the part file and a one-row summary so the user can assign
                // the conversation by hand (Settings > Data & Backup > Match Conversations -> phoneSyncAssign()).
                QString kind = p.isGroup ? "g" : "u";
                int count = 0;
                qint64 lastTs = 0;
                QList<int> recent;   // indices of the 4 newest rows, newest first
                for (int k = 0; k < p.rows.size(); ++k) {
                    const PsRow &r = p.rows[k];
                    if (r.glb == "0" || r.content.isEmpty()) continue;
                    count++;
                    if (r.ts > lastTs) lastTs = r.ts;
                    int at = 0;
                    while (at < recent.size() && p.rows[recent[at]].ts >= r.ts) at++;
                    if (at < 4) { recent.insert(at, k); if (recent.size() > 4) recent.removeLast(); }
                }
                rt->doneRows += p.rows.size();
                if (lastTs > 0 && lastTs < 1000000000000LL) lastTs *= 1000;
                QStringList lines;
                for (int k = recent.size() - 1; k >= 0; --k) {     // oldest first
                    const PsRow &r = p.rows[recent[k]];
                    int ot = 0; QString oc; qint64 stk = 0;
                    if (!psConvertRow(r.type, r.content, ot, oc, stk)) continue;
                    lines << ((r.sender == rt->meShort) ? QString::fromUtf8("You: ") : QString()) + psPreviewText(ot, oc);
                }
                if (count > 0 && !p.filePath.isEmpty() && psKeepFile(p.filePath, psPendingFile(kind, p.shortId))) {
                    p.filePath.clear();
                    sqlite3_stmt *mt = 0;
                    if (sqlite3_prepare_v2(m_db,
                            "INSERT OR REPLACE INTO phone_sync_pending_meta (kind, shortId, count, lastTs, preview) VALUES (?,?,?,?,?)",
                            -1, &mt, 0) == SQLITE_OK) {
                        sqlite3_bind_text (mt, 1, kind.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text (mt, 2, p.shortId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int  (mt, 3, count);
                        sqlite3_bind_int64(mt, 4, (sqlite3_int64)lastTs);
                        sqlite3_bind_text (mt, 5, lines.join("\n").toUtf8().constData(), -1, SQLITE_TRANSIENT);
                        sqlite3_step(mt);
                        sqlite3_finalize(mt);
                    }
                    rt->staged++;
                }
                rt->pi++; rt->ri = 0;
                if (clock.elapsed() >= PS_SLICE_MS) break;
                continue;
            }
            if (rt->ri == 0 && !rt->clearedAt.contains(p.threadLong)) {
                rt->clearedAt[p.threadLong] = psQueryString(m_db, "SELECT clearedAt FROM cleared_threads WHERE threadId=?", p.threadLong).toLongLong();
            }
            qint64 cleared = rt->clearedAt.value(p.threadLong);
            while (rt->ri < p.rows.size()) {
                const PsRow &r = p.rows[rt->ri++];
                rt->doneRows++;
                rt->typeSeen[r.type]++;
                if (!rt->typeKeys.contains(r.type) && !r.content.isEmpty()) {
                    // Diagnostics for unknown phone types: JSON key names only, never values.
                    QString keys = QString::fromUtf8("text");
                    if (r.content.trimmed().startsWith(QChar('{')))
                        keys = QString("json:%1 len %2").arg(psJsonKeys(r.content)).arg(r.content.size());
                    rt->typeKeys[r.type] = keys;
                }
                if (r.glb == "0" || r.content.isEmpty()) continue;
                qint64 ts = r.ts;
                if (ts > 0 && ts < 1000000000000LL) ts *= 1000;
                if (cleared > 0 && ts <= cleared) continue;
                if (isMessageDeletedForMe(r.glb)) continue;

                int outType = 0;
                QString outContent;
                qint64 stickerId = 0;
                if (!psConvertRow(r.type, r.content, outType, outContent, stickerId)) continue;
                if (stickerId > 0) rt->stickers.insert(stickerId);

                bool isMine = (r.sender == rt->meShort);
                QString sid = rt->senderLong.value(r.sender);
                QString sname = rt->senderName.value(r.sender);
                if (sid.isEmpty()) { sid = r.sender; sname = QString::fromUtf8("Member ") + r.sender; }

                if (psInsertMessage(m_db, rt->ins, r.glb, p.threadLong, outContent, sid, sname, ts,
                                    isMine, p.isGroup, outType, r.cli)) {
                    rt->imported++;
                    rt->importedThreads.insert(p.threadLong);
                    rt->typeOut[outType]++;
                }

                if ((rt->ri & 63) == 0 && clock.elapsed() >= PS_SLICE_MS) break;
            }
            if (rt->ri >= p.rows.size()) {
                // Matched now: drop what an earlier sync kept for manual matching, and the temporary file.
                psDropPending(m_db, p.isGroup ? "g" : "u", p.shortId);
                if (!p.filePath.isEmpty()) { QFile::remove(p.filePath); p.filePath.clear(); }
                rt->pi++; rt->ri = 0;
            }
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
    int staged = rt->staged;
    QList<qint64> stickerIds = rt->stickers.toList();
    {
        QString seen, out;
        for (QMap<int,int>::const_iterator it = rt->typeSeen.constBegin(); it != rt->typeSeen.constEnd(); ++it)
            seen += QString("%1:%2[%3] ").arg(it.key()).arg(it.value()).arg(rt->typeKeys.value(it.key()));
        for (QMap<int,int>::const_iterator it = rt->typeOut.constBegin(); it != rt->typeOut.constEnd(); ++it)
            out += QString("%1:%2 ").arg(it.key()).arg(it.value());
        qDebug() << "[PhoneSync] phone types (type:rows[json keys]):" << seen;
        qDebug() << "[PhoneSync] imported as msgType:" << out << "stickers=" << stickerIds.size() << "staged=" << staged;
    }
    QSettings s("BerryLife", "Zalo10");
    s.setValue("phonesync/lastSync_" + m_uid, QDateTime::currentMSecsSinceEpoch());

    psCleanup(rt, m_db);
    rt->timer->deleteLater();
    rt->tick->deleteLater();
    delete rt;
    m_psh.p = 0;

    // Warm the sticker cache for imported sticker messages (downloadSticker() dedups; capped).
    for (int i = 0; i < stickerIds.size() && i < 150; ++i)
        downloadSticker(QString::number(stickerIds[i]));

    QString msg = QString::fromUtf8("Imported %1 older message(s) into %2 conversation(s).").arg(imported).arg(threads);
    if (unmatched > 0)
        msg += QString::fromUtf8("\n%1 of %2 conversations could not be matched automatically. "
                                 "Open Settings > Data & Backup > Match Conversations to choose who they are.").arg(unmatched).arg(total);
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
