#include "core/websocket/WebSocketCommon.hpp"

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
