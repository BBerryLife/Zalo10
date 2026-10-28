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
    PsPart() : isGroup(false) {}
};

class PsDecodeThread : public QThread {
public:
    QByteArray    crypt;
    QString       keyHex;
    QString       tmpDir;
    QList<PsPart> parts;
    QString       err;
    bool          ok;
    PsDecodeThread() : ok(false) {}

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
                        part.rows.append(r);
                    }
                    sqlite3_finalize(st);
                }
                sqlite3_close(db);
            }
            QFile::remove(path);
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
    int             imported;
    QSet<QString>   importedThreads;

    QString         dlUrl;
    int             dlVariant;
    int             dlRedirects;
    int             dlRound;
    bool            rawTried;
    PsRawHttpsThread *rawThread;

    PhoneSyncRuntime() : rsaThread(0), decodeThread(0), rsa(0), pullReply(0), dlReply(0), timer(0), tick(0),
        stage(0), cancelled(false), phase(0), pi(0), ri(0), totalRows(0), doneRows(0), ins(0), find(0), imported(0),
        dlVariant(0), dlRedirects(0), dlRound(0), rawTried(false), rawThread(0) {}
};

static inline void psCleanup(PhoneSyncRuntime *rt, sqlite3 *db)
{
    if (rt->timer) rt->timer->stop();
    if (rt->tick)  rt->tick->stop();
    if (rt->pullReply) { rt->pullReply->abort(); rt->pullReply->deleteLater(); rt->pullReply = 0; }
    if (rt->dlReply)   { rt->dlReply->abort();   rt->dlReply->deleteLater();   rt->dlReply = 0; }
    if (rt->ins)  { sqlite3_finalize(rt->ins);  rt->ins = 0; }
    if (rt->find) { sqlite3_finalize(rt->find); rt->find = 0; }
    if (rt->rsa)  { PhoneSyncCore::rsaFree(rt->rsa); rt->rsa = 0; }
    (void)db;
}

#endif
