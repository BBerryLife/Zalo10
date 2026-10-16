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
#include <QSslSocket>
#include <QSslError>
#include <sqlite3.h>

#include <openssl/aes.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <string.h>

void ZaloService::sendWsRequest(int cmd, int subCmd, const QString &jsonData)
{
    if (!m_webSocket || !m_wsConnected) return;
    static int reqId = 0;
    QString json = jsonData.trimmed();
    if (json.endsWith("}")) {
        json.chop(1);
        json += QString(",\"req_id\":\"req_%1\"}").arg(reqId++);
    }
    QByteArray payload;
    payload.append((char)1);
    payload.append((char)(cmd & 0xFF));
    payload.append((char)((cmd >> 8) & 0xFF));
    payload.append((char)(subCmd & 0xFF));
    payload.append(json.toUtf8());
    wsWriteRaw(maskWsFrame(0x2, payload));
}

void ZaloService::sendWsPing()
{
    if (!m_webSocket || !m_wsConnected) return;
    QString json = QString("{\"eventId\":%1}").arg(QDateTime::currentMSecsSinceEpoch());
    QByteArray payload;
    payload.append((char)1);
    payload.append((char)2);
    payload.append((char)0);
    payload.append((char)1);
    payload.append(json.toUtf8());
    wsWriteRaw(maskWsFrame(0x2, payload));
}

void ZaloService::connectWebSocket()
{
    if (m_realtimeSuspended) {
        qDebug() << "[WS] connectWebSocket skipped (realtime suspended - UI/service handoff)";
        return;
    }
    if (m_zpwWsUrls.isEmpty()) {
        return;
    }
    disconnectWebSocket();

    // m_wsUrls is seeded from m_zpwWsUrls once; reconnects after a failure advance m_wsUrlIndex
    // within the same list instead of resetting to host 0 (which retried the dead host forever).
    if (m_wsUrls.isEmpty()) {
        m_wsUrls     = m_zpwWsUrls;
        m_wsUrlIndex = 0;
    } else if (m_wsAdvanceUrlOnReconnect) {
        m_wsUrlIndex = (m_wsUrlIndex + 1) % m_wsUrls.size();
        qWarning() << "[WS] Previous host failed at the socket/SSL level, "
                     "advancing to pool host index" << m_wsUrlIndex;
    }
    m_wsAdvanceUrlOnReconnect = false;

    QUrl url(m_wsUrls[m_wsUrlIndex]);
    url.addQueryItem("t",        QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.addQueryItem("zpw_ver",  QString::number(API_VERSION));
    url.addQueryItem("zpw_type", QString::number(API_TYPE));

    qDebug() << "[WS] connecting to" << url.host();

    m_webSocket = new QTcpSocket(this);
    m_wsBuffer.clear();
    m_wsHandshakeSent   = false;
    m_wsConnected       = false;
    m_wsCipherKey.clear();
    m_wsSslCtx          = 0;
    m_wsSsl             = 0;
    m_wsTlsEstablished  = false;

    connect(m_webSocket, SIGNAL(connected()),          this, SLOT(onWsConnected()));
    connect(m_webSocket, SIGNAL(readyRead()),          this, SLOT(onWsReadyRead()));
    connect(m_webSocket, SIGNAL(disconnected()),       this, SLOT(onWsDisconnected()));
    connect(m_webSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onWsSocketError(QAbstractSocket::SocketError)));

    bool useSsl = (url.scheme() == "wss" || url.scheme() == "https");
    int  port   = url.port(useSsl ? 443 : 80);
    m_wsUseSsl  = useSsl;

    // BB10's QSslSocket always fails the handshake (error:1407742E) even with AnyProtocol/
    // SecureProtocols, while the bundled OpenSSL supports TLS 1.2. The WS therefore uses a plain
    // QTcpSocket and runs TLS through the OpenSSL C API (wsTlsHandshakeStep(), started from
    // onWsConnected() and resumed from onWsReadyRead()).
    m_webSocket->connectToHost(url.host(), port);

    m_webSocket->setProperty("wsUrl", url.toString());
}

static void zalo10LogOpenSslErrors(const char *context)
{
    unsigned long e;
    char buf[256];
    bool any = false;
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        qWarning() << "[WS]" << context << "OpenSSL error:" << buf;
        any = true;
    }
    if (!any)
        qWarning() << "[WS]" << context << "(no OpenSSL error queued)";
}

bool ZaloService::wsTlsHandshakeStep()
{
    if (!m_webSocket) return true;

    if (!m_wsSslCtx) {
        const SSL_METHOD *method = TLSv1_2_client_method();
        if (!method) {
            disconnectWebSocket();
            m_wsAdvanceUrlOnReconnect = true;
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
            return true;
        }
        m_wsSslCtx = SSL_CTX_new(method);
        if (!m_wsSslCtx) {
            zalo10LogOpenSslErrors("SSL_CTX_new failed");
            disconnectWebSocket();
            m_wsAdvanceUrlOnReconnect = true;
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
            return true;
        }
        // TODO: the certificate chain is not verified (same as the old unconditional
        // ignoreSslErrors()); tighten once the WS is stable.
        SSL_CTX_set_verify(m_wsSslCtx, SSL_VERIFY_NONE, 0);

        m_wsSsl = SSL_new(m_wsSslCtx);
        if (!m_wsSsl) {
            zalo10LogOpenSslErrors("SSL_new failed");
            disconnectWebSocket();
            m_wsAdvanceUrlOnReconnect = true;
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
            return true;
        }

        QString hostStr = QUrl(m_webSocket->property("wsUrl").toString()).host();
        QByteArray hostBytes = hostStr.toUtf8();
        SSL_set_tlsext_host_name(m_wsSsl, hostBytes.constData());

        // Memory-BIO pair, NOT SSL_set_fd(): QTcpSocket always drains the fd into its own buffer, so
        // OpenSSL reading the same fd never sees data and hangs silently in WANT_READ.
        // onWsReadyRead() readAll()s the socket and BIO_write()s into rbio; wsPumpTlsOutput() drains
        // wbio into QTcpSocket::write() after every SSL_* call. Only QTcpSocket touches the real fd.
        BIO *rbio = BIO_new(BIO_s_mem());
        BIO *wbio = BIO_new(BIO_s_mem());
        if (!rbio || !wbio) {
            if (rbio) BIO_free(rbio);
            if (wbio) BIO_free(wbio);
            zalo10LogOpenSslErrors("BIO_new failed");
            disconnectWebSocket();
            m_wsAdvanceUrlOnReconnect = true;
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
            return true;
        }
        // An empty memory BIO reports EOF by default, which OpenSSL would take as a clean server close.
        // set_mem_eof_return(-1) makes it report "would block" instead.
        BIO_set_mem_eof_return(rbio, -1);
        BIO_set_mem_eof_return(wbio, -1);
        SSL_set_bio(m_wsSsl, rbio, wbio);
        SSL_set_connect_state(m_wsSsl);
    }

    int ret = SSL_connect(m_wsSsl);
    wsPumpTlsOutput();

    if (ret == 1) {
        qDebug() << "[WS] TLS handshake OK," << SSL_get_version(m_wsSsl);
        m_wsTlsEstablished = true;
        onWsEncrypted();
        return true;
    }

    int sslErr = SSL_get_error(m_wsSsl, ret);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        // With memory BIOs WANT_WRITE is practically impossible; WANT_READ means we are waiting for the
        // server and onWsReadyRead() resumes the handshake when more ciphertext arrives.
        return false;
    }

    qWarning() << "[WS] SSL_connect failed, SSL_get_error=" << sslErr;
    zalo10LogOpenSslErrors("SSL_connect");
    m_wsAdvanceUrlOnReconnect = true;
    disconnectWebSocket();
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(wsNextReconnectDelayMs());
    return true;
}

// Drain the ciphertext OpenSSL wrote to wbio and send it via QTcpSocket::write().
// Call after every SSL_* call that may produce output.
void ZaloService::wsPumpTlsOutput()
{
    if (!m_wsSsl || !m_webSocket) return;
    BIO *wbio = SSL_get_wbio(m_wsSsl);
    if (!wbio) return;
    char buf[4096];
    int n;
    while ((n = BIO_read(wbio, buf, sizeof(buf))) > 0) {
        m_webSocket->write(buf, n);
    }
}

// Single write path: SSL_write() through the memory BIO when m_wsUseSsl, else plain
// QTcpSocket::write(). SSL_write() practically never returns WANT_* here.
qint64 ZaloService::wsWriteRaw(const QByteArray &data)
{
    if (!m_webSocket) return -1;
    if (!m_wsUseSsl || !m_wsSsl || !m_wsTlsEstablished)
        return m_webSocket->write(data);

    int n = SSL_write(m_wsSsl, data.constData(), data.size());
    wsPumpTlsOutput();
    if (n <= 0) {
        int sslErr = SSL_get_error(m_wsSsl, n);
        qWarning() << "[WS] SSL_write failed, SSL_get_error=" << sslErr;
        zalo10LogOpenSslErrors("SSL_write");
        return -1;
    }
    return n;
}

