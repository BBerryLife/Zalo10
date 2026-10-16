#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include "PhoneSyncCore.hpp"

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

bool ZaloService::isPhoneSyncing() const { return m_psh.p != 0; }

static bool psInflate(const QByteArray &in, QByteArray &out)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return false;
    zs.next_in  = (Bytef *)in.constData();
    zs.avail_in = (uInt)in.size();
    out.clear();
    char buf[16384];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out  = (Bytef *)buf;
        zs.avail_out = sizeof(buf);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
        out.append(buf, (int)(sizeof(buf) - zs.avail_out));
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

static QString psKey(const PsPart &p) { return QString(p.isGroup ? "g:" : "u:") + p.shortId; }

void ZaloService::startPhoneSync()
{
    if (m_psh.p) return;
    if (!m_loggedIn) { emit phoneSyncFinished(false, QString::fromUtf8("You are not logged in")); return; }
    if (!m_wsConnected || !m_webSocket) {
        emit phoneSyncFinished(false, QString::fromUtf8("Not connected yet. Keep the app open for a few seconds and try again."));
        return;
    }
    if (m_fileServiceUrl.isEmpty()) {
        emit phoneSyncFinished(false, QString::fromUtf8("Missing file service address. Please log in again."));
        return;
    }
    PhoneSyncRuntime *rt = new PhoneSyncRuntime;
    m_psh.p = rt;
    rt->timer = new QTimer(this);
    rt->timer->setSingleShot(true);
    connect(rt->timer, SIGNAL(timeout()), this, SLOT(onPhoneSyncTimeout()));
    rt->tick = new QTimer(this);
    rt->tick->setSingleShot(true);
    rt->tick->setInterval(0);
    connect(rt->tick, SIGNAL(timeout()), this, SLOT(onPhoneSyncTick()));

    rt->stage = 0;
    emit phoneSyncProgress(3, QString::fromUtf8("Creating security key…"));
    rt->rsaThread = new PsRsaThread;
    connect(rt->rsaThread, SIGNAL(finished()), this, SLOT(onPhoneSyncRsaDone()));
    rt->rsaThread->start();
}

void ZaloService::cancelPhoneSync()
{
    if (!m_psh.p) return;
    m_psh.p->cancelled = true;
    phoneSyncFail(QString::fromUtf8("Sync cancelled."));
}

static void psCleanup(PhoneSyncRuntime *rt, sqlite3 *db)
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

void ZaloService::phoneSyncFail(const QString &msg)
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt) return;
    qWarning() << "[PhoneSync] FAIL:" << msg;
    psCleanup(rt, m_db);
    if (rt->rsaThread) {
        if (rt->rsaThread->isRunning()) QObject::connect(rt->rsaThread, SIGNAL(finished()), rt->rsaThread, SLOT(deleteLater()));
        else delete rt->rsaThread;
    }
    if (rt->rawThread) {
        if (rt->rawThread->isRunning()) QObject::connect(rt->rawThread, SIGNAL(finished()), rt->rawThread, SLOT(deleteLater()));
        else delete rt->rawThread;
    }
    if (rt->decodeThread) {
        if (rt->decodeThread->isRunning()) QObject::connect(rt->decodeThread, SIGNAL(finished()), rt->decodeThread, SLOT(deleteLater()));
        else delete rt->decodeThread;
    }
    rt->timer->deleteLater();
    rt->tick->deleteLater();
    delete rt;
    m_psh.p = 0;
    emit phoneSyncFinished(false, msg);
}

