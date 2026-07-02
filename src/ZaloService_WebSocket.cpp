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

// Hand-rolled WebSocket client (RFC 6455) over QSslSocket.
// BB10 NDK 10.3 ships Qt4, which has no QtWebSockets, so the TLS handshake,
// HTTP Upgrade, and binary frame (de)masking are implemented manually here.
// Real-time chat/group events arrive over this connection once it's up.

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
    payload.append((char)1);                   // version
    payload.append((char)(cmd & 0xFF));        // cmd lo
    payload.append((char)((cmd >> 8) & 0xFF)); // cmd hi
    payload.append((char)(subCmd & 0xFF));     // subCmd
    payload.append(json.toUtf8());
    m_webSocket->write(maskWsFrame(0x2, payload));
}

void ZaloService::sendWsPing()
{
    if (!m_webSocket || !m_wsConnected) return;
    // Ping: cmd=2 subCmd=1 data={"eventId": <timestamp>}
    QString json = QString("{\"eventId\":%1}").arg(QDateTime::currentMSecsSinceEpoch());
    QByteArray payload;
    payload.append((char)1);    // version
    payload.append((char)2);    // cmd=2 lo
    payload.append((char)0);    // cmd=2 hi
    payload.append((char)1);    // subCmd=1
    payload.append(json.toUtf8());
    m_webSocket->write(maskWsFrame(0x2, payload));
}