QByteArray ZaloService::wsReadDecrypted()
{
    QByteArray out;
    if (!m_wsSsl) return out;
    char buf[4096];
    while (true) {
        int n = SSL_read(m_wsSsl, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, n);
            continue;
        }
        int sslErr = SSL_get_error(m_wsSsl, n);
        if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE)
            break;
        if (sslErr == SSL_ERROR_ZERO_RETURN)
            break;
        zalo10LogOpenSslErrors("SSL_read");
        break;
    }
    return out;
}

void ZaloService::closeWebSocketGracefully()
{
    if (!m_webSocket || !m_wsConnected) {
        return;
    }

    wsWriteRaw(maskWsFrame(0x8, QByteArray()));
    m_webSocket->flush();
    m_webSocket->waitForBytesWritten(300);
}

void ZaloService::disconnectWebSocket()
{
    if (m_wsSsl) {
        SSL_free(m_wsSsl);
        m_wsSsl = 0;
    }
    if (m_wsSslCtx) {
        SSL_CTX_free(m_wsSslCtx);
        m_wsSslCtx = 0;
    }
    m_wsTlsEstablished = false;

    if (m_webSocket) {
        m_webSocket->disconnect();
        m_webSocket->abort();
        m_webSocket->deleteLater();
        m_webSocket = 0;
    }
    m_wsConnected     = false;
    m_wsHandshakeSent = false;
    m_wsCipherKey.clear();
    m_wsBuffer.clear();
}

void ZaloService::suspendRealtime()
{
    m_realtimeSuspended = true;
    if (m_wsReconnectTimer) m_wsReconnectTimer->stop();
    if (m_listenTimer)      m_listenTimer->stop();
    if (m_keepAliveTimer)   m_keepAliveTimer->stop();
    closeWebSocketGracefully();
    disconnectWebSocket();
    if (m_loggedIn) {
        m_loggedIn = false;
        emit loggedInChanged();
    }
}

bool ZaloService::resumeRealtime()
{
    m_realtimeSuspended       = false;
    m_wsConsecutiveFailCount  = 0;
    m_wsAdvanceUrlOnReconnect = false;
    m_renewRetry              = 0;
    return loadSession();
}