void ZaloService::onPhoneSyncRsaDone()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || !rt->rsaThread) return;
    rt->rsa = rt->rsaThread->rsa;
    rt->rsaThread->deleteLater();
    rt->rsaThread = 0;
    if (rt->cancelled) return;
    std::string der;
    if (!rt->rsa || !PhoneSyncCore::rsaPublicKeyDer(rt->rsa, der)) {
        phoneSyncFail(QString::fromUtf8("Sync failed: could not create the security key."));
        return;
    }
    QByteArray pub = QByteArray(der.data(), (int)der.size()).toBase64();

    QVariantMap params;
    params["pc_name"]    = "Web";
    params["public_key"] = QString::fromLatin1(pub.constData(), pub.size());
    params["from_seq_id"] = 0;
    params["is_retry"]   = 0;
    params["min_seq_id"] = 0;
    params["temp_key"]   = "";
    params["imei"]       = m_imei;
    QString enc = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString urlStr = m_fileServiceUrl + "/api/message/pull_mobile_msg"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(enc)
                   + "&nretry=0";

    rt->stage = 1;
    emit phoneSyncProgress(8, QString::fromUtf8("Sending request to your phone…"));
    rt->pullReply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(rt->pullReply, SIGNAL(finished()), this, SLOT(onPhoneSyncPullDone()));
}

void ZaloService::onPhoneSyncPullDone()
{
    PhoneSyncRuntime *rt = m_psh.p;
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!rt || !reply || reply != rt->pullReply) { if (reply) reply->deleteLater(); return; }
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    rt->pullReply = 0;
    if (rt->cancelled) return;
    QVariantMap root = jsonToMap(raw);
    if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
        phoneSyncFail(QString::fromUtf8("Sync failed: could not send the request (network error)."));
        return;
    }
    if (root.contains("error_code") && root["error_code"].toInt() != 0) {
        phoneSyncFail(QString::fromUtf8("Sync failed: Zalo rejected the request: ") + root["error_message"].toString());
        return;
    }
    rt->stage = 2;
    rt->timer->start(PS_CONFIRM_TIMEOUT_MS);
    emit phoneSyncProgress(12, QString::fromUtf8("Open Zalo on your phone and tap Agree…"));
}

void ZaloService::handlePhoneSyncEvent(const QString &act, const QVariantMap &d)
{
    PhoneSyncRuntime *rt = m_psh.p;
    qDebug() << "[PhoneSync] event act=" << act;
    if (!rt || rt->cancelled) return;

    if (act == "user_confirm") {
        int action = d["user_action"].toInt();
        if (action == 1) {
            emit phoneSyncProgress(18, QString::fromUtf8("Phone confirmed, waiting for data…"));
        } else {
            phoneSyncFail(QString::fromUtf8("Sync failed: you declined on your phone."));
        }
        return;
    }
    if (act == "transfer_error") {
        int code = d["error_code"].toInt();
        // The phone sends transfer_error with error_code=0 right after syncmsg_info; not a real error.
        if (code != 0 && rt->stage < 3) phoneSyncFail(QString::fromUtf8("Sync failed: your phone reported an error (code %1).").arg(code));
        return;
    }
    if (act == "syncmsg_info") {
        if (rt->stage >= 3) return;
        qDebug() << "[PhoneSync] sync info: size=" << d["file_size"].toLongLong() << "full=" << d["is_full_transfer"].toString();
        phoneSyncBeginDownload(d);
    }
}

void ZaloService::phoneSyncBeginDownload(const QVariantMap &d)
{
    PhoneSyncRuntime *rt = m_psh.p;
    QString url = d["url"].toString();
    QString encKey = d["encrypted_key"].toString();
    if (url.isEmpty() || encKey.isEmpty()) {
        phoneSyncFail(QString::fromUtf8("Sync failed: incomplete file information from your phone."));
        return;
    }
    std::string aesRaw;
    QByteArray cipher = QByteArray::fromBase64(encKey.toLatin1());
    if (!PhoneSyncCore::rsaPrivateDecrypt(rt->rsa, std::string(cipher.constData(), (size_t)cipher.size()), aesRaw)
        || aesRaw.size() != 32) {
        phoneSyncFail(QString::fromUtf8("Sync failed: could not decrypt the transfer key."));
        return;
    }
    rt->timer->setProperty("aesHex", QString::fromLatin1(PhoneSyncCore::toHex(aesRaw).c_str()));
    rt->timer->setProperty("fileSize", d["file_size"].toLongLong());

    rt->stage = 3;
    rt->timer->start(PS_DOWNLOAD_TIMEOUT_MS);
    emit phoneSyncProgress(25, QString::fromUtf8("Downloading data from your phone…"));
    rt->dlUrl = url;
    rt->dlVariant = 0;
    rt->dlRedirects = 0;
    phoneSyncStartDownload();
}

