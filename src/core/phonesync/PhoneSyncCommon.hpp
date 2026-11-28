#ifndef ZALO10_PHONESYNC_COMMON_HPP
#define ZALO10_PHONESYNC_COMMON_HPP

#include "core/ZaloService.hpp"
#include "utils/ZaloServiceUtils.hpp"
#include "core/PhoneSyncCore.hpp"

#include <QThread>
#include <QTimer>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QRegExp>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QSslConfiguration>
#include <QSslSocket>
#include <zlib.h>
#include <QDebug>
#include <sqlite3.h>
#include <string>

// Phone message sync (Zalo's "sync msg to PC"):
// 1. RSA-2048 keypair (own thread, a few seconds on BB10)
// 2. GET /api/message/pull_mobile_msg with the public key -> the phone asks the user
// 3. WS cmd=601 act_type "syncmsgmb": user_confirm -> syncmsg_info (url + encrypted_key)
// 4. download .db.crypt, RSA-decrypt encrypted_key -> AES key, AES-CBC per 64KB chunk
// 5. ZDB4.0 -> XZ -> SQLite files (own thread, see PhoneSyncCore.cpp)
// 6. map short phone ids <-> long Zalo10 ids by matching message ids (cached in phone_sync_alias),
//    then import into `messages` in small time slices to keep the UI responsive.
// Conversations with no received message can't be mapped yet; the next sync imports them.