void ZaloService::connectWebSocket()
{
    if (m_zpwWsUrls.isEmpty()) {
        qDebug() << "[Zalo WS] No zpw_ws URLs, skip";
        return;
    }
    disconnectWebSocket();

    m_wsUrlIndex = 0;
    m_wsUrls = m_zpwWsUrls;

    QUrl url(m_wsUrls[m_wsUrlIndex]);
    // Thêm query params như zca-js
    url.addQueryItem("t",        QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.addQueryItem("zpw_ver",  QString::number(API_VERSION));
    url.addQueryItem("zpw_type", QString::number(API_TYPE));

    qDebug() << "[Zalo WS] Connecting to:" << url.toString().left(80);

    m_webSocket = new QSslSocket(this);
    m_wsBuffer.clear();
    m_wsHandshakeSent = false;
    m_wsConnected     = false;
    m_wsCipherKey.clear();

    connect(m_webSocket, SIGNAL(connected()),          this, SLOT(onWsConnected()));
    connect(m_webSocket, SIGNAL(encrypted()),          this, SLOT(onWsEncrypted()));
    connect(m_webSocket, SIGNAL(readyRead()),          this, SLOT(onWsReadyRead()));
    connect(m_webSocket, SIGNAL(disconnected()),       this, SLOT(onWsDisconnected()));
    connect(m_webSocket, SIGNAL(sslErrors(QList<QSslError>)),
            this, SLOT(onWsSslErrors(QList<QSslError>)));

    bool useSsl = (url.scheme() == "wss" || url.scheme() == "https");
    int  port   = url.port(useSsl ? 443 : 80);

    if (useSsl)
        m_webSocket->connectToHostEncrypted(url.host(), port);
    else
        m_webSocket->connectToHost(url.host(), port);

    m_webSocket->setProperty("wsUrl", url.toString());
}

// Đóng WS "sạch" (gửi đúng Close frame chuẩn, opcode 0x8) trước khi process bị
// kill — thay cho việc abort() trần trụi trong disconnectWebSocket() bên dưới.
//
// LÝ DO THÊM HÀM NÀY: log thực tế cho thấy cookie zpw_sek chết RẤT nhanh sau khi
// app bị đóng (vuốt card) — chỉ ~19s sau lần keepAlive vừa báo "OK". Một khả
// năng hợp lý: server phân biệt "client tự đóng kết nối đúng chuẩn WS" (Close
// frame) với "client rớt mạng/bị kill bất ngờ" (TCP reset/abort), và có thể xử
// lý zpw_sek khác nhau giữa 2 trường hợp (vd: coi rớt-bất-ngờ là dấu hiệu cần
// revoke session để an toàn). Đây là giả thuyết CHƯA được xác nhận chắc chắn —
// cần test thực tế mới biết có thật sự giúp ích hay không — nhưng chi phí thử
// rất thấp và đúng chuẩn giao thức WS, không có gì để mất.
//
// Được gọi từ ApplicationUI::onManualExit() ngay trước khi quit() — tại đó
// process sắp bị OS thu hồi nên không còn event loop để chờ async, do đó dùng
// waitForBytesWritten() (blocking) để đảm bảo frame thực sự ra khỏi tiến trình
// trước khi bị kill, thay vì gọi flush() rồi hy vọng kernel gửi kịp.
void ZaloService::closeWebSocketGracefully()
{
    if (!m_webSocket || !m_wsConnected) {
        qDebug() << "[Zalo WS] closeWebSocketGracefully: no active WS, skip";
        return;
    }

    qDebug() << "[Zalo WS] Sending graceful Close frame before exit";
    m_webSocket->write(maskWsFrame(0x8, QByteArray()));
    m_webSocket->flush();
    m_webSocket->waitForBytesWritten(300); // blocking — không còn event loop để chờ async
}

void ZaloService::disconnectWebSocket()
{
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

void ZaloService::onWsConnected()
{
    // Plain TCP connected (non-SSL) — gửi HTTP Upgrade ngay
    if (!m_webSocket) return;
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::onWsEncrypted()
{
    // TLS handshake xong — gửi HTTP Upgrade
    if (!m_webSocket) return;
    QString urlStr = m_webSocket->property("wsUrl").toString();
    sendWsHandshake(QUrl(urlStr));
}

void ZaloService::sendWsHandshake(const QUrl &url)
{
    if (m_wsHandshakeSent) return;
    m_wsHandshakeSent = true;

    // Tạo Sec-WebSocket-Key ngẫu nhiên (16 bytes base64)
    QByteArray keyBytes;
    for (int i = 0; i < 16; ++i)
        keyBytes.append((char)(qrand() % 256));
    QString wsKey = keyBytes.toBase64();

    // Expected Accept = base64(SHA1(key + "258EAFA5-..."))
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

    m_webSocket->write(handshake.toUtf8());
    qDebug() << "[Zalo WS] HTTP Upgrade sent to" << url.host() << path.left(60);
}

void ZaloService::onWsSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
    m_webSocket->ignoreSslErrors();
}

void ZaloService::onWsReadyRead()
{
    if (!m_webSocket) return;
    m_wsBuffer += m_webSocket->readAll();

    if (!m_wsConnected) {
        // Đang chờ HTTP 101 Switching Protocols
        int headerEnd = 0;
        if (parseWsHandshakeResponse(m_wsBuffer, headerEnd)) {
            qDebug() << "[Zalo WS] Upgraded OK, WebSocket connected";
            m_wsConnected = true;
            m_wsBuffer    = m_wsBuffer.mid(headerEnd);
            // Server sẽ tự gửi cmd=1 handshake ngay sau khi connect
        } else if (m_wsBuffer.contains("\r\n\r\n")) {
            // HTTP response nhưng không phải 101
            qDebug() << "[Zalo WS] Upgrade failed:" << m_wsBuffer.left(200);
            disconnectWebSocket();
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(5000);
            return;
        } else {
            return; // Chưa nhận đủ header
        }
    }

    // Parse WebSocket frames từ m_wsBuffer
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
        if ((int)(headerSize + payLen) > m_wsBuffer.size()) break; // Chưa đủ

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
    if (opcode == 0x8) { // Close
        qDebug() << "[Zalo WS] Server sent Close frame";
        onWsDisconnected();
        return;
    }
    if (opcode == 0x9) { // Ping → Pong
        QByteArray pong = maskWsFrame(0xA, payload);
        m_webSocket->write(pong);
        return;
    }
    if (opcode != 0x2 && opcode != 0x1) return; // Chỉ xử lý binary/text

    handleWsMessage(opcode, payload);
}

void ZaloService::handleWsMessage(int /*opcode*/, const QByteArray &payload)
{
    // Zalo WS header: version(1B) + cmd(2B LE) + subCmd(1B) = 4 bytes
    // Source: zca-js listen.js buffer[0]=version, readUInt16LE(1)=cmd, buffer[3]=subCmd
    if (payload.size() < 4) return;

    quint8  version = (quint8)payload[0];
    qint32  cmd     = (qint32)((quint8)payload[1] | ((quint8)payload[2] << 8));
    qint32  subCmd  = (qint32)((quint8)payload[3]);
    QByteArray data = payload.mid(4);
    Q_UNUSED(version);

    if (cmd == 1 && subCmd == 1) {
        QVariantMap parsed = jsonToMap(data);
        QString keyB64 = parsed["key"].toString();
        m_wsCipherKey = QByteArray::fromBase64(keyB64.toUtf8());
        qDebug() << "[Zalo WS] Handshake OK, cipherKey len:" << m_wsCipherKey.size();

        // Gửi PING ngay (cmd=2 subCmd=1) theo zca-js
        sendWsPing();
        // Bắt đầu ping timer 25s
        if (m_listenTimer) m_listenTimer->start(25000);
        if (!m_pending510Toid.isEmpty()) {
            QString req510 = QString("{\"first\":true,\"lastId\":null,\"toid\":\"%1\",\"preIds\":[]}")
                             .arg(m_pending510Toid);
            sendWsRequest(510, 1, req510);
            qDebug() << "[Zalo WS] WS ready, auto-fetch DM toid=" << m_pending510Toid;
        }
        return;
    }

    // cmd=501 (DM mới) / cmd=521 (group mới): new real-time message
    if ((cmd == 501 || cmd == 521) && subCmd == 0) {
        bool isGroup = (cmd == 521);

        // WS event: JSON { data: "<base64>", encrypt: 0|1|2|3 }
        // encrypt=0: plaintext JSON, encrypt=2/3: AES-GCM
        QVariantMap outer = jsonToMap(data);
        QVariantMap d;
        int encType = outer.contains("encrypt") ? outer["encrypt"].toInt() : 1;
        if (encType == 0) {
            // Không mã hoá
            d = outer.contains("data") && outer["data"].type() == QVariant::Map
                ? outer["data"].toMap() : outer;
        } else if ((encType == 2 || encType == 3) && !m_wsCipherKey.isEmpty()) {
            QString rawB64 = outer["data"].toString();
            if (encType == 2) rawB64 = QUrl::fromPercentEncoding(rawB64.toUtf8());
            QByteArray cipherBytes = QByteArray::fromBase64(rawB64.toUtf8());
            qDebug() << "[Zalo WS] GCM decrypt cmd501: keyLen=" << m_wsCipherKey.size()
                     << "cipherLen=" << cipherBytes.size()
                     << "keyHex8=" << m_wsCipherKey.left(8).toHex()
                     << "ivHex8=" << cipherBytes.left(8).toHex();
            QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
            qDebug() << "[Zalo WS] GCM result501: plainLen=" << plain.size();
            if (encType == 2 && !plain.isEmpty()) {
                // Format: gzip (1f 8b ...) — windowBits = 15+16
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
                    qDebug() << "[Zalo WS] inflated (first500):" << QString::fromUtf8(inflated.left(500));
                    QVariantMap parsed = jsonToMap(inflated);
                    // zca-js: inflate result is direct JSON, no "data" wrapper
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed; // direct struct: { ms:[], msgs:[], ... }
                } else {
                    qDebug() << "[Zalo WS] inflate FAILED, trying raw plain";
                    qDebug() << "[Zalo WS] plain (first150):" << QString::fromUtf8(plain.left(150));
                    QVariantMap parsed = jsonToMap(plain);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                }
            } else if (!plain.isEmpty()) {
                qDebug() << "[Zalo WS] encType=3 plain (first150):" << QString::fromUtf8(plain.left(150));
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qDebug() << "[Zalo WS] decrypt returned empty for encType=" << encType;
            }
        } else {
            QString dec = aesDecryptBase64(m_secretKey, outer["data"].toString());
            if (dec.isEmpty() || dec.trimmed() == "{}")
                dec = aesDecryptBase64(QString::fromUtf8(m_wsCipherKey.toBase64()), outer["data"].toString());
            QVariantMap r = jsonToMap(dec.toUtf8());
            d = r.contains("data") ? r["data"].toMap() : r;
        }

        // zca-js real-time: field "ms" (not "msgs") for cmd=501/521
        QVariantList msgs;
        if (isGroup) {
            // zca-js cmd=521: parsedData.groupMsgs
            msgs = d["groupMsgs"].toList();
            if (msgs.isEmpty()) msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
        } else {
            // zca-js cmd=501: parsedData.msgs
            msgs = d["msgs"].toList();
            if (msgs.isEmpty()) msgs = d["ms"].toList();
            if (msgs.isEmpty()) msgs = d["groupMsgs"].toList(); // fallback
        }
        // Zalo sometimes wraps in d["data"]
        if (msgs.isEmpty()) {
            QVariantMap dd = d["data"].toMap();
            msgs = isGroup ? dd["groupMsgs"].toList() : dd["msgs"].toList();
            if (msgs.isEmpty()) msgs = isGroup ? dd["msgs"].toList() : dd["ms"].toList();
            if (msgs.isEmpty()) msgs = dd["ms"].toList();
        }
        qDebug() << "[Zalo WS] cmd=501/521 encType=" << (outer.contains("encrypt") ? outer["encrypt"].toInt() : 1)
                 << "d.keys=" << d.keys() << "msgs.size=" << msgs.size() << "isGroup=" << isGroup;

        for (int i = 0; i < msgs.size(); ++i) {
            QVariantMap m = msgs[i].toMap();

            QString rawUidFrom = m["uidFrom"].toString();
            QString rawIdTo    = m["idTo"].toString();
            bool isSelf = (rawUidFrom == "0"); // "0" = tin của mình

            // Resolve "0" → m_uid
            QString uidFrom = isSelf      ? m_uid : rawUidFrom;
            QString idTo    = (rawIdTo == "0") ? m_uid : rawIdTo;

            QString threadId;
            if (isGroup) {
                threadId = idTo;
            } else {
                // zca-js Message.js: threadId = uidFrom=="0" ? idTo : uidFrom
                threadId = isSelf ? idTo : uidFrom;
            }
            if (threadId.isEmpty() || threadId == m_uid) continue;

            QString msgId = m["msgId"].toString();
            if (!msgId.isEmpty()) {
                if (m_seenMsgIds.contains(msgId)) continue;
                m_seenMsgIds.insert(msgId);
            }
            QString cliMsgId = m["cliMsgId"].toString();

            // chat.undo = recall/unsend notification, not a real message — update the
            // original message in place instead of appending a stray JSON bubble.
            QString recalledId = extractRecalledMsgId(m);
            if (!recalledId.isEmpty()) {
                markMessageRecalled(threadId, recalledId);
                continue;
            }

            QVariantMap out;
            out["msgId"]    = msgId;
            out["senderId"] = uidFrom;
            out["dName"]    = m["dName"].toString();
            out["ts"]       = m["ts"].toString();
            out["isGroup"]  = isGroup;
            out["isMine"]   = isSelf;
            out["msgType"]  = m["msgType"].toInt();

            int mt = m["msgType"].toInt();
            // Also try alternate field names Zalo WS uses
            if (mt == 0) {
                mt = m["type"].toInt();
                if (mt == 0) mt = m["mt"].toInt();
                if (mt == 0) mt = m["msgtype"].toInt();
            }
            // Zalo's WS also sends msgType as a string ("chat.photo") rather than
            // an int, so fall back to matching it against known photo type names.
            if (mt == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mt = 2;
            }

            // Qt4's QScriptEngine converts nested JSON objects to QVariantMap, so
            // content.toString() comes back empty for photo/object payloads — re-serialize
            // it to a JSON string here so the rest of the pipeline can parse it as usual.
            QString rawContent = m["content"].toString();
            if (rawContent.isEmpty()) {
                QVariantMap cm = m["content"].toMap();
                if (!cm.isEmpty()) {
                    // Build JSON string from the content map
                    rawContent = "{";
                    QStringList cmKeys = cm.keys();
                    for (int k = 0; k < cmKeys.size(); ++k) {
                        if (k > 0) rawContent += ",";
                        QString v = cm[cmKeys[k]].toString();
                        // escape backslash and double-quote
                        v.replace("\\", "\\\\").replace("\"", "\\\"");
                        rawContent += "\"" + cmKeys[k] + "\":\"" + v + "\"";
                    }
                    rawContent += "}";
                    qDebug() << "[Zalo WS] content was nested object, serialized:" << rawContent.left(200);
                }
            }

            // No content found yet — fall back to paramsExt/previewThumb, where Zalo's
            // real-time WS push sometimes carries the photo data instead.
            if (rawContent.isEmpty()) {
                QString paramsExtStr = m["paramsExt"].toString();
                QString previewThumb = m["previewThumb"].toString();
                qDebug() << "[Zalo WS] msg keys=" << m.keys()
                         << "msgType=" << m["msgType"].toInt()
                         << "type=" << m["type"].toInt()
                         << "mt=" << m["mt"].toInt()
                         << "paramsExt(100)=" << paramsExtStr.left(100)
                         << "previewThumb(60)=" << previewThumb.left(60);
            }

            // Zalo WS real-time photo: msgType may be 0 but photo URLs are in paramsExt/previewThumb
            if (mt == 2 || rawContent.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContent);
                if (normalized != rawContent && !normalized.isEmpty()) {
                    rawContent = normalized;
                    mt = 2;
                    out["msgType"] = 2;
                    qDebug() << "[Zalo WS] photo detected via paramsExt/previewThumb: content=" << rawContent.left(80);
                    // Log the full payload (truncated) since content/attach can run past 500 chars
                    // and the photo-URL fields can land in any of these three places.
                    qDebug() << "[Zalo WS] PHOTO content=" << m["content"].toString().left(500);
                    qDebug() << "[Zalo WS] PHOTO attach="  << m["attach"].toString().left(500);
                    qDebug() << "[Zalo WS] PHOTO params="  << m["params"].toString().left(500);
                }
            }
            if (mt == 2) {
                // Normalize photo content to {"normalUrl":"...","thumbUrl":"...","hdUrl":"..."}
                // WS may deliver via content JSON (href/thumb), top-level, or in "attach" sub-object
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
                // Check top-level fields
                if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumb"].toString();
                if (fSizeStr.isEmpty()) fSizeStr = m["hdSize"].toString();
                // Check "attach" sub-object (Zalo WS real-time delivery)
                if (nUrl.isEmpty()) {
                    QVariantMap att = m["attach"].toMap();
                    if (att.isEmpty()) {
                        // attach may be a JSON string
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
                // Check "params" sub-object
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
                    // Extract caption from normalizePhotoContent result (it was called above
                    // and may have put a "caption" key in rawContent) before we overwrite it.
                    QString caption;
                    if (!rawContent.isEmpty() && rawContent.contains("\"caption\":\"")) {
                        QVariantMap prevCm = jsonToMap(rawContent.toUtf8());
                        caption = prevCm["caption"].toString();
                    }
                    // Also try extracting directly from message map (title field)
                    if (caption.isEmpty()) {
                        QVariantMap cm = jsonToMap(m["content"].toString().toUtf8());
                        if (!cm.isEmpty()) {
                            caption = cm["title"].toString();
                            if (caption.isEmpty()) caption = cm["description"].toString();
                        }
                    }
                    if (caption.isEmpty()) caption = m["title"].toString();

                    if (!caption.isEmpty()) {
                        // Escape caption for JSON embedding
                        caption.replace("\\", "\\\\").replace("\"", "\\\"")
                               .replace("\n", "\\n").replace("\r", "\\r")
                               .replace("\t", "\\t");
                    }

                    // If this is the WS echo of a photo we JUST sent ourselves, we already
                    // know the exact original fileSize/fileName from sendPhoto() — prefer
                    // that over whatever (possibly absent) hdSize the server echoed back.
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

                    // Self-echo of our own just-sent photo: reuse the already-cached local
                    // file instead of re-downloading from the CDN. Fetching the CDN copy
                    // moments after upload is a race — the file can still 404/return empty
                    // server-side, which previously left "my" sent photo as a permanent gray
                    // box once the in-memory placeholder was replaced by this DB row. The
                    // local file was copied into the persistent "/tmp" cache at pick-time
                    // (see cacheLocalImage()) and is never deleted except by clearCache().
                    if (haveSentInfo) {
                        QString localP = sentInfo.value("localPath").toString();
                        QString fsPath = localP.startsWith("file://") ? localP.mid(7) : localP;
                        if (!fsPath.isEmpty() && QFile::exists(fsPath)) {
                            QSize dim = imageDimensions(localP);
                            out["localImage"] = localP;
                            out["imgWidth"]   = dim.width();
                            out["imgHeight"]  = dim.height();
                            m_avatarCache[nUrl] = localP;
                            qDebug() << "[Zalo WS] self photo echo: reusing cached local file"
                                      << localP << "for msgId" << msgId;
                        }
                        m_pendingSentPhotoInfo.remove(cliMsgId);
                    }
                }
                // Log the full key set when no URL was found, to help diagnose
                // payload shapes the parsing above doesn't handle yet.
                if (nUrl.isEmpty()) {
                    qDebug() << "[Zalo WS] photo msgType=2 but no URL found. m.keys=" << m.keys()
                             << "content=" << rawContent.left(100);
                }
                // If nUrl is NOT a real HTTP URL (it's a Zalo protobuf thumbnail):
                // 1. Decode thumbnail immediately for fast preview.
                // 2. Fetch full-res via WS cmd=510 history to get real HTTP URL.
                if (!nUrl.isEmpty() && !nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    qDebug() << "[Zalo WS] photo has protobuf thumb (not HTTP URL), decoding thumbnail msgId=" << msgId;
                    downloadImageMessage(msgId, nUrl, threadId);
                    // Fetch full-res via WS cmd=510 AND HTTP API in parallel
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        fetchPhotoViaWs510(msgId, threadId);
                        fetchPhotoViaHttp(msgId, threadId);
                    }
                } else if (!nUrl.isEmpty() && nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    // Regular HTTP photo URL: previously this was only fetched lazily
                    // by ChatView when the user actually opened the thread and the
                    // bubble was rendered on screen. That meant a photo recalled
                    // before the user ever viewed the chat had no cached local file
                    // to fall back on, so "Show Recalled Messages" could only show
                    // the placeholder, never the actual image. Cache it eagerly here
                    // instead, as soon as the message arrives over WS, regardless of
                    // which thread is currently open. downloadImageMessage() is
                    // idempotent (checks m_avatarCache/m_pendingAvatars first), so
                    // this is a no-op if ChatView already requested/has it.
                    downloadImageMessage(msgId, nUrl, threadId);
                }
            }
            out["content"] = rawContent;

            qDebug() << "[Zalo WS] new msg from" << uidFrom
                     << "thread" << threadId << "content=" << out["content"].toString().left(60);
            dbSaveMessage(out, threadId);
            emit newMessage(threadId, out);
            if (!isSelf && threadId != m_activeThreadId && !m_mutedThreads.contains(threadId)) {
                QString senderName = out["dName"].toString();
                if (senderName.isEmpty()) senderName = "Unknown";
                int mt = out["msgType"].toInt();
                QString msgPreview = (mt == 2) ? "[Photo]" : out["content"].toString().left(80);
                if (msgPreview.isEmpty()) msgPreview = "[Message]";
                bool isGrp = out["isGroup"].toBool();
                QString notifTitle = isGrp ? m_groupNames.value(threadId, "Zalo10") : "Zalo10";
                sendHubNotification(notifTitle, senderName + ": " + msgPreview, threadId, isGrp);
            }
            if (threadId == m_activeThreadId && !msgId.isEmpty()) {
                qint64 newNum = msgId.toLongLong();
                qint64 curNum = m_lastPollMsgId.toLongLong();
                if (newNum > curNum) m_lastPollMsgId = msgId;
            }
        }
        return;
    }

    // cmd=510 subCmd=1: server trả lời old messages DM (response của requestOldMessages)
    // (zca-js: emit "old_messages", responseMsgs, ThreadType.User)
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
                // Format: gzip (1f 8b ...) — windowBits = 15+16
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
                    qDebug() << "[Zalo WS] inflated (first500):" << QString::fromUtf8(inflated.left(500));
                    QVariantMap parsed = jsonToMap(inflated);
                    // zca-js: inflate result is direct JSON, no "data" wrapper
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed; // direct struct: { ms:[], msgs:[], ... }
                } else {
                    qDebug() << "[Zalo WS] inflate FAILED, trying raw plain";
                    qDebug() << "[Zalo WS] plain (first150):" << QString::fromUtf8(plain.left(150));
                    QVariantMap parsed = jsonToMap(plain);
                    if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                        d = parsed["data"].toMap();
                    else
                        d = parsed;
                }
            } else if (!plain.isEmpty()) {
                qDebug() << "[Zalo WS] encType=3 plain (first150):" << QString::fromUtf8(plain.left(150));
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            } else {
                qDebug() << "[Zalo WS] decrypt returned empty for encType=" << encType;
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
        qDebug() << "[Zalo WS] old_messages DM count:" << rawMsgs.size()
                 << "d.keys=" << d.keys() << "activeThread:" << m_activeThreadId;

        // ── Xác định emitThread ────────────────────────────────────────────
        // Dùng m_pending510Toid (thread đã gửi request) thay vì queue FIFO
        // để tránh lệch pha khi user switch tab nhanh
        QString emitThread = m_pending510Toid.isEmpty() ? m_activeThreadId : m_pending510Toid;
        m_pending510Toid.clear(); // reset sau mỗi response
        qDebug() << "[Zalo WS] old_messages: emitThread=" << emitThread
                 << "msgs=" << rawMsgs.size();

        if (emitThread.isEmpty()) return;

        // An empty result for a thread that's no longer active just means the response
        // arrived late for a request the user has already navigated away from — discard it
        // rather than clearing the model for whichever thread is now showing.
        if (rawMsgs.isEmpty() && emitThread != m_activeThreadId) {
            qDebug() << "[Zalo WS] cmd=510 empty stale response, discarding (emitThread="
                     << emitThread << "activeThread=" << m_activeThreadId << ")";
            return;
        }

        // Guard against stale responses from an earlier request: confirm at least one
        // message in the batch actually belongs to emitThread before trusting it.
        if (!rawMsgs.isEmpty()) {
            bool anyMatch = false;
            for (int vi = 0; vi < rawMsgs.size(); ++vi) {
                QVariantMap vm = rawMsgs[vi].toMap();
                // Resolve "0" → m_uid (server uses "0" for self in DM responses)
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
                qDebug() << "[Zalo WS] cmd=510 stale response, discarding (emitThread=" << emitThread << ")";
                return;
            }
        }

        QVariantList msgs;
        qint64 maxMsgNum = -1;
        QString newestMsgId;

        for (int i = 0; i < rawMsgs.size(); ++i) {
            QVariantMap m = rawMsgs[i].toMap();
            QString msgId      = m["msgId"].toString();
            QString rawUidFrom = m["uidFrom"].toString();

            // cmd=510 uses a single global lastId, so one response can carry messages
            // from several threads at once — keep only the ones for emitThread.
            {
                QString vFrom = rawUidFrom;
                QString vTo   = m["idTo"].toString();
                if (vFrom == "0") vFrom = m_uid;
                if (vTo   == "0") vTo   = m_uid;
                bool belongsHere = (vFrom == emitThread || vTo == emitThread
                                    || (vFrom == m_uid && vTo == emitThread)
                                    || (vTo == m_uid && vFrom == emitThread));
                if (!belongsHere) {
                    qDebug() << "[Zalo WS] old_messages: skip msg" << msgId
                             << "belongs to other thread (from=" << vFrom << "to=" << vTo << ")";
                    continue;
                }
            }
            bool isMine = (rawUidFrom == "0" || rawUidFrom == m_uid);
            QString uidFrom = (rawUidFrom == "0") ? m_uid : rawUidFrom;

            // chat.undo = recall/unsend notification, not a real message. Patch the
            // original message if it's earlier in this same history batch, persist the
            // recall to SQLite either way, and skip adding this event as its own bubble.
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
            // Same string-typed msgType case as above ("chat.photo" instead of an int).
            if (mtH == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mtH = 2;
            }

            // Re-serialize nested content objects back to JSON, same reasoning as above.
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
                    qDebug() << "[Zalo WS] old_messages: content was nested obj, serialized:"
                             << rawContentH.left(200);
                    if (mtH == 0) mtH = 2; // nested content obj → photo message
                }
            }
            // Normalize photo: also handles paramsExt/previewThumb (msgType may be 0)
            if (mtH == 2 || rawContentH.isEmpty()) {
                QString normalized = normalizePhotoContent(m, rawContentH);
                if (normalized != rawContentH && !normalized.isEmpty()) {
                    rawContentH = normalized;
                    mtH = 2;
                    out["msgType"] = 2;
                }
            }

            // Check if photo has no real HTTP URL — fetch full-res via WS cmd=510
            if ((mtH == 2 || out["msgType"].toInt() == 2) && !msgId.isEmpty()) {
                QString checkUrl;
                if (!rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{")) {
                    QVariantMap cm = jsonToMap(rawContentH.toUtf8());
                    checkUrl = cm["normalUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["hdUrl"].toString();
                    if (checkUrl.isEmpty()) checkUrl = cm["thumbUrl"].toString();
                }
                if (!checkUrl.isEmpty() && !checkUrl.startsWith("http")) {
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        qDebug() << "[Zalo WS] old_messages photo protobuf thumb, decoding msgId=" << msgId;
                        // Decode thumbnail for immediate preview
                        downloadImageMessage(msgId, checkUrl, emitThread);
                        // Fetch full-res via WS cmd=510 history
                        fetchPhotoViaWs510(msgId, emitThread);
                        fetchPhotoViaHttp(msgId, emitThread);
                    }
                }
            }
            out["content"] = rawContentH;
            msgs.append(out);

            if (!msgId.isEmpty()) {
                qint64 num = msgId.toLongLong();
                if (num > maxMsgNum) { maxMsgNum = num; newestMsgId = msgId; }
                if (isMine) m_seenMsgIds.insert(msgId);
            }
        }

        if (!newestMsgId.isEmpty()) m_lastPollMsgId = newestMsgId;

        // Check if any returned messages are pending photo fetches
        // If ALL messages in this batch are photo-pending, this was a fetchPhotoViaWs510 call —
        // extract real URLs and emit imageMsgReady instead of messagesReady.
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
                    qDebug() << "[Zalo WS] photo fetch resolved msgId=" << mid << "url=" << photoUrl.left(80);
                    // Update DB with real URLs
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
                    qDebug() << "[Zalo WS] photo fetch: no HTTP URL for msgId=" << mid << "content=" << content.left(80);
                    // FULL RAW DUMP: print every field of the message map so we can
                    // identify which field carries the real CDN URL in a future build.
                    qDebug() << "[Zalo WS] photo msg KEYS=" << mm.keys();
                    QStringList dumpKeys = mm.keys();
                    for (int dk = 0; dk < dumpKeys.size(); ++dk) {
                        QString val = mm[dumpKeys[dk]].toString();
                        if (!val.isEmpty())
                            qDebug() << "[Zalo WS] photo msg" << dumpKeys[dk] << "=" << val.left(120);
                    }
                }
            }
        }

        for (int i = 0; i < msgs.size(); ++i)
            dbSaveMessage(msgs[i].toMap(), emitThread);
        if (!newestMsgId.isEmpty())
            m_threadLastMsgId[emitThread] = newestMsgId;

        emit messagesReady(emitThread, msgs);
    }
}

QByteArray ZaloService::maskWsFrame(int opcode, const QByteArray &data)
{
    QByteArray frame;
    frame.append((char)(0x80 | opcode)); // FIN + opcode
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
    qDebug() << "[Zalo WS] Disconnected";
    m_wsConnected = false;
    m_wsCipherKey.clear();
    if (!m_loggedIn) return;
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(5000);
}

void ZaloService::onWsReconnectTimer()
{
    qDebug() << "[Zalo WS] Reconnecting...";
    connectWebSocket();
}

