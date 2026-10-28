#include "core/phonesync/PhoneSyncCommon.hpp"

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