namespace {

const int PS_CONFIRM_TIMEOUT_MS  = 10 * 60 * 1000;
const int PS_DOWNLOAD_TIMEOUT_MS = 5 * 60 * 1000;
const int PS_SLICE_MS            = 30;

struct PsRow {
    QString sender, cli, glb;
    qint64  ts;
    int     type;
    QString content;
    PsRow() : ts(0), type(0) {}
};

struct PsPart {
    QString       shortId;
    bool          isGroup;
    QList<PsRow>  rows;
    QString       threadLong;
    QString       schema;      // diagnostics: tables(columns) found in the part database
    int           apiRows;     // diagnostics: rows in ApiSafe
    QSet<QString> longIds;     // diagnostics: 17-19 digit numbers seen in message contents
    QString       filePath;    // the part's SQLite file (kept for unmatched parts, see psPendingDir())
    PsPart() : isGroup(false), apiRows(0) {}
};

// ---- shared by the automatic import (Tick) and the manual matching (Match) ----

inline QString psEsc(const QString &s)
{
    QString e = s;
    e.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
    return e;
}

inline QString psQueryString(sqlite3 *db, const char *sql, const QString &arg)
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

// phone_sync_alias: short phone id -> Zalo10 id.
// Conversations that could not be matched automatically keep their decoded part file (one SQLite file per
// conversation, as the phone sends it) in psPendingDir(), plus one summary row in phone_sync_pending_meta,
// so the user can assign them by hand later (Settings > Data & Backup > Match Conversations). Copying every
// row into zalo_messages.db instead made a full-history sync take forever on the device.
inline void psEnsureTables(sqlite3 *db)
{
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS phone_sync_alias ("
        "kind TEXT NOT NULL, shortId TEXT NOT NULL, longId TEXT NOT NULL, name TEXT, "
        "PRIMARY KEY(kind, shortId))", 0, 0, 0);
    sqlite3_exec(db, "DROP TABLE IF EXISTS phone_sync_pending", 0, 0, 0);   // row copy of an earlier build
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS phone_sync_pending_meta ("
        "kind TEXT NOT NULL, shortId TEXT NOT NULL, count INTEGER, lastTs INTEGER, preview TEXT, "
        "PRIMARY KEY(kind, shortId))", 0, 0, 0);
}

inline QString psPendingDir() { return QDir::homePath() + "/phonesync_pending"; }

inline QString psPendingFile(const QString &kind, const QString &shortId)
{
    return psPendingDir() + "/" + kind + "_" + shortId + ".sqlite";
}

// Moves (or copies, across file systems) a decoded part file into the pending folder.
inline bool psKeepFile(const QString &from, const QString &to)
{
    QDir().mkpath(psPendingDir());
    if (QFile::exists(to)) QFile::remove(to);
    if (QFile::rename(from, to)) return true;
    if (QFile::copy(from, to)) { QFile::remove(from); return true; }
    return false;
}

inline void psDropPending(sqlite3 *db, const QString &kind, const QString &shortId)
{
    QFile::remove(psPendingFile(kind, shortId));
    sqlite3_stmt *del = 0;
    if (sqlite3_prepare_v2(db, "DELETE FROM phone_sync_pending_meta WHERE kind=? AND shortId=?", -1, &del, 0) == SQLITE_OK) {
        sqlite3_bind_text(del, 1, kind.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(del, 2, shortId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del);
        sqlite3_finalize(del);
    }
}

// Reads one field from JSON-like text without a full parse (a full jsonToMap() per row was slow on the
// device, and the phone's non-text rows are not always strict JSON). Finds "key":"value", "key":123 and the
// escaped form \"key\":\"value\" used when an object is nested as a string (e.g. "params":"{\"fileSize\":...}").
// Returns an empty string when the key is absent.
inline QString psField(const QString &c, const QString &key)
{
    for (int form = 0; form < 2; ++form) {
        bool nested = (form == 1);
        QString k = nested ? ("\\\"" + key + "\\\"") : ("\"" + key + "\"");
        QString q = nested ? QString("\\\"") : QString("\"");
        int from = 0;
        while (true) {
            int at = c.indexOf(k, from);
            if (at < 0) break;
            from = at + 1;
            if (!nested && at > 0 && c[at - 1] == QChar('\\')) continue;   // that is the nested form
            int i = at + k.size();
            while (i < c.size() && c[i].isSpace()) i++;
            if (i >= c.size() || c[i] != QChar(':')) continue;
            i++;
            while (i < c.size() && c[i].isSpace()) i++;
            if (c.mid(i, q.size()) == q) {
                i += q.size();
                QString out;
                while (i < c.size()) {
                    if (nested) {
                        if (c.mid(i, 2) == "\\\"") break;
                        if (c[i] == QChar('\\') && i + 1 < c.size() && c[i + 1] == QChar('\\')) {
                            // \\X inside a nested string: one level of escaping left
                            QChar x = (i + 2 < c.size()) ? c[i + 2] : QChar();
                            if (x == QChar('n')) out += QChar('\n');
                            else if (x == QChar('u') && i + 6 < c.size()) { out += QChar(c.mid(i + 3, 4).toUShort(0, 16)); i += 7; continue; }
                            else if (!x.isNull()) out += x;
                            i += 3; continue;
                        }
                        if (c[i] == QChar('\\') && i + 1 < c.size()) { out += c[i + 1]; i += 2; continue; }
                        out += c[i++];
                    } else {
                        if (c[i] == QChar('"')) break;
                        if (c[i] == QChar('\\') && i + 1 < c.size()) {
                            QChar x = c[i + 1];
                            if (x == QChar('n')) out += QChar('\n');
                            else if (x == QChar('t')) out += QChar('\t');
                            else if (x == QChar('r')) {}
                            else if (x == QChar('u') && i + 5 < c.size()) { out += QChar(c.mid(i + 2, 4).toUShort(0, 16)); i += 6; continue; }
                            else out += x;
                            i += 2; continue;
                        }
                        out += c[i++];
                    }
                }
                return out;
            }
            int s = i;   // number or bare token
            while (i < c.size() && (c[i].isDigit() || c[i] == QChar('-') || c[i] == QChar('.'))) i++;
            return c.mid(s, i - s);
        }
    }
    return QString();
}

// Key names found in JSON-like text (diagnostics only; never values).
inline QString psJsonKeys(const QString &c)
{
    QRegExp rx("\\\\?\"([A-Za-z_][A-Za-z0-9_]{0,30})\\\\?\"\\s*:");
    QStringList keys;
    int pos = 0;
    while ((pos = rx.indexIn(c, pos)) != -1 && keys.size() < 25) {
        if (!keys.contains(rx.cap(1))) keys << rx.cap(1);
        pos += rx.matchedLength();
    }
    return keys.join(",");
}

// {"normalUrl","thumbUrl","hdUrl"} as Zalo10 stores photos, or "" when the text has no http image URL.
inline QString psPhotoJson(const QString &c)
{
    QString nu = psField(c, "normalUrl"), hd = psField(c, "hdUrl"), ori = psField(c, "oriUrl");
    QString th = psField(c, "thumbUrl"), href = psField(c, "href"), thumb = psField(c, "thumb");
    QString n = nu.startsWith("http") ? nu : (href.startsWith("http") ? href : (ori.startsWith("http") ? ori : hd));
    QString h = hd.startsWith("http") ? hd : (ori.startsWith("http") ? ori : n);
    QString t = th.startsWith("http") ? th : (thumb.startsWith("http") ? thumb : n);
    if (!n.startsWith("http")) n = h.startsWith("http") ? h : t;
    if (!n.startsWith("http")) return QString();
    if (!h.startsWith("http")) h = n;
    if (!t.startsWith("http")) t = n;
    return QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"}").arg(psEsc(n)).arg(psEsc(t)).arg(psEsc(h));
}

// Converts one phone ChatContent row into the (msgType, content) Zalo10 stores: 0 text, 2 photo,
// 3 file/video, 4 call, 5 sticker, 6 link. The phone's numeric MsgType values are not documented, so
// non-text rows are recognised by their content (same field names as the web protocol). Media that
// Zalo10 cannot show (voice, location, unknown) becomes a short readable label instead of being dropped.
// Returns false to skip the row.
inline bool psConvertRow(int phoneType, const QString &content, int &outType, QString &outContent, qint64 &stickerId)
{
    stickerId = 0;
    QString c = content.trimmed();
    if (c.isEmpty()) return false;
    bool isJson = c.startsWith(QChar('{'));

    if (phoneType == 0) {
        outType = 0;
        outContent = content;
        if (isJson && (c.contains("\"normalUrl\"") || c.contains("\"thumbUrl\"") || c.contains("\"hdUrl\""))) {
            QString ph = psPhotoJson(c);
            if (!ph.isEmpty()) { outType = 2; outContent = ph; }
        }
        return true;
    }

    if (!isJson) {                        // media type with a plain-text body (e.g. a bare link)
        outType = 0;
        outContent = content;
        return true;
    }
    QString action = psField(c, "action").toLower();
    QString href   = psField(c, "href");
    QString lhref  = href.toLower();
    QString title  = psField(c, "title");
    QString desc   = psField(c, "description");

    // sticker: {id, catId, type}
    qint64 sid = psField(c, "id").toLongLong();
    if (sid > 0 && href.isEmpty() && (c.contains("catId") || c.contains("cateId") || c.contains("catid"))) {
        stickerId = sid;
        outType = 5;
        outContent = QString("{\"stickerId\":%1}").arg(sid);
        return true;
    }
    // call log: {action:"recommened.calltime"/"recommened.misscall", params:{calltype, duration}}
    if (action.contains("call")) {
        QString result = action.contains("misscall") ? "missed" : "ended";
        QString kind   = (psField(c, "calltype").toInt() == 1) ? "video" : "voice";
        qint64  dur    = psField(c, "duration").toLongLong();
        outType = 4;
        outContent = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}").arg(result).arg(kind).arg(dur);
        return true;
    }
    // link preview: {action:"recommened.link", title, description, href, thumb}
    if (action.contains("link")) {
        outType = 6;
        outContent = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                         .arg(psEsc(title)).arg(psEsc(desc)).arg(psEsc(href)).arg(psEsc(psField(c, "thumb")));
        return true;
    }
    // voice note
    if (action.contains("voice") || lhref.contains(".aac") || lhref.contains(".amr") || lhref.contains(".m4a")) {
        outType = 0;
        outContent = QString::fromUtf8("[Voice message]");
        return true;
    }
    // file / video: {title, href, params:{fileSize, fileExt}} or a video href
    bool isVideo = lhref.contains(".mp4") || c.contains("video_width") || c.contains("videoWidth");
    bool isFile  = c.contains("fileSize") || c.contains("fileExt") || c.contains("\"fileName\"");
    if ((isFile || isVideo) && href.startsWith("http")) {
        QString name = psField(c, "fileName");
        if (name.isEmpty()) name = title;
        if (name.isEmpty()) name = isVideo ? QString("video.mp4") : href.section('/', -1).section('?', 0, 0);
        qint64 size = psField(c, "fileSize").toLongLong();
        outType = 3;
        outContent = QString("{\"fileName\":\"%1\",\"href\":\"%2\"").arg(psEsc(name)).arg(psEsc(href));
        if (size > 0) outContent += QString(",\"fileSize\":%1").arg(size);
        outContent += "}";
        return true;
    }
    // photo
    QString ph = psPhotoJson(c);
    if (!ph.isEmpty()) {
        outType = 2;
        outContent = ph;
        return true;
    }
    // location
    if (c.contains("\"lat") || c.contains("latitude")) {
        outType = 0;
        outContent = QString::fromUtf8("[Location]");
        return true;
    }
    // anything else with readable text
    outType = 0;
    const char *textKeys[] = { "title", "description", "msg", "message", "text", "content", 0 };
    for (int i = 0; textKeys[i]; ++i) {
        QString v = psField(c, QString::fromLatin1(textKeys[i])).trimmed();
        if (!v.isEmpty() && !v.startsWith(QChar('{')) && !v.startsWith(QChar('['))) { outContent = v; return true; }
    }
    outContent = QString::fromUtf8("[Attachment]");
    return true;
}