void ZaloService::onWsConnected()
{
    if (!m_webSocket) return;
    if (m_wsUseSsl) {
        wsTlsHandshakeStep();
        return;
    }
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::onWsEncrypted()
{
    if (!m_webSocket) return;
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::sendWsHandshake(const QUrl &url)
{
    if (m_wsHandshakeSent) return;
    m_wsHandshakeSent = true;

    QByteArray keyBytes;
    for (int i = 0; i < 16; ++i)
        keyBytes.append((char)(qrand() % 256));
    QString wsKey = keyBytes.toBase64();

    QByteArray raw = (wsKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toUtf8();
    m_wsExpectedAccept = QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toBase64();

    QString path = url.path();
    if (path.isEmpty()) path = "/";
    if (!url.encodedQuery().isEmpty()) path += "?" + url.encodedQuery();

    QString handshake =
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + url.host() + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + wsKey + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Origin: https://chat.zalo.me\r\n"
        "User-Agent: " + m_userAgent + "\r\n"
        "Cookie: " + buildCookieHeader() + "\r\n"
        "Accept-Language: en-US,en;q=0.9\r\n"
        "\r\n";

    wsWriteRaw(handshake.toUtf8());
}

void ZaloService::onWsSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
}

void ZaloService::onWsSocketError(QAbstractSocket::SocketError err)
{
    if (!m_webSocket) return;
    qWarning() << "[WS] Socket error:" << err
              << m_webSocket->errorString();
    m_wsAdvanceUrlOnReconnect = true;
}

void ZaloService::onWsReadyRead()
{
    if (!m_webSocket) return;

    QByteArray raw = m_webSocket->readAll();
    if (m_wsUseSsl && m_wsSsl && !raw.isEmpty()) {
        BIO *rbio = SSL_get_rbio(m_wsSsl);
        if (rbio) BIO_write(rbio, raw.constData(), raw.size());
    }

    if (m_wsUseSsl && !m_wsTlsEstablished) {
        wsTlsHandshakeStep();
        return;
    }

    m_wsBuffer += m_wsUseSsl ? wsReadDecrypted() : raw;
    if (m_wsUseSsl) wsPumpTlsOutput();

    if (!m_wsConnected) {
        int headerEnd = 0;
        if (parseWsHandshakeResponse(m_wsBuffer, headerEnd)) {
            qDebug() << "[WS] Upgraded OK, WebSocket connected";
            m_wsConnected = true;
            m_wsConsecutiveFailCount = 0;
            m_wsBuffer    = m_wsBuffer.mid(headerEnd);
        } else if (m_wsBuffer.contains("\r\n\r\n")) {
            qWarning() << "[WS] upgrade failed";
            disconnectWebSocket();
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
            return;
        } else {
            return;
        }
    }

    while (m_wsBuffer.size() >= 2) {
        bool  fin    = (m_wsBuffer[0] & 0x80) != 0;
        int   opcode = (m_wsBuffer[0] & 0x0F);
        bool  masked = (m_wsBuffer[1] & 0x80) != 0;
        quint64 payLen = (m_wsBuffer[1] & 0x7F);

        int headerSize = 2;
        if (payLen == 126) {
            if (m_wsBuffer.size() < 4) break;
            payLen = ((quint8)m_wsBuffer[2] << 8) | (quint8)m_wsBuffer[3];
            headerSize = 4;
        } else if (payLen == 127) {
            if (m_wsBuffer.size() < 10) break;
            payLen = 0;
            for (int i = 0; i < 8; ++i)
                payLen = (payLen << 8) | (quint8)m_wsBuffer[2 + i];
            headerSize = 10;
        }

        if (masked) headerSize += 4;
        if ((int)(headerSize + payLen) > m_wsBuffer.size()) break;

        QByteArray payload = m_wsBuffer.mid(headerSize, payLen);
        if (masked) {
            QByteArray maskKey = m_wsBuffer.mid(headerSize - 4, 4);
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = payload[i] ^ maskKey[i % 4];
        }

        m_wsBuffer = m_wsBuffer.mid(headerSize + payLen);

        Q_UNUSED(fin);
        handleWsFrame(opcode, payload);
    }
}

bool ZaloService::parseWsHandshakeResponse(const QByteArray &data, int &headerEnd)
{
    int idx = data.indexOf("\r\n\r\n");
    if (idx < 0) return false;
    headerEnd = idx + 4;
    QString resp = QString::fromUtf8(data.left(headerEnd));
    return resp.contains("101") && resp.contains("Upgrade", Qt::CaseInsensitive);
}

void ZaloService::handleWsFrame(int opcode, const QByteArray &payload)
{
    if (opcode == 0x8) {
        onWsDisconnected();
        return;
    }
    if (opcode == 0x9) {
        QByteArray pong = maskWsFrame(0xA, payload);
        wsWriteRaw(pong);
        return;
    }
    if (opcode != 0x2 && opcode != 0x1) return;

    handleWsMessage(opcode, payload);
}

QVariantMap ZaloService::decodeWsEnvelope(const QVariantMap &outer, const QString &debugTag)
{
    QVariantMap d;
    int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
    if (encType == 0) {
        d = outer.contains("data") && outer["data"].type() == QVariant::Map
            ? outer["data"].toMap() : outer;
    } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
        QString rawB64 = outer["data"].toString();
        if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
        QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
        QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
        if (encType == 2 && !plain.isEmpty()) {
            QByteArray inflated;
            inflated.resize(plain.size() * 8 + 4096);
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in  = (Bytef*)plain.constData();
            zs.avail_in = plain.size();
            bool inflateOk = false;
            if (inflateInit2(&zs, 15 + 16) == Z_OK) {
                int outPos = 0, ret2 = Z_OK;
                do {
                    if (outPos >= inflated.size())
                        inflated.resize(inflated.size() * 2);
                    zs.next_out  = (Bytef*)(inflated.data() + outPos);
                    zs.avail_out = inflated.size() - outPos;
                    ret2 = inflate(&zs, Z_SYNC_FLUSH);
                    outPos = zs.total_out;
                } while ((ret2 == Z_OK || ret2 == Z_BUF_ERROR) && zs.avail_in > 0);
                inflateEnd(&zs);
                if ((ret2 == Z_STREAM_END || ret2 == Z_OK) && zs.total_out > 0) {
                    inflated.resize(zs.total_out);
                    inflateOk = true;
                }
            }
            if (inflateOk) {
                QVariantMap parsed = jsonToMap(inflated);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qWarning() << "[WS] inflate FAILED" << debugTag << ", trying raw plain";
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            }
        } else if (!plain.isEmpty()) {
            QVariantMap parsed = jsonToMap(plain);
            if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                d = parsed["data"].toMap();
            else
                d = parsed;
        } else {
        }
    } else {
        QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
        if (dec.isEmpty() || dec.trimmed() == "{}")
            dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
        QVariantMap r = jsonToMap(dec.toUtf8());
        d = r.contains("data") ? r["data"].toMap() : r;
    }
    return d;
}

static QByteArray inflateAuto(const QByteArray &in)
{
    if (in.isEmpty()) return QByteArray();
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in  = (Bytef*)in.constData();
    zs.avail_in = in.size();
    if (inflateInit2(&zs, 15 + 32) != Z_OK) return QByteArray();
    QByteArray out;
    out.resize(in.size() * 8 + 4096);
    int outPos = 0, ret = Z_OK;
    do {
        if (outPos >= out.size()) out.resize(out.size() * 2);
        zs.next_out  = (Bytef*)(out.data() + outPos);
        zs.avail_out = out.size() - outPos;
        ret = inflate(&zs, Z_SYNC_FLUSH);
        outPos = zs.total_out;
    } while (ret == Z_OK && zs.avail_in > 0);
    inflateEnd(&zs);
    if (zs.total_out == 0) return QByteArray();
    out.resize(zs.total_out);
    return out;
}

// True if the uid list contains anyone other than us (delivered/seen events with only our own
// uid come from our other devices).
static bool uidListHasOther(const QVariantList &uids, const QString &self)
{
    for (int i = 0; i < uids.size(); ++i)
        if (uids[i].toString() != self) return true;
    return false;
}

void ZaloService::handleWsMessage(int , const QByteArray &payload)
{
    if (payload.size() < 4) return;

    quint8  version = (quint8)payload[0];
    qint32  cmd     = (qint32)((quint8)payload[1] | ((quint8)payload[2] << 8));
    qint32  subCmd  = (qint32)((quint8)payload[3]);
    QByteArray data = payload.mid(4);

    if (cmd == 1 && subCmd == 1) {
        QVariantMap parsed = jsonToMap(data);
        QString keyB64 = parsed["key"].toString();
        m_wsCipherKey = QByteArray::fromBase64(keyB64.toUtf8());
        qDebug() << "[WS] handshake OK";

        sendWsPing();
        if (m_listenTimer) m_listenTimer->start(25000);
        if (!m_pending510Toid.isEmpty()) {
            QString req510 = QString("{\"first\":true,\"lastId\":null,\"toid\":\"%1\",\"preIds\":[]}")
                             .arg(m_pending510Toid);
            sendWsRequest(510, 1, req510);
        }
        fetchPinConversations();
        QTimer::singleShot(1500, this, SLOT(onSyncStartDeferred()));
        return;
    }

    if ((cmd == 501 || cmd == 521) && subCmd == 0) {
        bool isGroup = (cmd == 521);

        QVariantMap outer = jsonToMap(data);
        QVariantMap d = decodeWsEnvelope(outer, "cmd501");

        QVariantList msgs;
        if (isGroup) {
            msgs = d["groupMsgs"].toList();
            if (msgs.isEmpty()) msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
        } else {
            msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
            if (msgs.isEmpty()) msgs = d["groupMsgs"].toList();
        }
        if (msgs.isEmpty()) {
            QVariantMap dd = d["data"].toMap();
            msgs = isGroup ? dd["groupMsgs"].toList() : dd["msgs"].toList();
            if (msgs.isEmpty()) msgs = isGroup ? dd["msgs"].toList() : dd["ms"].toList();
            if (msgs.isEmpty()) msgs = dd["ms"].toList();
        }

        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap m = msgs[i].toMap();

            QString rawUidFrom = m["uidFrom"].toString();
            QString rawIdTo    = m["idTo"].toString();
            bool isSelf = (rawUidFrom == "0");

            QString uidFrom = isSelf      ? m_uid : rawUidFrom;
            QString idTo    = (rawIdTo == "0") ? m_uid : rawIdTo;

            QString threadId;
            if (isGroup) {
                threadId = idTo;
            } else {
                threadId = isSelf ? idTo : uidFrom;
            }
            if (threadId.isEmpty() || threadId == m_uid) continue;

            if (isGroupPollNotice(m)) {
                QString pollIdN = extractPollNoticeId(m);
                emit boardEventOccurred(threadId, "update_board", QString(), isSelf, 3, QString(), QString());
                continue;
            }

            QString msgId = m["msgId"].toString();
            if (!msgId.isEmpty()) {
                if (m_seenMsgIds.contains(msgId)) {
                    // Already saved, usually from the HTTP response of our own message, which had no server ts and
                    // used the local clock. The WS push carries the real ts: patch the row instead of skipping it.
                    QString serverTs = m["ts"].toString();
                    if (!serverTs.isEmpty() && m_db) {
                        const char *sqlFixTs = "UPDATE messages SET ts=? WHERE msgId=?";
                        sqlite3_stmt *fixStmt = 0;
                        if (sqlite3_prepare_v2(m_db, sqlFixTs, -1, &fixStmt, 0) == SQLITE_OK) {
                            sqlite3_bind_text(fixStmt, 1, serverTs.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(fixStmt, 2, msgId.toUtf8().constData(),    -1, SQLITE_TRANSIENT);
                            sqlite3_step(fixStmt);
                            sqlite3_finalize(fixStmt);
                        }
                        emit messageTsCorrected(threadId, msgId, serverTs);
                    }
                    continue;
                }
                m_seenMsgIds.insert(msgId);
            }
            QString cliMsgId = m["cliMsgId"].toString();

            // Tombstone guard (same as the cmd=510 history path): a "delete for me" msgId never reaches
            // UI/DB again, even on duplicate pushes.
            if (isMessageDeletedForMe(msgId)) {
                continue;
            }

            QString recalledId = extractRecalledMsgId(m);
            if (!recalledId.isEmpty()) {
                markMessageRecalled(threadId, recalledId);
                continue;
            }

            QString delMsgId, deleterUid;
            if (extractDeleteInfo(m, delMsgId, deleterUid)) {
                if (deleterUid == m_uid) {
                    markMessageDeletedForMe(threadId, delMsgId);
                }
                continue;
            }

            QString reactMsgId, reactUid, reactIcon;
            if (extractReactionInfo(m, reactMsgId, reactUid, reactIcon)) {
                if (reactIcon.isEmpty()) dbRemoveReaction(reactMsgId, reactUid);
                else                     dbSaveReaction(threadId, reactMsgId, reactUid, reactIcon);
                emit reactionUpdated(threadId, reactMsgId, reactUid, reactIcon);
                continue;
            }

            QVariantMap out;
            out["msgId"]    = msgId;
            out["cliMsgId"] = cliMsgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = isGroup;
            out["isMine"]   = isSelf;
            out["msgType"]  = m["msgType"].toInt();

            // quote object: globalMsgId = quoted message id, "msg" = snippet, "fromD" = quoted sender name.
            // Empty quoteMsgId means the message is not a reply.
            QVariantMap quoteObj = m["quote"].toMap();
            if (!quoteObj.isEmpty()) {
                // globalMsgId/ownerId arrive as exact strings: quoteBigJsonInts() quotes bare 16+ digit numbers
                // before JSON.parse, which would otherwise round them (860110201644973228 -> ...200).
                out["quoteMsgId"]      = quoteObj["globalMsgId"].toString();
                out["quoteContent"]    = quoteObj["msg"].toString();
                out["quoteSenderName"] = quoteObj["fromD"].toString();
                out["quoteMsgType"]    = quoteObj["cliMsgType"].toInt();
                // ownerId = real uid of the quoted message's sender. In 1-1 chats Zalo reports "0" (self) for
                // our own quoted messages; it must be mapped to our uid or quoteIsMine is always false.
                QString qOwnerIdStr = quoteObj["ownerId"].toString();
                out["quoteOwnerId"] = (qOwnerIdStr == "0") ? m_uid : qOwnerIdStr;
            }

            int mt = m["msgType"].toInt();
            if (mt == 0) {
                mt = m["type"].toInt();
                if (mt == 0) mt = m["mt"].toInt();
                if (mt == 0) mt = m["msgtype"].toInt();
            }
            if (mt == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mt = 2;
                else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                    mt = 3;
                // chat.recommended covers two bubble kinds, told apart by content.action (confirmed on device,
                // not in zca-js): "recommened.calltime"/"recommened.misscall" = call log (params JSON with
                // duration/calltype/isCaller), "recommened.link" = link share (title/description/href/thumb).
                // Read the action BEFORE setting mt, or link shares show up as voice calls.
                // Call logs are normalized to {callResult, callKind, duration}.
                else if (mtStr.contains("chat.recommended"))
                {
                    QVariantMap peekMap = m["content"].toMap();
                    if (peekMap.isEmpty()) {
                        QString peekStr = m["content"].toString();
                        if (!peekStr.isEmpty() && peekStr.trimmed().startsWith("{"))
                            peekMap = jsonToMap(peekStr.toUtf8());
                    }
                    QString peekAction = peekMap["action"].toString();
                    mt = peekAction.contains("link") ? 6 : 4;
                }
                // Sticker content is {"id","catId","type"}: id plugs straight into
                // https://zalo-api.zadn.vn/api/emoticon/sticker/webpc?eid={id}; catId and getStickersDetail
                // aren't needed to display received stickers. Normalized to {"stickerId":N}.
                else if (mtStr.contains("chat.sticker"))
                    mt = 5;
            }

            if (mt == 5) {
                QVariantMap sm = m["content"].toMap();
                if (sm.isEmpty()) {
                    QString sStr = m["content"].toString();
                    if (!sStr.isEmpty() && sStr.trimmed().startsWith("{"))
                        sm = jsonToMap(sStr.toUtf8());
                }
                qint64 stickerId = sm["id"].toString().toLongLong();
                if (stickerId == 0) stickerId = sm["id"].toLongLong();
                QString newContent = QString("{\"stickerId\":%1}").arg(stickerId);
                m["content"] = newContent;
                // Start the download here in C++: QML init signals (Component.onCompleted/onVisibleChanged)
                // proved unreliable from the nested delegate. downloadSticker() dedups; QML only reads
                // stickerLocalPath.
                if (stickerId > 0) downloadSticker(QString::number(stickerId));
            }

            if (mt == 6) {
                QVariantMap lm = m["content"].toMap();
                if (lm.isEmpty()) {
                    QString lStr = m["content"].toString();
                    if (!lStr.isEmpty() && lStr.trimmed().startsWith("{"))
                        lm = jsonToMap(lStr.toUtf8());
                }
                QString lTitle = lm["title"].toString();
                QString lDesc  = lm["description"].toString();
                QString lHref  = lm["href"].toString();
                QString lThumb = lm["thumb"].toString();
                QString esc = lTitle; esc.replace("\\", "\\\\").replace("\"", "\\\"");
                QString descEsc = lDesc; descEsc.replace("\\", "\\\\").replace("\"", "\\\"");
                QString hrefEsc = lHref; hrefEsc.replace("\\", "\\\\").replace("\"", "\\\"");
                QString thumbEsc = lThumb; thumbEsc.replace("\\", "\\\\").replace("\"", "\\\"");
                QString newContent = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                                          .arg(esc).arg(descEsc).arg(hrefEsc).arg(thumbEsc);
                m["content"] = newContent;
            }

            if (mt == 4) {
                QVariantMap rm = m["content"].toMap();
                if (rm.isEmpty()) {
                    QString rStr = m["content"].toString();
                    if (!rStr.isEmpty() && rStr.trimmed().startsWith("{"))
                        rm = jsonToMap(rStr.toUtf8());
                }
                QString action = rm["action"].toString();
                QVariant paramsV = rm["params"];
                QVariantMap paramsMap = (paramsV.type() == QVariant::String)
                    ? jsonToMap(paramsV.toString().toUtf8())
                    : paramsV.toMap();
                QString callResult = action.contains("misscall") ? "missed" : "ended";
                QString callKind   = (paramsMap["calltype"].toInt() == 1) ? "video" : "voice";
                qint64  duration   = paramsMap["duration"].toString().toLongLong();
                QString newContent = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}")
                                          .arg(callResult).arg(callKind).arg(duration);
                m["content"] = newContent;
            }

            if (mt == 3) {
                QVariantMap fm = m["content"].toMap();
                if (fm.isEmpty()) {
                    QString fStr = m["content"].toString();
                    if (!fStr.isEmpty() && fStr.trimmed().startsWith("{"))
                        fm = jsonToMap(fStr.toUtf8());
                }
                QString fTitle = fm["title"].toString();
                QString fHref  = fm["href"].toString();
                qint64  fSize  = 0;
                QVariant paramsV = fm["params"];
                QVariantMap paramsMap = (paramsV.type() == QVariant::String)
                    ? jsonToMap(paramsV.toString().toUtf8())
                    : paramsV.toMap();
                if (!paramsMap.isEmpty())
                    fSize = paramsMap["fileSize"].toString().toLongLong();

                if (!fHref.isEmpty()) {
                    QString fTitleEsc = fTitle;
                    fTitleEsc.replace("\\", "\\\\").replace("\"", "\\\"");
                    QString newContent = QString("{\"fileName\":\"%1\",\"href\":\"%2\"")
                                              .arg(fTitleEsc).arg(fHref);
                    if (fSize > 0) newContent += QString(",\"fileSize\":%1").arg(fSize);
                    newContent += "}";
                    m["content"] = newContent;
                } else {
                }
            }

            // Qt4's QScriptEngine converts nested JSON objects to QVariantMap, so content.toString() is
            // empty for photo/object payloads: re-serialize to a JSON string.
            QString rawContent = m["content"].toString();
            if (rawContent.isEmpty()) {
                QVariantMap cm = m["content"].toMap();
                if (!cm.isEmpty()) {
                    rawContent = "{";
                    QStringList cmKeys = cm.keys();
                    for (int k = 0; k < cmKeys.size(); ++k) {
                        if (k > 0) rawContent += ",";
                        QString v = cm[cmKeys[k]].toString();
                        v.replace("\\", "\\\\").replace("\"", "\\\"");
                        rawContent += "\"" + cmKeys[k] + "\":\"" + v + "\"";
                    }
                    rawContent += "}";
                }
            }

            // No content: fall back to paramsExt/previewThumb, where realtime WS sometimes carries photo data.
            if (rawContent.isEmpty()) {
                QString paramsExtStr = m["paramsExt"].toString();
                QString previewThumb = m["previewThumb"].toString();
            }

            // Realtime photo: msgType may be 0 with URLs in paramsExt/previewThumb. mt == 3 (video/file) is
            // excluded so a share.file without href stays text instead of being taken for a photo.
            if (mt != 3 && mt != 4 && mt != 5 && (mt == 2 || rawContent.isEmpty())) {
                QString normalized = normalizePhotoContent(m, rawContent);
                if (normalized != rawContent && !normalized.isEmpty()) {
                    rawContent = normalized;
                    mt = 2;
                    out["msgType"] = 2;
                }
            }
            if (mt == 2) {
                QString nUrl, hUrl, tUrl, fSizeStr;
                if (!rawContent.isEmpty() && rawContent.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContent.toUtf8());
                    nUrl = cm["normalUrl"].toString();
                    hUrl = cm["hdUrl"].toString();
                    tUrl = cm["thumbUrl"].toString();
                    if (nUrl.isEmpty()) nUrl = cm["href"].toString();
                    if (hUrl.isEmpty()) hUrl = cm["oriUrl"].toString();
                    if (tUrl.isEmpty()) tUrl = cm["thumb"].toString();
                    if (fSizeStr.isEmpty()) fSizeStr = cm["hdSize"].toString();
                    if (fSizeStr.isEmpty()) fSizeStr = cm["fileSize"].toString();
                }
                if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumb"].toString();
                if (fSizeStr.isEmpty()) fSizeStr = m["hdSize"].toString();
                if (nUrl.isEmpty()) {
                    QVariantMap att = m["attach"].toMap();
                    if (att.isEmpty()) {
                        QString attStr = m["attach"].toString();
                        if (!attStr.isEmpty() && attStr.startsWith("{"))
                            att = jsonToMap(attStr.toUtf8());
                    }
                    if (!att.isEmpty()) {
                        if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                        if (hUrl.isEmpty()) hUrl = att["hdUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = att["thumbUrl"].toString();
                        if (nUrl.isEmpty()) nUrl = att["href"].toString();
                        if (nUrl.isEmpty()) nUrl = att["normalUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = att["thumb"].toString();
                        if (fSizeStr.isEmpty()) fSizeStr = att["hdSize"].toString();
                    }
                }
                if (nUrl.isEmpty()) {
                    QVariantMap prm = m["params"].toMap();
                    if (prm.isEmpty()) {
                        QString prmStr = m["params"].toString();
                        if (!prmStr.isEmpty() && prmStr.startsWith("{"))
                            prm = jsonToMap(prmStr.toUtf8());
                    }
                    if (!prm.isEmpty()) {
                        if (nUrl.isEmpty()) nUrl = prm["normalUrl"].toString();
                        if (hUrl.isEmpty()) hUrl = prm["hdUrl"].toString();
                        if (tUrl.isEmpty()) tUrl = prm["thumbUrl"].toString();
                        if (nUrl.isEmpty()) nUrl = prm["href"].toString();
                        if (tUrl.isEmpty()) tUrl = prm["thumb"].toString();
                        if (fSizeStr.isEmpty()) fSizeStr = prm["hdSize"].toString();
                    }
                }

                if (nUrl.isEmpty()) nUrl = hUrl;
                if (nUrl.isEmpty()) nUrl = tUrl;
                if (tUrl.isEmpty()) tUrl = nUrl;
                if (hUrl.isEmpty()) hUrl = nUrl;

                if (!nUrl.isEmpty()) {
                    QString caption;
                    if (!rawContent.isEmpty() && rawContent.contains("\"caption\":\"")) {
                        QVariantMap prevCm = jsonToMap(rawContent.toUtf8());
                        caption = prevCm["caption"].toString();
                    }
                    if (caption.isEmpty()) {
                        QVariantMap cm = jsonToMap(m["content"].toString().toUtf8());
                        if (!cm.isEmpty()) {
                            caption = cm["title"].toString();
                            if (caption.isEmpty()) caption = cm["description"].toString();
                        }
                    }
                    if (caption.isEmpty()) caption = m["title"].toString();

                    if (!caption.isEmpty()) {
                        caption.replace("\\", "\\\\").replace("\"", "\\\"")
                               .replace("\n", "\\n").replace("\r", "\\r")
                               .replace("\t", "\\t");
                    }

                    // For the echo of our own photo, prefer fileSize/fileName known from sendPhoto() over the
                    // server's (possibly missing) hdSize.
                    QVariantMap sentInfo;
                    bool haveSentInfo = false;
                    if (isSelf && !cliMsgId.isEmpty() && m_pendingSentPhotoInfo.contains(cliMsgId)) {
                        sentInfo = m_pendingSentPhotoInfo.value(cliMsgId);
                        haveSentInfo = true;
                    }

                    qint64 fSize = haveSentInfo ? sentInfo.value("fileSize", 0).toLongLong()
                                                 : fSizeStr.toLongLong();
                    QString fName = haveSentInfo ? sentInfo.value("fileName").toString() : QString();
                    if (!fName.isEmpty()) fName.replace("\\", "\\\\").replace("\"", "\\\"");

                    rawContent = QString("{\"normalUrl\":\"%1\",\"thumbUrl\":\"%2\",\"hdUrl\":\"%3\"")
                                     .arg(nUrl).arg(tUrl).arg(hUrl);
                    if (fSize > 0) rawContent += QString(",\"fileSize\":%1").arg(fSize);
                    if (!fName.isEmpty()) rawContent += QString(",\"fileName\":\"%1\"").arg(fName);
                    if (!caption.isEmpty()) rawContent += QString(",\"caption\":\"%1\"").arg(caption);
                    rawContent += "}";

                    // Self-echo of our own photo: reuse the local cached file. Fetching the CDN copy right after
                    // upload races (can 404/be empty) and left our own photos as permanent grey boxes. The file
                    // was copied to the persistent "/tmp" cache at pick time (cacheLocalImage()) and is only
                    // removed by clearCache().
                    if (haveSentInfo) {
                        QString localP = sentInfo.value("localPath").toString();
                        QString fsPath = localP.startsWith("file://") ? localP.mid(7) : localP;
                        if (!fsPath.isEmpty() && QFile::exists(fsPath)) {
                            QSize dim = imageDimensions(localP);
                            out["localImage"] = localP;
                            out["imgWidth"]   = dim.width();
                            out["imgHeight"]  = dim.height();
                            m_avatarCache[nUrl] = localP;
                        }
                        m_pendingSentPhotoInfo.remove(cliMsgId);
                    }
                }
                if (nUrl.isEmpty()) {
                    qWarning() << "[WS] photo message without URL";
                }
                // nUrl is not an http URL (Zalo protobuf thumbnail): decode the thumbnail for a quick preview,
                // then fetch the full-res URL via WS cmd=510 history.
                if (!nUrl.isEmpty() && !nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    downloadImageMessage(msgId, nUrl, threadId);
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        fetchPhotoViaWs510(msgId, threadId);
                        fetchPhotoViaHttp(msgId, threadId);
                    }
                } else if (!nUrl.isEmpty() && nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    downloadImageMessage(msgId, nUrl, threadId);
                }
            }
            // Always sync out["msgType"] with the locally resolved mt: Zalo sometimes sends msgType as a
            // string ("chat.photo") and QVariant::toInt() silently returns 0, which left photo bubbles
            // empty because QML only copies the cached local image when msgType === 2.
            out["msgType"] = mt;
            out["content"] = rawContent;

            dbSaveMessage(out, threadId);
            // Keep m_threadLastMsgId updated for realtime messages too (not only cmd=510); otherwise
            // fetchMessages() falls back to lastId="0" and re-fetches the whole history.
            if (!msgId.isEmpty()) {
                qint64 numH = msgId.toLongLong();
                if (numH > m_threadLastMsgId.value(threadId, "0").toLongLong())
                    m_threadLastMsgId[threadId] = msgId;
            }
            emit newMessage(threadId, out);
            if (!isSelf && threadId != m_activeThreadId && !m_mutedThreads.contains(threadId)) {
                QString senderName = out["dName"].toString();
                if (senderName.isEmpty()) senderName = "Unknown";
                int mt = out["msgType"].toInt();
                QString msgPreview;
                if (mt == 2) {
                    msgPreview = "[Photo]";
                } else if (mt == 3) {
                    QString cs = out["content"].toString();
                    QString fn;
                    int fi = cs.indexOf("\"fileName\":\"");
                    if (fi >= 0) {
                        fi += 12;
                        int fe = cs.indexOf("\"", fi);
                        if (fe > fi) fn = cs.mid(fi, fe - fi);
                    }
                    msgPreview = fn.isEmpty() ? "[File]" : ("[File] " + fn);
                } else if (mt == 4) {
                    QString cs = out["content"].toString();
                    bool missed = cs.contains("\"callResult\":\"missed\"");
                    bool video  = cs.contains("\"callKind\":\"video\"");
                    msgPreview = missed ? (video ? "Missed video call" : "Missed call")
                                        : (video ? "Video call" : "Voice call");
                } else if (mt == 5) {
                    msgPreview = "[Sticker]";
                } else if (mt == 6) {
                    QString cs = out["content"].toString();
                    QString lt;
                    int li = cs.indexOf("\"linkTitle\":\"");
                    if (li >= 0) {
                        li += 13;
                        int le = cs.indexOf("\"", li);
                        if (le > li) lt = cs.mid(li, le - li);
                    }
                    msgPreview = lt.isEmpty() ? "[Link]" : lt.left(80);
                } else {
                    msgPreview = out["content"].toString().left(80);
                }
                if (msgPreview.isEmpty()) msgPreview = "[Message]";
                bool isGrp = out["isGroup"].toBool();
                QString notifTitle = isGrp ? m_groupNames.value(threadId, "Zalo10") : "Zalo10";
                sendHubNotification(notifTitle, senderName + ": " + msgPreview, threadId, isGrp);
                sendBannerNotification(notifTitle, senderName + ": " + msgPreview, threadId, isGrp);
            }
            if (threadId == m_activeThreadId && !msgId.isEmpty()) {
                qint64 newNum = msgId.toLongLong();
                qint64 curNum = m_lastPollMsgId.toLongLong();
                if (newNum > curNum) m_lastPollMsgId = msgId;
            }
        }
        return;
    }

    if (cmd == 510 && subCmd == 1) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d;
        int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
        if (encType == 0) {
            d = outer.contains("data") ? outer["data"].toMap() : outer;
        } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
            QString rawB64 = outer["data"].toString();
            if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
            QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
            QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
            if (encType == 2 && !plain.isEmpty()) {
                QByteArray inflated;
                inflated.resize(plain.size() * 8 + 4096);
                z_stream zs2; memset(&zs2, 0, sizeof(zs2));
                zs2.next_in = (Bytef*)plain.constData(); zs2.avail_in = plain.size();
                bool inflateOk2 = false;
                if (inflateInit2(&zs2, 15 + 16) == Z_OK) {
                    int outPos2 = 0, r2 = Z_OK;
                    do {
                        if (outPos2 >= inflated.size()) inflated.resize(inflated.size() * 2);
                        zs2.next_out  = (Bytef*)(inflated.data() + outPos2);
                        zs2.avail_out = inflated.size() - outPos2;
                        r2 = inflate(&zs2, Z_SYNC_FLUSH);
                        outPos2 = zs2.total_out;
                    } while ((r2 == Z_OK || r2 == Z_BUF_ERROR) && zs2.avail_in > 0);
                    inflateEnd(&zs2);
                    if ((r2 == Z_STREAM_END || r2 == Z_OK) && zs2.total_out > 0) {
                        inflated.resize(zs2.total_out);
                        inflateOk2 = true;
                    }
                }
                if (inflateOk2) {
                    QVariantMap parsed = jsonToMap(inflated);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                } else {
                    qWarning() << "[WS] inflate FAILED, trying raw plain";
                    QVariantMap parsed = jsonToMap(plain);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                }
            } else if (!plain.isEmpty()) {
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
            }
        } else {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            if (dec.isEmpty() || dec.trimmed() == "{}")
                dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
            QVariantMap r = jsonToMap(dec.toUtf8());
            d = r.contains("data") ? r["data"].toMap() : r;
        }

        QVariantList rawMsgs = d["msgs"].toList();
        if (rawMsgs.isEmpty()) rawMsgs = d["ms"].toList();

        if (m_sync.awaiting) {
            bool forSync = true;
            if (!m_pending510Toid.isEmpty()) {
                for (int si = 0; si < rawMsgs.size(); ++si) {
                    QVariantMap sm = rawMsgs[si].toMap();
                    QString sFrom = sm["uidFrom"].toString();
                    QString sTo   = sm["idTo"].toString();
                    if (sFrom == "0") sFrom = m_uid;
                    if (sTo   == "0") sTo   = m_uid;
                    if (sFrom == m_pending510Toid || sTo == m_pending510Toid) { forSync = false; break; }
                }
            }
            if (forSync) {
                handleSyncPage(rawMsgs);
                return;
            }
        }

        // emitThread comes from m_pending510Toid (the thread that sent the request), not a FIFO queue,
        // to avoid mismatches when the user switches tabs quickly.
        QString emitThread = m_pending510Toid.isEmpty() ? m_activeThreadId : m_pending510Toid;
        m_pending510Toid.clear();

        if (emitThread.isEmpty()) return;

        if (rawMsgs.isEmpty() && emitThread != m_activeThreadId) {
            return;
        }

        if (!rawMsgs.isEmpty()) {
            bool anyMatch = false;
            for (int vi = 0; vi < rawMsgs.size(); ++vi) {
                QVariantMap vm = rawMsgs[vi].toMap();
                QString vFrom = vm["uidFrom"].toString();
                QString vTo   = vm["idTo"].toString();
                if (vFrom == "0") vFrom = m_uid;
                if (vTo   == "0") vTo   = m_uid;
                if (vFrom == emitThread || vTo == emitThread
                    || (vFrom == m_uid && vTo == emitThread)
                    || (vTo == m_uid && vFrom == emitThread)) {
                    anyMatch = true; break;
                }
            }
            if (!anyMatch) {
                return;
            }
        }

        processOldDmBatch(rawMsgs, emitThread, false);
    }

    // cmd=601 subCmd=0: group_event (pin/note/poll board activity from any member, plus unrelated
    // membership/settings changes). Envelope is { controls: [...] }; relevant entries have
    // content.act_type == "group", content.act names the event and content.data is per-type
    // (sometimes JSON nested in a string). Not yet confirmed against the real server.
    if (cmd == 601 && subCmd == 0) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d = decodeWsEnvelope(outer, "cmd601");

        QVariantList controls = d["controls"].toList();
        for (int i = 0; i < controls.size(); ++i) {
            QVariantMap control = controls[i].toMap();
            QVariantMap content = control["content"].toMap();
            QString actType = content["act_type"].toString();

            // Video/file upload completion arrives asynchronously here, not in the asyncfile/upload HTTP
            // response. content.data may be a map or a JSON string. fileId is matched against
            // m_pendingVideoUpload and step 2 continues in handleFileUploadDone().
            if (actType == "file_done") {
                QString fileId = content["fileId"].toString();
                QVariant fdV = content["data"];
                QVariantMap fdMap = (fdV.type() == QVariant::String)
                    ? jsonToMap(fdV.toString().toUtf8())
                    : fdV.toMap();
                QString fileUrl = fdMap["url"].toString();
                if (fileUrl.isEmpty()) fileUrl = fdMap["fileUrl"].toString();
                qDebug() << "[WS] file upload done";
                if (!fileId.isEmpty() && !fileUrl.isEmpty())
                    handleFileUploadDone(fileId, fileUrl);
                continue;
            }

            if (actType == "syncmsgmb") {
                QVariant smV = content["data"];
                QVariantMap smData = (smV.type() == QVariant::String)
                    ? jsonToMap(smV.toString().toUtf8())
                    : smV.toMap();
                handlePhoneSyncEvent(content["act"].toString(), smData);
                continue;
            }

            if (actType != "group") continue;

            QString act = content["act"].toString();
            if (act == "join_reject") continue;

            QVariant dataV = content["data"];
            QVariantMap eventData = (dataV.type() == QVariant::String)
                ? jsonToMap(dataV.toString().toUtf8())
                : dataV.toMap();

            QString groupId = eventData.contains("groupId") ? eventData["groupId"].toString()
                                                              : eventData["group_id"].toString();
            if (groupId.isEmpty()) continue;

            QString title;
            bool isSelf = false;
            bool relevant = true;
            int topicType = -1;
            QString topicId;
            QString actorNameOut;

            if (act == "new_pin_topic" || act == "unpin_topic" || act == "update_pin_topic") {
                QString actorId = eventData["actorId"].toString();
                isSelf = (actorId == m_uid);
                QVariantMap topic = eventData["topic"].toMap();
                topicType = topic["type"].toInt();
                topicId = topic["id"].toString();
                QString actorName = isSelf ? "You" : memberDisplayName(actorId);
                if (actorName.isEmpty()) actorName = "Someone";
                actorNameOut = actorName;
                QString what = (topicType == 3) ? "a poll" : (topicType == 0) ? "a note" : "a message";
                if (act == "new_pin_topic")       title = actorName + " pinned " + what;
                else if (act == "unpin_topic")    title = actorName + " unpinned " + what;
                else                              title = actorName + " updated a pinned item";
            } else if (act == "update_board" || act == "remove_board") {
                QString sourceId = eventData["sourceId"].toString();
                isSelf = (sourceId == m_uid);
                QVariant gtV = eventData["groupTopic"];
                QVariantMap groupTopic = gtV.toMap();
                topicType = groupTopic["type"].toInt();
                topicId = groupTopic["id"].toString();
                QString actorName = isSelf ? "You" : memberDisplayName(sourceId);
                if (actorName.isEmpty()) actorName = "Someone";
                actorNameOut = actorName;
                if (act == "remove_board") {
                    title = actorName + " removed a board item";
                } else if (topicType == 3) {
                    title = actorName + " created a poll";
                } else if (topicType == 0) {
                    title = actorName + " created a note";
                } else {
                    // update_board also covers poll votes (the whole topic is resent on every vote), so "created"
                    // can't be told from "voted" by topicType; keep the wording generic.
                    title = actorName + " updated the group board";
                }
            } else if (act == "update_topic" || act == "remove_topic") {
                QString actorId = eventData.contains("creatorId") ? eventData["creatorId"].toString()
                                                                    : eventData["sourceId"].toString();
                isSelf = (actorId == m_uid);
                topicId = eventData.contains("topicId") ? eventData["topicId"].toString()
                                                          : eventData["id"].toString();
                QString actorName = isSelf ? "You" : memberDisplayName(actorId);
                if (actorName.isEmpty()) actorName = "Someone";
                actorNameOut = actorName;
                title = (act == "remove_topic") ? (actorName + " removed a board item")
                                                 : (actorName + " updated the group board");
            } else {
                relevant = false;
            }

            if (relevant && !title.isEmpty()) {
                emit boardEventOccurred(groupId, act, actorNameOut, isSelf, topicType, topicId, title);

                // Self-actions already get a Hub notification from the success path of onPinGroupMessageDone()/
                // onCreateGroupNoteDone()/onCreateGroupPollDone()/onVoteGroupPollDone(); skip them here.
                // Also skip when that group's board is open (ChatView reacts to boardEventOccurred).
                if (!isSelf && groupId != m_activeThreadId && !m_mutedThreads.contains(groupId)) {
                    QString grpName = m_groupNames.value(groupId, "Zalo10");
                    sendHubNotification(grpName, title, groupId, true);
                    sendBannerNotification(grpName, title, groupId, true);
                }
            }
        }
    }

    if ((cmd == 502 || cmd == 522) && subCmd == 0) {
        bool isGroup = (cmd == 522);
        QVariantMap outer = jsonToMap(data);
        int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : -1;
        QVariant rawData = outer["data"];
        QVariantMap d;
        // zca-js decodeEventData: with encrypt=0, "data" is a JSON STRING. decodeWsEnvelope() only
        // handles objects (delivereds/seens came back empty), so parse the string here.
        if (encType == 0) {
            d = (rawData.type() == QVariant::Map) ? rawData.toMap()
                                                  : jsonToMap(rawData.toString().toUtf8());
        } else {
            d = decodeWsEnvelope(outer, "cmd502");
        }
        if (!d.contains("delivereds") && !d.contains("seens") && !d.contains("groupSeens")
            && d["data"].type() == QVariant::Map)
            d = d["data"].toMap();
        if (!d.contains("delivereds") && !d.contains("seens") && !d.contains("groupSeens")
            && rawData.type() == QVariant::String) {
            QByteArray plain = inflateAuto(QByteArray::fromBase64(rawData.toString().toUtf8()));
            if (!plain.isEmpty()) {
                QVariantMap parsed = jsonToMap(plain);
                d = parsed["data"].type() == QVariant::Map ? parsed["data"].toMap() : parsed;
            }
        }

        QVariantList delivereds = d["delivereds"].toList();
        QVariantList seens      = isGroup ? d["groupSeens"].toList() : d["seens"].toList();
        if (delivereds.isEmpty() && seens.isEmpty()) {
            QVariantMap dd = d["data"].toMap();
            delivereds = dd["delivereds"].toList();
            seens      = isGroup ? dd["groupSeens"].toList() : dd["seens"].toList();
        }

        for (int i = 0; i < delivereds.size(); ++i) {
            QVariantMap e = delivereds[i].toMap();
            QVariantList dUids = e["deliveredUids"].toList();
            QString threadId = isGroup ? e["groupId"].toString()
                                       : (dUids.isEmpty() ? QString() : dUids.first().toString());
            if (threadId.isEmpty() || threadId == m_uid) continue;
            if (!uidListHasOther(dUids, m_uid)) continue;
            if (dbAdvanceMessageStates(threadId, 2, e["msgId"].toString(), e["realMsgId"].toString()))
                emit messageStatesChanged(threadId);
        }
        for (int i = 0; i < seens.size(); ++i) {
            QVariantMap e = seens[i].toMap();
            QString threadId;
            if (isGroup) {
                threadId = e["groupId"].toString();
                if (!uidListHasOther(e["seenUids"].toList(), m_uid)) continue;
            } else {
                threadId = e["idTo"].toString();
            }
            if (threadId.isEmpty() || threadId == m_uid) continue;
            if (dbAdvanceMessageStates(threadId, 3, e["msgId"].toString(), e["realMsgId"].toString()))
                emit messageStatesChanged(threadId);
        }
        return;
    }

    // cmd=612: realtime reactions on their own channel (not "chat.reaction" inside cmd=501/521,
    // which was an unconfirmed guess). The envelope has parallel lists "reacts" (1-1) and
    // "reactGroups" (group). Each entry's "content" is a JSON string:
    // { rMsg: [{gMsgID, cMsgID, msgType}], rIcon, rType, source }.
    // uidFrom == "0" means our own reaction echoed from another logged-in device.
    if (cmd == 612) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d = decodeWsEnvelope(outer, "cmd612");

        QVariantList reacts      = d["reacts"].toList();
        QVariantList reactGroups = d["reactGroups"].toList();

        for (int pass = 0; pass < 2; ++pass) {
            const QVariantList &list = (pass == 0) ? reacts : reactGroups;
            bool isGroupPass = (pass == 1);

            for (int i = 0; i < list.size(); ++i) {
                QVariantMap entry = list[i].toMap();

                QVariant contentV = entry.value("content");
                QVariantMap content = (contentV.type() == QVariant::String)
                    ? jsonToMap(contentV.toString().toUtf8())
                    : contentV.toMap();
                if (content.isEmpty()) continue;

                QString rawUidFrom = entry.value("uidFrom").toString();
                QString rawIdTo    = entry.value("idTo").toString();
                bool isSelf = (rawUidFrom == "0");

                QString resolvedUidFrom = isSelf ? m_uid : rawUidFrom;
                QString resolvedIdTo    = (rawIdTo == "0") ? m_uid : rawIdTo;
                QString threadId = (isGroupPass || isSelf) ? resolvedIdTo : resolvedUidFrom;

                QVariantList rMsg = content.value("rMsg").toList();
                if (rMsg.isEmpty()) continue;
                QString reactMsgId = rMsg.first().toMap().value("gMsgID").toString();
                if (reactMsgId.isEmpty()) continue;

                int rType = content.value("rType").toInt();
                QString rIcon = content.value("rIcon").toString();
                QString icon = (rType < 0 || rIcon.isEmpty()) ? QString() : emojiToReactionIcon(rIcon);

                if (icon.isEmpty()) dbRemoveReaction(reactMsgId, resolvedUidFrom);
                else                dbSaveReaction(threadId, reactMsgId, resolvedUidFrom, icon);
                emit reactionUpdated(threadId, reactMsgId, resolvedUidFrom, icon);
            }
        }
    }
}

