#include "core/db/DbCommon.hpp"

QStringList ZaloService::cacheFilePatterns() const
{
    QStringList pats;
    pats << "avatar_" << "msgthumb_" << "msgimg_" << "zalo_img_" << "qr.png";
    return pats;
}

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
    // UPDATE-then-INSERT like dbSaveMessage() (old bundled SQLite lacks ON CONFLICT DO UPDATE).
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
}

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
    if (rc != SQLITE_DONE) return false;
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

    out["success"] = true;
    out["importedMessages"] = imported;
    out["skippedMessages"] = skipped;
    out["importedQuickMessages"] = qmImported;
    out["skippedQuickMessages"] = qmSkipped;
    return out;
}

int ZaloService::clearCache()
{
    // Scan the literal "/tmp", not QDir::tempPath(): on BB10 tempPath() is a scratch dir wiped on
    // restart while "/tmp" holds the real image cache. Stale localImage paths are intentional
    // (exportData() checks QFile::exists()).
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

    m_avatarCache.clear();
    m_pendingAvatars.clear();
    m_pendingAvatarWaiters.clear();

    // Keep cleared_threads and quick_messages (user settings). avatar_meta is cleared because
    // its avatar files were just deleted.
    if (m_db) {
        sqlite3_exec(m_db, "DELETE FROM messages;", 0, 0, 0);
        sqlite3_exec(m_db, "DELETE FROM message_states;", 0, 0, 0);
        sqlite3_exec(m_db, "DELETE FROM avatar_meta;", 0, 0, 0);
        sqlite3_exec(m_db, "VACUUM;", 0, 0, 0);
    }

    return deleted;
}
