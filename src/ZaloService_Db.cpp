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

// SQLite-backed local storage: message cache, quick-message presets,
// and data export/import/cache-clearing utilities.

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

void ZaloService::dbSaveMessage(const QVariantMap &msg, const QString &threadId)
{
    if (!m_db || threadId.isEmpty()) return;
    QString msgId = msg["msgId"].toString();
    if (msgId.isEmpty()) return;

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

    // UPDATE-then-INSERT (rather than INSERT OR REPLACE) so that re-saving an
    // already-known message (e.g. on re-fetch/re-sync) never wipes the
    // recalledOriginalContent column back to '' — INSERT OR REPLACE deletes and
    // recreates the row, resetting any column not listed in VALUES. This also
    // avoids relying on "ON CONFLICT ... DO UPDATE" (SQLite 3.24+), which older
    // bundled SQLite builds on BB10 may not support.
    //
    // localImage/imgWidth/imgHeight use CASE WHEN to preserve the existing DB
    // value whenever the caller's map doesn't carry them (empty/0): almost
    // every dbSaveMessage() call site builds its QVariantMap from the raw WS
    // payload, which never includes these — they're populated separately and
    // asynchronously by downloadImageMessage()/onImageMsgDownloaded(). Without
    // this guard, any later re-save of the same msgId (e.g. the cmd=510
    // history-sync path patching a message to "recalled" in-place, then
    // re-saving it) would silently null out an already-downloaded photo's
    // local cache path, even though nothing about the image actually changed.
    const char *sqlUpdate =
        "UPDATE messages SET threadId=?,content=?,senderId=?,dName=?,ts=?,"
        "isMine=?,isGroup=?,msgType=?,"
        "localImage = CASE WHEN ?='' THEN localImage ELSE ? END,"
        "imgWidth   = CASE WHEN ?=0  THEN imgWidth   ELSE ? END,"
        "imgHeight  = CASE WHEN ?=0  THEN imgHeight  ELSE ? END "
        "WHERE msgId=?";
    sqlite3_stmt *upd = 0;
    bool updated = false;
    if (sqlite3_prepare_v2(m_db, sqlUpdate, -1, &upd, 0) == SQLITE_OK) {
        QByteArray localImageUtf8 = msg["localImage"].toString().toUtf8();
        int imgWidthVal  = msg["imgWidth"].toInt();
        int imgHeightVal = msg["imgHeight"].toInt();
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
        sqlite3_bind_text(upd, 15, msgId.toUtf8().constData(),                        -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
        updated = sqlite3_changes(m_db) > 0;
        sqlite3_finalize(upd);
    }
    if (updated) return;

    const char *sql =
        "INSERT INTO messages "
        "(msgId,threadId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
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
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

QVariantList ZaloService::dbLoadMessages(const QString &threadId)
{
    QVariantList result;
    if (!m_db || threadId.isEmpty()) return result;

    const char *sql =
        "SELECT msgId,content,senderId,dName,ts,isMine,isGroup,msgType,localImage,imgWidth,imgHeight,recalledOriginalContent "
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
        result.append(m);
    }
    sqlite3_finalize(stmt);
    qDebug() << "[Zalo] dbLoadMessages" << threadId << "rows:" << result.size();
    return result;
}

// Every message row across every thread, oldest first — used by exportData().
// Unlike dbLoadMessages() (per-thread, 200-row cap for chat display), this has
// no LIMIT: an export is meant to be a full backup, not a UI page.
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

// Filename prefixes Zalo10 writes under QDir::tempPath() for cached images.
// Centralized here so exportData()'s "include images" option and clearCache()
// agree on exactly what counts as a cache file — see ZaloService.hpp for the
// full list of call sites that create these (avatar download, photo thumbnail
// decode, full-size photo fetch, generic image fetch, QR login code).
QStringList ZaloService::cacheFilePatterns() const
{
    QStringList pats;
    pats << "avatar_" << "msgthumb_" << "msgimg_" << "zalo_img_" << "qr.png";
    return pats;
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
    // SQLITE_CONSTRAINT here means idx_qm_name rejected a duplicate (case-insensitive) name.
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
    if (rc != SQLITE_DONE) return false; // duplicate name against another row
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
// Data export / import / cache management (Settings → "Data" section)
// ---------------------------------------------------------------------------
//
// File layout produced by exportData(), under <destDir>/zalo10/:
//   zalo10_data_<timestamp>.json            — always written
//   zalo10_data_<timestamp>_images/         — only when includeImages=true,
//                                              and only if at least one
//                                              referenced image still exists
//
// Debug logs go to a sibling "log" folder instead — <destDir>/zalo10/log/ —
// kept separate from data exports so a user attaching a log to a bug report
// email doesn't also grab their full message history by accident.
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

    // Text-only export: image files are never copied. They live in tmp cache
    // that's gone the moment the app is updated/reinstalled (the user has to
    // delete and reinstall, not just upgrade in place), so bundling them would
    // produce a backup that's broken on arrival anyway. Each message that had
    // an image keeps its text content (if any) and gets a "[Photo]" marker
    // appended so the conversation still reads naturally on import; the
    // localImage/imgWidth/imgHeight fields are dropped entirely.
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

    // existing msgIds — used to skip duplicates ("existing data always wins",
    // matching the same UPDATE-then-INSERT philosophy as dbSaveMessage(),
    // except here we explicitly do NOT touch a row that's already present).
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

        // exportData() never bundles image files (text-only export — see its
        // comment), so an imported row never has a localImage; any message
        // that had a photo arrives here as plain text ending in "[Photo]".
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
    // 1. Delete every cached image file this app writes to tempPath().
    //    NOTE: once deleted, any localImage path stored in the messages table
    //    now points at a file that no longer exists — that's expected (it's
    //    exactly why exportData() checks QFile::exists() before bundling an
    //    image, and why Settings warns the user upfront).
    QDir tmp(QDir::tempPath());
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

    // Also drop the in-memory avatar cache map/dedup sets so the app doesn't
    // keep handing out file:// paths that were just deleted.
    m_avatarCache.clear();
    m_pendingAvatars.clear();
    m_pendingAvatarWaiters.clear();

    // 2. Wipe local message history. cleared_threads and quick_messages are
    //    left alone on purpose: a per-thread "cleared at" marker and the
    //    user's saved canned replies are settings/preferences, not cache.
    if (m_db) {
        sqlite3_exec(m_db, "DELETE FROM messages;", 0, 0, 0);
        sqlite3_exec(m_db, "VACUUM;", 0, 0, 0);
    }

    qDebug() << "[Zalo] clearCache: deleted" << deleted << "cache files, wiped message history";
    return deleted;
}