// Short label for previews (manual matching list).
inline QString psPreviewText(int outType, const QString &outContent)
{
    switch (outType) {
    case 2: return QString::fromUtf8("[Photo]");
    case 3: return QString::fromUtf8("[File]");
    case 4: return QString::fromUtf8("[Call]");
    case 5: return QString::fromUtf8("[Sticker]");
    case 6: return QString::fromUtf8("[Link]");
    default: break;
    }
    QString t = outContent.simplified();
    if (t.size() > 80) t = t.left(77) + "...";
    return t;
}

// INSERT OR IGNORE one converted row into `messages`. Returns true if a new row was written.
inline bool psInsertMessage(sqlite3 *db, sqlite3_stmt *ins, const QString &glb, const QString &threadId,
                            const QString &content, const QString &sid, const QString &sname, qint64 tsMs,
                            bool isMine, bool isGroup, int msgType, const QString &cli)
{
    sqlite3_reset(ins);
    sqlite3_bind_text(ins, 1, glb.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 2, threadId.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 3, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 4, sid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 5, sname.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, QString::number((qlonglong)tsMs).toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (ins, 7, isMine ? 1 : 0);
    sqlite3_bind_int (ins, 8, isGroup ? 1 : 0);
    sqlite3_bind_int (ins, 9, msgType);
    sqlite3_bind_text(ins, 10, cli.toUtf8().constData(), -1, SQLITE_TRANSIENT);
    sqlite3_step(ins);
    return sqlite3_changes(db) > 0;
}

inline const char *psInsertSql()
{
    return "INSERT OR IGNORE INTO messages (msgId, threadId, content, senderId, dName, ts, isMine, isGroup, msgType, cliMsgId) "
           "VALUES (?,?,?,?,?,?,?,?,?,?)";
}

class PsDecodeThread : public QThread {
public:
    QByteArray    crypt;
    QString       keyHex;
    QString       tmpDir;
    QList<PsPart> parts;
    QString       err;
    QString       tocPreview;  // diagnostics: sanitized start of the ZDB table of contents
    QString       apiSample;   // diagnostics: masked sample of one ApiSafe row
    qint64        tocSize;
    bool          ok;
    PsDecodeThread() : tocSize(0), ok(false) {}

protected:
    void run()
    {
        using namespace PhoneSyncCore;
        std::string cryptS(crypt.constData(), (size_t)crypt.size());
        std::string plain, e;
        bool footer = false;
        if (!decryptDbCrypt(cryptS, std::string(keyHex.toLatin1().constData()), plain, footer, e)) {
            err = QString::fromUtf8(e.c_str()); return;
        }
        cryptS.clear(); crypt.clear();
        if (plain.compare(0, 6, "ZDB4.0") != 0) {
            err = QString::fromUtf8("wrong key or not a ZDB4.0 file"); return;
        }
        std::string payload;
        size_t xzOff = 0;
        if (!xzDecodeAll(plain, payload, xzOff, e)) { err = QString::fromUtf8(e.c_str()); return; }
        std::vector<std::string> names = parseTocNames(plain, xzOff);
        {
            // Digits are masked as '#', other non-printable bytes as '.', so no ids end up in the log.
            size_t n = xzOff < plain.size() ? xzOff : plain.size();
            tocSize = (qint64)n;
            if (n > 1200) n = 1200;
            QString pv;
            for (size_t k = 0; k < n; ++k) {
                unsigned char c = (unsigned char)plain[k];
                pv += (c >= '0' && c <= '9') ? QChar('#') : ((c >= 32 && c < 127) ? QChar((char)c) : QChar('.'));
            }
            tocPreview = pv;
        }
        std::vector<Span> spans = splitSqlite(payload);
        plain.clear();

        QDir().mkpath(tmpDir);
        for (size_t i = 0; i < spans.size(); ++i) {
            PsPart part;
            QString name = (names.size() == spans.size()) ? QString::fromLatin1(names[i].c_str())
                                                          : QString("part%1.db").arg((int)i);
            part.isGroup = name.startsWith("group_");
            QString digits;
            for (int k = 0; k < name.size(); ++k) if (name[k].isDigit()) digits += name[k];
            part.shortId = digits;

            QString path = tmpDir + QString("/part_%1.sqlite").arg((int)i);
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { err = "cannot write temporary file"; return; }
            f.write(payload.data() + spans[i].offset, (qint64)spans[i].size);
            f.close();

            sqlite3 *db = 0;
            if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, 0) == SQLITE_OK) {
                {
                    QStringList tabs;
                    sqlite3_stmt *ss = 0;
                    if (sqlite3_prepare_v2(db, "SELECT name FROM sqlite_master WHERE type='table'", -1, &ss, 0) == SQLITE_OK) {
                        while (sqlite3_step(ss) == SQLITE_ROW) {
                            const unsigned char *c = sqlite3_column_text(ss, 0);
                            if (c) tabs << QString::fromUtf8((const char *)c);
                        }
                        sqlite3_finalize(ss);
                    }
                    for (int t = 0; t < tabs.size(); ++t) {
                        bool safe = !tabs[t].isEmpty();
                        for (int k = 0; k < tabs[t].size(); ++k)
                            if (!(tabs[t][k].isLetterOrNumber() || tabs[t][k] == QChar('_'))) safe = false;
                        QStringList cols;
                        if (safe) {
                            QString q = QString("PRAGMA table_info(%1)").arg(tabs[t]);
                            if (sqlite3_prepare_v2(db, q.toUtf8().constData(), -1, &ss, 0) == SQLITE_OK) {
                                while (sqlite3_step(ss) == SQLITE_ROW) {
                                    const unsigned char *c = sqlite3_column_text(ss, 1);
                                    if (c) cols << QString::fromUtf8((const char *)c);
                                }
                                sqlite3_finalize(ss);
                            }
                        }
                        part.schema += tabs[t] + "(" + cols.join(",") + ") ";
                    }
                    if (tabs.contains("ApiSafe")) {
                        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM ApiSafe", -1, &ss, 0) == SQLITE_OK) {
                            if (sqlite3_step(ss) == SQLITE_ROW) part.apiRows = sqlite3_column_int(ss, 0);
                            sqlite3_finalize(ss);
                        }
                        if (part.apiRows > 0 && apiSample.isEmpty()
                            && sqlite3_prepare_v2(db, "SELECT Params, Result FROM ApiSafe LIMIT 1", -1, &ss, 0) == SQLITE_OK) {
                            if (sqlite3_step(ss) == SQLITE_ROW) {
                                for (int col = 0; col < 2; ++col) {
                                    const unsigned char *c = sqlite3_column_text(ss, col);
                                    QString v = c ? QString::fromUtf8((const char *)c) : QString();
                                    if (v.size() > 200) v = v.left(200);
                                    QString m;
                                    for (int k = 0; k < v.size(); ++k) m += v[k].isDigit() ? QChar('#') : v[k];
                                    apiSample += (col ? " | Result: " : "Params: ") + m;
                                }
                            }
                            sqlite3_finalize(ss);
                        }
                    }
                }
                sqlite3_stmt *st = 0;
                const char *sql = "SELECT SenderId, CliMsgId, GlbMsgId, MsgType, TimeStamp, MsgContent FROM ChatContent";
                if (sqlite3_prepare_v2(db, sql, -1, &st, 0) == SQLITE_OK) {
                    while (sqlite3_step(st) == SQLITE_ROW) {
                        PsRow r;
                        r.sender  = QString::number((qlonglong)sqlite3_column_int64(st, 0));
                        r.cli     = QString::number((qlonglong)sqlite3_column_int64(st, 1));
                        r.glb     = QString::number((qlonglong)sqlite3_column_int64(st, 2));
                        r.type    = sqlite3_column_int(st, 3);
                        r.ts      = (qint64)sqlite3_column_int64(st, 4);
                        const unsigned char *c = sqlite3_column_text(st, 5);
                        r.content = c ? QString::fromUtf8((const char *)c) : QString();
                        if (part.longIds.size() < 200) {
                            int run = 0;
                            for (int k = 0; k <= r.content.size(); ++k) {
                                if (k < r.content.size() && r.content[k].isDigit()) { ++run; continue; }
                                if (run >= 17 && run <= 19) part.longIds.insert(r.content.mid(k - run, run));
                                run = 0;
                            }
                        }
                        part.rows.append(r);
                    }
                    sqlite3_finalize(st);
                }
                sqlite3_close(db);
            }
            // Kept until the import: unmatched parts move to psPendingDir(), the rest are deleted then.
            part.filePath = path;
            parts.append(part);
        }
        ok = true;
    }
};