QByteArray ZaloService::maskWsFrame(int opcode, const QByteArray &data)
{
    QByteArray frame;
    frame.append((char)(0x80 | opcode));
    quint32 maskKey = qrand();
    QByteArray mask(4, 0);
    mask[0] = (maskKey >> 24) & 0xFF;
    mask[1] = (maskKey >> 16) & 0xFF;
    mask[2] = (maskKey >>  8) & 0xFF;
    mask[3] = (maskKey      ) & 0xFF;

    int len = data.size();
    if (len < 126) {
        frame.append((char)(0x80 | len));
    } else if (len < 65536) {
        frame.append((char)(0x80 | 126));
        frame.append((char)(len >> 8));
        frame.append((char)(len & 0xFF));
    } else {
        frame.append((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.append((char)((len >> (8*i)) & 0xFF));
    }
    frame += mask;
    for (int i = 0; i < len; ++i)
        frame.append(data[i] ^ mask[i % 4]);
    return frame;
}

void ZaloService::onWsDisconnected()
{
    qWarning() << "[WS] Disconnected"
              << (m_webSocket ? m_webSocket->errorString() : QString("(socket already gone)"));
    m_wsConnected = false;
    m_wsCipherKey.clear();
    if (!m_loggedIn) return;
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(wsNextReconnectDelayMs());
}

void ZaloService::onWsReconnectTimer()
{
    qDebug() << "[WS] Reconnecting...";
    connectWebSocket();
}

// Backoff: 5s, 10s, 20s, 40s, capped at 60s. m_wsConsecutiveFailCount resets on a successful
// WS upgrade (onWsReadyRead).
int ZaloService::wsNextReconnectDelayMs()
{
    int n = m_wsConsecutiveFailCount;
    if (n > 3) n = 3;
    int delay = 5000 << n;
    m_wsConsecutiveFailCount++;
    return delay;
}

void ZaloService::processOldDmBatch(const QVariantList &rawMsgs, const QString &emitThread, bool bulk)
{
        QVariantList msgs;
        qint64 maxMsgNum = -1;
        QString newestMsgId;

        for (int i = 0; i < rawMsgs.size(); ++i) {
            QVariantMap m = rawMsgs[i].toMap();
            QString msgId      = m["msgId"].toString();
            QString rawUidFrom = m["uidFrom"].toString();

            {
                QString vFrom = rawUidFrom;
                QString vTo   = m["idTo"].toString();
                if (vFrom == "0") vFrom = m_uid;
                if (vTo   == "0") vTo   = m_uid;
                bool belongsHere = (vFrom == emitThread || vTo == emitThread
                                    || (vFrom == m_uid && vTo == emitThread)
                                    || (vTo == m_uid && vFrom == emitThread));
                if (!belongsHere) {
                    continue;
                }
            }
            bool isMine = (rawUidFrom == "0" || rawUidFrom == m_uid);
            QString uidFrom = (rawUidFrom == "0") ? m_uid : rawUidFrom;

            if (isMessageDeletedForMe(msgId)) {
                continue;
            }

            if (isGroupPollNotice(m)) continue;

            // chat.undo is a recall notice, not a message: patch the original if it is earlier in the same
            // history batch, always save the recall to SQLite, and don't add it as a bubble.
            QString recalledIdH = extractRecalledMsgId(m);
            if (!recalledIdH.isEmpty()) {
                markMessageRecalled(emitThread, recalledIdH);
                for (int pj = 0; pj < msgs.size(); ++pj) {
                    QVariantMap pm = msgs[pj].toMap();
                    if (pm["msgId"].toString() == recalledIdH) {
                        pm["content"] = QString();
                        pm["msgType"] = 99;
                        msgs[pj] = pm;
                        break;
                    }
                }
                continue;
            }

            QString delMsgIdH, deleterUidH;
            if (extractDeleteInfo(m, delMsgIdH, deleterUidH)) {
                if (deleterUidH == m_uid) {
                    markMessageDeletedForMe(emitThread, delMsgIdH);
                    for (int pj = 0; pj < msgs.size(); ++pj) {
                        if (msgs[pj].toMap()["msgId"].toString() == delMsgIdH) {
                            msgs.removeAt(pj);
                            break;
                        }
                    }
                }
                continue;
            }

            QString reactMsgIdH, reactUidH, reactIconH;
            if (extractReactionInfo(m, reactMsgIdH, reactUidH, reactIconH)) {
                if (reactIconH.isEmpty()) dbRemoveReaction(reactMsgIdH, reactUidH);
                else                      dbSaveReaction(emitThread, reactMsgIdH, reactUidH, reactIconH);
                emit reactionUpdated(emitThread, reactMsgIdH, reactUidH, reactIconH);
                for (int pj = 0; pj < msgs.size(); ++pj) {
                    if (msgs[pj].toMap()["msgId"].toString() == reactMsgIdH) {
                        msgs.removeAt(pj);
                        break;
                    }
                }
                continue;
            }

            QVariantMap out;
            out["msgId"]    = msgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = false;
            out["isMine"]   = isMine;
            out["msgType"]  = m["msgType"].toInt();

            int mtH = m["msgType"].toInt();
            if (mtH == 0) {
                mtH = m["type"].toInt();
                if (mtH == 0) mtH = m["mt"].toInt();
            }
            if (mtH == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mtH = 2;
                // The history path must map share.file -> mt=3 like the realtime path; otherwise a re-polled
                // video (periodic cmd=510 poll) fell into the nested-content branch (mt=2) and permanently
                // overwrote the correct DB record.
                else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                    mtH = 3;
                else if (mtStr.contains("chat.recommended"))
                {
                    QVariantMap peekMapH = m["content"].toMap();
                    if (peekMapH.isEmpty()) {
                        QString peekStrH = m["content"].toString();
                        if (!peekStrH.isEmpty() && peekStrH.trimmed().startsWith("{"))
                            peekMapH = jsonToMap(peekStrH.toUtf8());
                    }
                    QString peekActionH = peekMapH["action"].toString();
                    mtH = peekActionH.contains("link") ? 6 : 4;
                }
                else if (mtStr.contains("chat.sticker"))
                    mtH = 5;
            }

            QString rawContentH = m["content"].toString();
            if (rawContentH.isEmpty()) {
                QVariantMap cm = m["content"].toMap();
                if (!cm.isEmpty()) {
                    rawContentH = "{";
                    QStringList cmKeys = cm.keys();
                    for (int k = 0; k < cmKeys.size(); ++k) {
                        if (k > 0) rawContentH += ",";
                        QString v = cm[cmKeys[k]].toString();
                        v.replace("\\", "\\\\").replace("\"", "\\\"");
                        rawContentH += "\"" + cmKeys[k] + "\":\"" + v + "\"";
                    }
                    rawContentH += "}";
                    if (mtH == 0) mtH = 2;
                }
            }
            if (mtH == 5) {
                QVariantMap smH = m["content"].toMap();
                if (smH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    smH = jsonToMap(rawContentH.toUtf8());
                qint64 stickerIdH = smH["id"].toString().toLongLong();
                if (stickerIdH == 0) stickerIdH = smH["id"].toLongLong();
                rawContentH = QString("{\"stickerId\":%1}").arg(stickerIdH);
                out["msgType"] = 5;
                if (stickerIdH > 0) downloadSticker(QString::number(stickerIdH));
            }

            if (mtH == 6) {
                QVariantMap lmH = m["content"].toMap();
                if (lmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    lmH = jsonToMap(rawContentH.toUtf8());
                QString lTitleH = lmH["title"].toString();
                QString lDescH  = lmH["description"].toString();
                QString lHrefH  = lmH["href"].toString();
                QString lThumbH = lmH["thumb"].toString();
                QString escH = lTitleH; escH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString descEscH = lDescH; descEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString hrefEscH = lHrefH; hrefEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                QString thumbEscH = lThumbH; thumbEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                rawContentH = QString("{\"linkTitle\":\"%1\",\"linkDesc\":\"%2\",\"linkHref\":\"%3\",\"linkThumb\":\"%4\"}")
                                  .arg(escH).arg(descEscH).arg(hrefEscH).arg(thumbEscH);
                out["msgType"] = 6;
            }

            if (mtH == 4) {
                QVariantMap rmH = m["content"].toMap();
                if (rmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    rmH = jsonToMap(rawContentH.toUtf8());
                QString actionH = rmH["action"].toString();
                QVariant paramsVH2 = rmH["params"];
                QVariantMap paramsMapH2 = (paramsVH2.type() == QVariant::String)
                    ? jsonToMap(paramsVH2.toString().toUtf8())
                    : paramsVH2.toMap();
                QString callResultH = actionH.contains("misscall") ? "missed" : "ended";
                QString callKindH   = (paramsMapH2["calltype"].toInt() == 1) ? "video" : "voice";
                qint64  durationH   = paramsMapH2["duration"].toString().toLongLong();
                rawContentH = QString("{\"callResult\":\"%1\",\"callKind\":\"%2\",\"duration\":%3}")
                                  .arg(callResultH).arg(callKindH).arg(durationH);
                out["msgType"] = 4;
            }

            if (mtH == 3) {
                QVariantMap fmH = m["content"].toMap();
                if (fmH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    fmH = jsonToMap(rawContentH.toUtf8());
                QString fTitleH = fmH["title"].toString();
                QString fHrefH  = fmH["href"].toString();
                qint64  fSizeH  = 0;
                QVariant paramsVH = fmH["params"];
                QVariantMap paramsMapH = (paramsVH.type() == QVariant::String)
                    ? jsonToMap(paramsVH.toString().toUtf8())
                    : paramsVH.toMap();
                if (!paramsMapH.isEmpty())
                    fSizeH = paramsMapH["fileSize"].toString().toLongLong();
                if (!fHrefH.isEmpty()) {
                    QString fTitleEscH = fTitleH;
                    fTitleEscH.replace("\\", "\\\\").replace("\"", "\\\"");
                    rawContentH = QString("{\"fileName\":\"%1\",\"href\":\"%2\"")
                                      .arg(fTitleEscH).arg(fHrefH);
                    if (fSizeH > 0) rawContentH += QString(",\"fileSize\":%1").arg(fSizeH);
                    rawContentH += "}";
                }
            }
            if (mtH == 2 || rawContentH.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContentH);
                if (normalized != rawContentH && !normalized.isEmpty()) {
                    rawContentH = normalized;
                    mtH = 2;
                    out["msgType"] = 2;
                }
            }

            if (!bulk && (mtH == 2 || out["msgType"].toInt() == 2) && !msgId.isEmpty()) {
                QString checkUrl;
                if (!rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContentH.toUtf8());
                    checkUrl = cm["normalUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["hdUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["thumbUrl"].toString();
                }
                if (!checkUrl.isEmpty() && !checkUrl.startsWith("http")) {
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        downloadImageMessage(msgId, checkUrl, emitThread);
                        fetchPhotoViaWs510(msgId, emitThread);
                        fetchPhotoViaHttp(msgId, emitThread);
                    }
                }
            }
            // Same as the realtime path: keep out["msgType"] in sync with the resolved mtH.
            out["msgType"] = mtH;
            out["content"] = rawContentH;
            msgs.append(out);

            if (!msgId.isEmpty()) {
                qint64 num = msgId.toLongLong();
                if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
                if (isMine) m_seenMsgIds.insert(msgId);
            }
        }

        // Bulk sync walks the whole history, so only ever move the incremental-poll cursor forward.
        if (!newestMsgId.isEmpty()
            && (!bulk || newestMsgId.toLongLong() > m_lastPollMsgId.toLongLong()))
            m_lastPollMsgId = newestMsgId;

        QList<QVariantMap> photoUpdates;
        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap mm = msgs[i].toMap();
            QString mid = mm["msgId"].toString();
            if (m_pendingPhotoMsgIds.contains(mid)) {
                QString threadForPhoto = m_pendingPhotoMsgIds.take(mid);
                QString content = mm["content"].toString();
                QString photoUrl;
                if (!content.isEmpty() && content.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(content.toUtf8());
                    photoUrl = cm["normalUrl"].toString();
                    if (photoUrl.isEmpty()) photoUrl = cm["hdUrl"].toString();
                    if (photoUrl.isEmpty()) photoUrl = cm["thumbUrl"].toString();
                }
                if (!photoUrl.isEmpty() && photoUrl.startsWith("http")) {
                    if (m_db) {
                        const char *sql = "UPDATE messages SET content=?, msgType=2 WHERE msgId=?";
                        sqlite3_stmt *stmt = 0;
                        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) == SQLITE_OK) {
                            sqlite3_bind_text(stmt, 1, content.toUtf8().constData(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_text(stmt, 2, mid.toUtf8().constData(),     -1, SQLITE_TRANSIENT);
                            sqlite3_step(stmt);
                            sqlite3_finalize(stmt);
                        }
                    }
                    downloadImageMessage(mid, photoUrl, threadForPhoto);
                } else {
                    qWarning() << "[Msg] photo fetch: no HTTP URL, msgId=" << mid;
                }
            }
        }

        for (int i = 0; i < msgs.size(); ++i)
            dbSaveMessage(msgs[i].toMap(), emitThread);
        if (!newestMsgId.isEmpty() && !bulk)
            m_threadLastMsgId[emitThread] = newestMsgId;

        if (!bulk || emitThread == m_activeThreadId)
            emit messagesReady(emitThread, msgs);
}