void ZaloService::phoneSyncStartDownload()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled) return;

    QNetworkRequest req;
    if (rt->dlVariant >= 3) {
        req = buildRequest(rt->dlUrl, "https://chat.zalo.me/", false);
        req.setRawHeader("Accept-Encoding", "gzip, deflate");
        QSslConfiguration c3 = req.sslConfiguration();
        c3.setPeerVerifyMode(QSslSocket::VerifyNone);
        c3.setProtocol(QSsl::AnyProtocol);
        req.setSslConfiguration(c3);
    } else {
        req = QNetworkRequest(QUrl::fromEncoded(rt->dlUrl.toUtf8()));
        QSslConfiguration sslConf = req.sslConfiguration();
        sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
        // BB10's QSslSocket only offers old TLS by default and new Zalo hosts need TLS 1.2: force
        // AnyProtocol (same fix as the update download in ZaloService_Network.cpp).
        sslConf.setProtocol(QSsl::AnyProtocol);
        req.setSslConfiguration(sslConf);
        req.setRawHeader("User-Agent", m_userAgent.toUtf8());
        req.setRawHeader("Accept", "*/*");
        // Sends "gzip, deflate" like zca-js; "br" is not offered because Qt can't decode it.
        req.setRawHeader("Accept-Encoding", "gzip, deflate");
        req.setRawHeader("Accept-Language", "en-US");
        req.setRawHeader("Cache-Control", "no-store, must-revalidate");
        req.setRawHeader("Pragma", "no-cache");
        req.setRawHeader("Expires", "0");
        req.setRawHeader("Sec-Fetch-Dest", "empty");
        req.setRawHeader("Sec-Fetch-Mode", "no-cors");
        req.setRawHeader("Sec-Fetch-Site", "none");
        req.setRawHeader("Connection", "keep-alive");
        QString ck = buildCookieHeader();
        if (!ck.isEmpty() && rt->dlVariant != 1) req.setRawHeader("Cookie", ck.toUtf8());
        if (rt->dlVariant == 2) {
            req.setRawHeader("Referer", "https://chat.zalo.me/");
            req.setRawHeader("Origin", "https://chat.zalo.me");
        }
    }
    rt->dlReply = m_manager->get(req);
    connect(rt->dlReply, SIGNAL(finished()), this, SLOT(onPhoneSyncDownloadDone()));
    connect(rt->dlReply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(onPhoneSyncDownloadProgress(qint64, qint64)));
}

void ZaloService::onPhoneSyncRetryDownload()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled || rt->stage != 3) return;
    phoneSyncStartDownload();
}

void ZaloService::onPhoneSyncDownloadProgress(qint64 got, qint64 total)
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->stage != 3) return;
    if (total <= 0) total = rt->timer->property("fileSize").toLongLong();
    if (total > 0) emit phoneSyncProgress(25 + (int)(20 * got / total),
                                          QString::fromUtf8("Downloading data from your phone…"));
}

void ZaloService::onPhoneSyncDownloadDone()
{
    PhoneSyncRuntime *rt = m_psh.p;
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!rt || !reply || reply != rt->dlReply) { if (reply) reply->deleteLater(); return; }
    int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    QNetworkReply::NetworkError ne = reply->error();
    QVariant loc = reply->header(QNetworkRequest::LocationHeader);
    QUrl reqUrl = reply->request().url();
    QList<QNetworkReply::RawHeaderPair> hdrs = reply->rawHeaderPairs();
    reply->deleteLater();
    rt->dlReply = 0;
    if (rt->cancelled) return;

    if (http >= 300 && http < 400 && loc.isValid() && rt->dlRedirects < 5) {
        QUrl next = reqUrl.resolved(loc.toUrl());
        rt->dlRedirects++;
        rt->dlUrl = QString::fromUtf8(next.toEncoded());
        phoneSyncStartDownload();
        return;
    }
    if (ne != QNetworkReply::NoError || http >= 400 || data.size() < 64) {
        if ((http == 400 || http == 403) && rt->dlVariant < 3) {
            rt->dlVariant++;
            rt->dlRedirects = 0;
            phoneSyncStartDownload();
            return;
        }
        phoneSyncDownloadFailed(http);
        return;
    }
    QByteArray encoding;
    for (int h = 0; h < hdrs.size(); ++h)
        if (hdrs[h].first.toLower() == "content-encoding") encoding = hdrs[h].second.toLower();
    if (encoding.contains("gzip") || encoding.contains("deflate")) {
        QByteArray plainData;
        if (psInflate(data, plainData)) data = plainData;
        else qWarning() << "[PhoneSync] WARNING: Content-Encoding" << encoding << "but inflate failed";
    }
    phoneSyncDownloadSucceeded(data);
}