// TLS 1.2 built directly on OpenSSL (see PhoneSyncCore::httpsGet), bypassing BB10's QSslSocket.
class PsRawHttpsThread : public QThread {
public:
    QString    url, userAgent, cookie;
    QByteArray body;
    int        status;
    bool       ok;
    QString    info;
    PsRawHttpsThread() : status(0), ok(false) {}
protected:
    void run()
    {
        QUrl u = QUrl::fromEncoded(url.toUtf8());
        QByteArray host = u.host().toLatin1();
        int port = u.port(443);
        QByteArray path = u.encodedPath();
        if (path.isEmpty()) path = "/";
        if (u.hasQuery()) path += "?" + u.encodedQuery();

        for (int variant = 0; variant < 2 && !ok; ++variant) {
            std::string hdr = std::string("User-Agent: ") + userAgent.toUtf8().constData() + "\r\n"
                "Accept: */*\r\nAccept-Language: en-US\r\nCache-Control: no-store, must-revalidate\r\n"
                "Pragma: no-cache\r\nExpires: 0\r\nSec-Fetch-Dest: empty\r\nSec-Fetch-Mode: no-cors\r\nSec-Fetch-Site: none\r\n";
            if (variant == 0 && !cookie.isEmpty()) hdr += std::string("Cookie: ") + cookie.toUtf8().constData() + "\r\n";
            std::string b, inf;
            int st = 0;
            bool r = PhoneSyncCore::httpsGet(host.constData(), port, path.constData(), hdr, 30, b, st, inf);
            info += QString("[v%1 %2 http=%3 bytes=%4] ").arg(variant).arg(QString::fromLatin1(inf.c_str())).arg(st).arg((int)b.size());
            status = st;
            if (r && st >= 200 && st < 300 && b.size() >= 64) { body = QByteArray(b.data(), (int)b.size()); ok = true; }
        }
    }
};

