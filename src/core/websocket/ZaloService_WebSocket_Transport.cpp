#include "core/websocket/WebSocketCommon.hpp"

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