void ZaloService::phoneSyncDownloadSucceeded(const QByteArray &data)
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled) return;

    rt->stage = 4;
    rt->timer->stop();
    emit phoneSyncProgress(48, QString::fromUtf8("Decrypting and unpacking…"));
    rt->decodeThread = new PsDecodeThread;
    rt->decodeThread->crypt  = data;
    rt->decodeThread->keyHex = rt->timer->property("aesHex").toString();
    rt->decodeThread->tmpDir = QDir::tempPath() + "/zalo10_phonesync";
    connect(rt->decodeThread, SIGNAL(finished()), this, SLOT(onPhoneSyncDecodeDone()));
    rt->decodeThread->start();
}

void ZaloService::phoneSyncDownloadFailed(int http)
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || rt->cancelled) return;
    if (!rt->rawTried) {
        rt->rawTried = true;
        emit phoneSyncProgress(25, QString::fromUtf8("Retrying with a secure connection…"));
        rt->rawThread = new PsRawHttpsThread;
        rt->rawThread->url       = rt->dlUrl;
        rt->rawThread->userAgent = m_userAgent;
        rt->rawThread->cookie    = buildCookieHeader();
        connect(rt->rawThread, SIGNAL(finished()), this, SLOT(onPhoneSyncRawDone()));
        rt->rawThread->start();
        return;
    }
    if ((http == 400 || http == 403 || http == 404 || http == 0) && rt->dlRound < 2) {
        rt->dlRound++;
        rt->dlVariant = 0;
        rt->dlRedirects = 0;
        rt->rawTried = false;
        emit phoneSyncProgress(25, QString::fromUtf8("Waiting for your phone to finish uploading…"));
        QTimer::singleShot(4000, this, SLOT(onPhoneSyncRetryDownload()));
        return;
    }
    phoneSyncFail(QString::fromUtf8("Sync failed: download error (HTTP %1).").arg(http));
}

void ZaloService::onPhoneSyncRawDone()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || !rt->rawThread) return;
    PsRawHttpsThread *th = rt->rawThread;
    bool ok = th->ok;
    int status = th->status;
    QByteArray body = th->body;
    QString info = th->info;
    th->deleteLater();
    rt->rawThread = 0;
    if (rt->cancelled) return;
    if (ok) { phoneSyncDownloadSucceeded(body); return; }
    phoneSyncDownloadFailed(status);
}

void ZaloService::onPhoneSyncDecodeDone()
{
    PhoneSyncRuntime *rt = m_psh.p;
    if (!rt || !rt->decodeThread) return;
    PsDecodeThread *th = rt->decodeThread;
    bool ok = th->ok;
    QString err = th->err;
    rt->parts = th->parts;
    th->deleteLater();
    rt->decodeThread = 0;
    if (rt->cancelled) return;
    if (!ok) { phoneSyncFail(QString::fromUtf8("Sync failed: could not decrypt the data: ") + err); return; }

    qint64 total = 0;
    for (int i = 0; i < rt->parts.size(); ++i) total += rt->parts[i].rows.size();
    rt->totalRows = total;
    rt->stage = 5;
    rt->phase = 0; rt->pi = 0; rt->ri = 0; rt->doneRows = 0;
    emit phoneSyncProgress(55, QString::fromUtf8("Matching conversations…"));
    rt->tick->start();
}

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