class PsRsaThread : public QThread {
public:
    void *rsa;
    PsRsaThread() : rsa(0) {}
protected:
    void run() { rsa = PhoneSyncCore::rsaGenerate(2048); }
};

}

struct PhoneSyncRuntime {
    PsRsaThread    *rsaThread;
    PsDecodeThread *decodeThread;
    void           *rsa;
    QNetworkReply  *pullReply;
    QNetworkReply  *dlReply;
    QTimer         *timer;
    QTimer         *tick;
    int             stage;
    bool            cancelled;

    QList<PsPart>   parts;
    int             phase;
    int             pi, ri;
    qint64          totalRows, doneRows;
    QString         meShort, myName;
    QMap<QString, QString> threadLong;
    QMap<QString, QString> senderLong;
    QMap<QString, QString> senderName;
    QMap<QString, qint64>  clearedAt;
    sqlite3_stmt   *ins;
    sqlite3_stmt   *find;
    sqlite3_stmt   *pend;         // INSERT into phone_sync_pending (unmatched conversations)
    int             imported;
    int             staged;       // conversations kept for manual matching
    QSet<QString>   importedThreads;
    QSet<qint64>    stickers;     // sticker ids seen, downloaded once at the end
    QMap<int, int>  typeSeen;     // diagnostics: phone MsgType -> rows
    QMap<int, int>  typeOut;      // diagnostics: Zalo10 msgType written -> rows
    QMap<int, QString> typeKeys;  // diagnostics: JSON keys (no values) of the first row per phone MsgType

    QString         dlUrl;
    int             dlVariant;
    int             dlRedirects;
    int             dlRound;
    int             dlShort;      // retries after a download shorter than the announced size
    bool            rawTried;
    PsRawHttpsThread *rawThread;

    PhoneSyncRuntime() : rsaThread(0), decodeThread(0), rsa(0), pullReply(0), dlReply(0), timer(0), tick(0),
        stage(0), cancelled(false), phase(0), pi(0), ri(0), totalRows(0), doneRows(0), ins(0), find(0), pend(0), imported(0), staged(0),
        dlVariant(0), dlRedirects(0), dlRound(0), dlShort(0), rawTried(false), rawThread(0) {}
};

static inline void psCleanup(PhoneSyncRuntime *rt, sqlite3 *db)
{
    if (rt->timer) rt->timer->stop();
    if (rt->tick)  rt->tick->stop();
    if (rt->pullReply) { rt->pullReply->abort(); rt->pullReply->deleteLater(); rt->pullReply = 0; }
    if (rt->dlReply)   { rt->dlReply->abort();   rt->dlReply->deleteLater();   rt->dlReply = 0; }
    if (rt->ins)  { sqlite3_finalize(rt->ins);  rt->ins = 0; }
    if (rt->find) { sqlite3_finalize(rt->find); rt->find = 0; }
    if (rt->pend) { sqlite3_finalize(rt->pend); rt->pend = 0; }
    for (int i = 0; i < rt->parts.size(); ++i) {           // temporary part files not moved/deleted yet
        if (!rt->parts[i].filePath.isEmpty()) QFile::remove(rt->parts[i].filePath);
        rt->parts[i].filePath.clear();
    }
    if (rt->rsa)  { PhoneSyncCore::rsaFree(rt->rsa); rt->rsa = 0; }
    (void)db;
}

#endif
