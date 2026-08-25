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
    wsWriteRaw(maskWsFrame(0x2, payload));
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
    wsWriteRaw(maskWsFrame(0x2, payload));
}

void ZaloService::connectWebSocket()
{
    if (m_zpwWsUrls.isEmpty()) {
        qDebug() << "[Zalo WS] No zpw_ws URLs, skip";
        return;
    }
    disconnectWebSocket();

    // m_wsUrls chỉ cần (re)seed từ m_zpwWsUrls 1 lần — các lần reconnect
    // sau khi bị flag lỗi sẽ tăng m_wsUrlIndex trong CÙNG list m_wsUrls
    // thay vì ghi đè và reset về index 0 (đó là lý do trước đây mọi lần
    // reconnect cứ thử lại đúng host đã hỏng mãi mãi).
    if (m_wsUrls.isEmpty()) {
        m_wsUrls     = m_zpwWsUrls;
        m_wsUrlIndex = 0;
    } else if (m_wsAdvanceUrlOnReconnect) {
        m_wsUrlIndex = (m_wsUrlIndex + 1) % m_wsUrls.size();
        qDebug() << "[Zalo WS] Previous host failed at the socket/SSL level, "
                     "advancing to pool host index" << m_wsUrlIndex;
    }
    m_wsAdvanceUrlOnReconnect = false;

    QUrl url(m_wsUrls[m_wsUrlIndex]);
    // Thêm query params như zca-js
    url.addQueryItem("t",        QString::number(QDateTime::currentMSecsSinceEpoch()));
    url.addQueryItem("zpw_ver",  QString::number(API_VERSION));
    url.addQueryItem("zpw_type", QString::number(API_TYPE));

    qDebug() << "[Zalo WS] Connecting to:" << url.toString().left(80);

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

    // QSslSocket của BlackBerry (bản biên dịch sẵn trong BB10 NDK) luôn fail
    // handshake với error:1407742E dù đã thử QSsl::AnyProtocol/SecureProtocols
    // — trong khi OpenSSL 1.0.2g link cùng app thực sự hỗ trợ TLS 1.2. Giới
    // hạn nằm ở tầng QSslSocket riêng của BlackBerry, không lộ ra qua API
    // public (enum QSsl::SslProtocol còn không có TlsV1_1/TlsV1_2).
    //
    // Fix: kết nối TCP thuần bằng QTcpSocket, tự dựng TLS bằng OpenSSL C API
    // thẳng trên fd của nó (wsTlsHandshakeStep(), gọi từ onWsConnected() sau
    // khi TCP xong, lặp lại từ onWsReadyRead() nếu handshake chưa xong).
    // Bỏ qua hoàn toàn QSslSocket cho kết nối WS này.
    m_webSocket->connectToHost(url.host(), port);

    m_webSocket->setProperty("wsUrl", url.toString());
}

// ─── Raw OpenSSL TLS layer (thay QSslSocket) ───────────────────────────────
// Chỉ dùng cho kết nối WS khi m_wsUseSsl == true. Ép thẳng TLS 1.2, bỏ qua
// hoàn toàn lớp QSslSocket của BlackBerry — xem lý do đầy đủ ở connectWebSocket().

static void zalo10LogOpenSslErrors(const char *context)
{
    unsigned long e;
    char buf[256];
    bool any = false;
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        qDebug() << "[Zalo WS TLS]" << context << "OpenSSL error:" << buf;
        any = true;
    }
    if (!any)
        qDebug() << "[Zalo WS TLS]" << context << "(no OpenSSL error queued)";
}

// Gọi từ onWsConnected() ngay sau khi TCP bắt tay xong, và lặp lại từ
// onWsReadyRead() nếu SSL_connect() lần trước trả về WANT_READ. Trả về true
// nếu handshake ĐÃ XONG (thành công hoặc lỗi hẳn), false nếu vẫn đang chờ.
bool ZaloService::wsTlsHandshakeStep()
{
    if (!m_webSocket) return true;

    if (!m_wsSslCtx) {
        // Khởi tạo SSL_CTX lần đầu — ép thẳng TLS 1.2, không cho OpenSSL tự
        // đàm phán xuống bản cũ hơn.
        const SSL_METHOD *method = TLSv1_2_client_method();
        if (!method) {
            qDebug() << "[Zalo WS TLS] TLSv1_2_client_method() null — bản OpenSSL link cùng app quá cũ, không có API này";
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
        // Không verify chain ở đây (giữ đúng hành vi cũ — QSslSocket trước đây
        // cũng ignoreSslErrors() vô điều kiện). Có thể siết lại sau khi WS
        // connect ổn định, nhưng không phải trọng tâm của lần sửa này.
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

        // SNI — nhiều server (kể cả cụm *-msg.chat.zalo.me) từ chối/trả sai
        // chứng chỉ nếu thiếu SNI. QSslSocket tự làm ngầm; tự set tay ở đây.
        QString hostStr = QUrl(m_webSocket->property("wsUrl").toString()).host();
        QByteArray hostBytes = hostStr.toUtf8();
        SSL_set_tlsext_host_name(m_wsSsl, hostBytes.constData());

        // QUAN TRỌNG: dùng memory-BIO pair, KHÔNG dùng SSL_set_fd() gắn thẳng
        // vào fd của QTcpSocket. Lý do: QTcpSocket luôn tự đọc dữ liệu đến từ
        // fd vào buffer nội bộ ngay khi có (không tắt được). Nếu OpenSSL cũng
        // đọc trực tiếp cùng fd qua SSL_set_fd(), hai bên tranh đọc cùng 1
        // nguồn — Qt luôn hút trước, khiến SSL_read()/SSL_connect() không
        // bao giờ thấy dữ liệu, kẹt ở WANT_READ vĩnh viễn, im lặng không lỗi.
        //
        // Fix: rbio/wbio là buffer RAM thuần. onWsReadyRead() tự readAll() từ
        // QTcpSocket rồi BIO_write() ciphertext vào rbio cho OpenSSL đọc. Sau
        // mỗi lần gọi SSL_connect()/SSL_read()/SSL_write(), wsPumpTlsOutput()
        // rút hết ciphertext OpenSSL ghi vào wbio rồi đẩy ra qua
        // QTcpSocket::write() thật. Chỉ QTcpSocket đụng vào fd thật.
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
        // Mặc định, đọc hết 1 memory BIO rỗng sẽ báo "EOF" (0) — OpenSSL sẽ
        // hiểu nhầm thành server đóng kết nối sạch thay vì "chưa có gì, thử
        // lại sau". set_mem_eof_return(-1) khiến đọc rỗng trả về "would
        // block" (đúng semantics WANT_READ cho non-blocking).
        BIO_set_mem_eof_return(rbio, -1);
        BIO_set_mem_eof_return(wbio, -1);
        SSL_set_bio(m_wsSsl, rbio, wbio); // từ đây SSL_free(m_wsSsl) sẽ tự free luôn rbio+wbio
        SSL_set_connect_state(m_wsSsl);
    }

    int ret = SSL_connect(m_wsSsl);
    // Bất kể ret là gì, SSL_connect() có thể đã ghi ciphertext (ClientHello,
    // hoặc phần tiếp theo của handshake) vào wbio — luôn rút ra và đẩy thật
    // ra mạng qua QTcpSocket trước khi xử lý tiếp.
    wsPumpTlsOutput();

    if (ret == 1) {
        qDebug() << "[Zalo WS TLS] TLS handshake OK, phiên bản:" << SSL_get_version(m_wsSsl)
                  << "cipher:" << SSL_get_cipher(m_wsSsl);
        m_wsTlsEstablished = true;
        onWsEncrypted(); // gửi HTTP Upgrade — giữ nguyên logic cũ
        return true;
    }

    int sslErr = SSL_get_error(m_wsSsl, ret);
    if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE) {
        // Với memory BIO, WANT_WRITE gần như không thể xảy ra (wbio là RAM,
        // không "đầy" theo nghĩa kernel send buffer) — trên thực tế hầu như
        // luôn là WANT_READ: OpenSSL đã ghi xong ClientHello vào wbio (đã
        // pump ở trên), giờ đang chờ ServerHello/cert từ server. Chờ
        // onWsReadyRead() lần sau (đọc được thêm ciphertext, BIO_write vào
        // rbio) rồi gọi lại wsTlsHandshakeStep() tiếp tục đúng chỗ dở dang.
        return false;
    }

    // Lỗi thật — hết đường, coi như host này fail ở tầng TLS (giống hệt cách
    // xử lý QSslSocket lỗi trước đây).
    qDebug() << "[Zalo WS TLS] SSL_connect failed, SSL_get_error=" << sslErr;
    zalo10LogOpenSslErrors("SSL_connect");
    m_wsAdvanceUrlOnReconnect = true;
    disconnectWebSocket();
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(wsNextReconnectDelayMs());
    return true;
}

// Rút hết ciphertext OpenSSL đã ghi vào wbio (do SSL_connect/SSL_write/
// SSL_read sinh ra — record TLS, ClientHello, Finished, v.v.) rồi đẩy ra
// mạng thật qua QTcpSocket::write(). Gọi sau MỌI lần gọi hàm SSL_* có khả
// năng sinh ra dữ liệu cần gửi đi.
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

// Dùng thay cho mọi m_webSocket->write(...) trước đây — tự chọn SSL_write()
// (qua memory BIO, xem wsTlsHandshakeStep()) hay QTcpSocket::write() thuần
// tuỳ m_wsUseSsl. Với memory BIO, SSL_write() gần như không bao giờ trả
// WANT_READ/WANT_WRITE (không bị giới hạn bởi kernel send buffer như fd
// thật), nên không cần vòng lặp chờ phức tạp như bản dùng SSL_set_fd() cũ.
qint64 ZaloService::wsWriteRaw(const QByteArray &data)
{
    if (!m_webSocket) return -1;
    if (!m_wsUseSsl || !m_wsSsl || !m_wsTlsEstablished)
        return m_webSocket->write(data);

    int n = SSL_write(m_wsSsl, data.constData(), data.size());
    wsPumpTlsOutput(); // đẩy ciphertext vừa sinh ra ra mạng thật
    if (n <= 0) {
        int sslErr = SSL_get_error(m_wsSsl, n);
        qDebug() << "[Zalo WS TLS] SSL_write failed, SSL_get_error=" << sslErr;
        zalo10LogOpenSslErrors("SSL_write");
        return -1;
    }
    return n;
}

// Rút hết dữ liệu plaintext đang có sẵn từ SSL_read() (đã giải mã, đọc từ
// rbio — xem onWsReadyRead() chỗ BIO_write() bơm ciphertext vào rbio trước
// khi gọi hàm này) — gọi từ onWsReadyRead() khi TLS đã established, thay vì
// m_webSocket->readAll().
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
            break; // hết dữ liệu sẵn có lúc này, bình thường
        if (sslErr == SSL_ERROR_ZERO_RETURN)
            break; // server đóng TLS session sạch — onWsDisconnected() lo phần còn lại
        // Lỗi khác — log rồi thoát vòng lặp, để dữ liệu đã đọc được (nếu có)
        // vẫn được xử lý bình thường.
        zalo10LogOpenSslErrors("SSL_read");
        break;
    }
    return out;
}

// Đóng WS "sạch" (gửi Close frame chuẩn, opcode 0x8) trước khi process bị
// kill — thay cho abort() trần trụi trong disconnectWebSocket() bên dưới.
//
// Lý do: log thực tế cho thấy cookie zpw_sek chết rất nhanh sau khi app bị
// đóng (~19s sau lần keepAlive vừa OK). Khả năng server phân biệt "client tự
// đóng đúng chuẩn WS" với "client rớt/bị kill bất ngờ" và xử lý session khác
// nhau — giả thuyết chưa xác nhận chắc, nhưng chi phí thử rất thấp và đúng
// chuẩn giao thức.
//
// Gọi từ ApplicationUI::onManualExit() ngay trước quit() — lúc đó không còn
// event loop để chờ async, nên dùng waitForBytesWritten() (blocking) để đảm
// bảo frame ra khỏi tiến trình trước khi bị kill.
void ZaloService::closeWebSocketGracefully()
{
    if (!m_webSocket || !m_wsConnected) {
        qDebug() << "[Zalo WS] closeWebSocketGracefully: no active WS, skip";
        return;
    }

    qDebug() << "[Zalo WS] Sending graceful Close frame before exit";
    wsWriteRaw(maskWsFrame(0x8, QByteArray()));
    m_webSocket->flush();
    m_webSocket->waitForBytesWritten(300); // blocking — không còn event loop để chờ async
}

void ZaloService::disconnectWebSocket()
{
    // Dọn SSL trước khi đụng vào socket bên dưới — SSL_free không tự đóng fd
    // (fd do QTcpSocket sở hữu), chỉ giải phóng state TLS nội bộ của OpenSSL.
    if (m_wsSsl) {
        SSL_free(m_wsSsl); // chỉ free SSL*, KHÔNG free SSL_CTX (free riêng dưới đây)
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

void ZaloService::onWsConnected()
{
    // TCP bắt tay xong. Với ws:// thường, gửi HTTP Upgrade luôn. Với wss://,
    // KHÔNG còn dựa vào QSslSocket::encrypted() signal nữa (đã bỏ QSslSocket
    // hoàn toàn cho kết nối WS — xem lý do đầy đủ ở connectWebSocket()) — tự
    // bắt đầu handshake TLS bằng tay qua wsTlsHandshakeStep(). Nếu handshake
    // chưa xong ngay lần gọi đầu (WANT_READ, rất thường gặp — cần đợi
    // ServerHello từ server), wsTlsHandshakeStep() sẽ được gọi lại tiếp từ
    // onWsReadyRead() mỗi khi có thêm dữ liệu, cho tới khi xong hẳn.
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
    // Không còn là Qt slot (QTcpSocket không có signal encrypted()) — gọi
    // trực tiếp từ wsTlsHandshakeStep() ngay khi SSL_connect() trả về 1
    // (handshake TLS xong). Giữ nguyên tên + logic cũ để đỡ phải sửa chỗ
    // khác: gửi HTTP Upgrade ngay khi kênh đã mã hoá.
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

    wsWriteRaw(handshake.toUtf8());
    qDebug() << "[Zalo WS] HTTP Upgrade sent to" << url.host() << path.left(60);
}

// Không còn được gọi (QTcpSocket không có signal sslErrors — TLS giờ tự
// dựng bằng OpenSSL thô, lỗi TLS được xử lý ngay trong wsTlsHandshakeStep()
// qua zalo10LogOpenSslErrors()). Giữ lại hàm rỗng thay vì xoá khỏi header để
// không phải sửa thêm chỗ khác lỡ có nơi nào còn tham chiếu.
void ZaloService::onWsSslErrors(const QList<QSslError> &errors)
{
    Q_UNUSED(errors);
}

void ZaloService::onWsSocketError(QAbstractSocket::SocketError err)
{
    // Log lỗi socket cấp thấp (TCP refused, host not found, timeout, SSL
    // handshake failure...) — trước đây onWsDisconnected() chỉ in "Disconnected"
    // suông, không biết WS fail vì lý do gì.
    if (!m_webSocket) return;
    qDebug() << "[Zalo WS] Socket error:" << err
              << m_webSocket->errorString();
    // Bất kỳ lỗi tầng thấp nào trước khi WS handshake xong (kể cả SSL
    // handshake fail) đều coi là dấu hiệu host hiện tại có vấn đề — báo để
    // lần reconnect tới thử host khác trong pool zpw_ws.
    m_wsAdvanceUrlOnReconnect = true;
}

void ZaloService::onWsReadyRead()
{
    if (!m_webSocket) return;

    // QUAN TRỌNG: luôn readAll() từ QTcpSocket THẬT trước (đây là nơi DUY
    // NHẤT đọc dữ liệu từ mạng). Với wss://, đây vẫn là ciphertext thô — bơm
    // vào rbio cho OpenSSL tự giải mã, KHÔNG đưa thẳng vào m_wsBuffer
    // (m_wsBuffer chỉ chứa dữ liệu WS/HTTP đã giải mã xong).
    QByteArray raw = m_webSocket->readAll();
    if (m_wsUseSsl && m_wsSsl && !raw.isEmpty()) {
        BIO *rbio = SSL_get_rbio(m_wsSsl);
        if (rbio) BIO_write(rbio, raw.constData(), raw.size());
    }

    if (m_wsUseSsl && !m_wsTlsEstablished) {
        // Còn đang dở handshake TLS (lần gọi trước WANT_READ) — ciphertext
        // vừa bơm vào rbio ở trên chính là phần tiếp theo của
        // ServerHello/cert/... OpenSSL cần. Tiếp tục handshake ngay tại đây;
        // KHÔNG đụng vào m_wsBuffer — chưa có gì để parse ở tầng WS/HTTP.
        wsTlsHandshakeStep();
        return;
    }

    m_wsBuffer += m_wsUseSsl ? wsReadDecrypted() : raw;
    if (m_wsUseSsl) wsPumpTlsOutput();

    if (!m_wsConnected) {
        // Đang chờ HTTP 101 Switching Protocols
        int headerEnd = 0;
        if (parseWsHandshakeResponse(m_wsBuffer, headerEnd)) {
            qDebug() << "[Zalo WS] Upgraded OK, WebSocket connected";
            m_wsConnected = true;
            m_wsConsecutiveFailCount = 0; // reset backoff — kết nối thật sự đã thành công
            m_wsBuffer    = m_wsBuffer.mid(headerEnd);
            // Server sẽ tự gửi cmd=1 handshake ngay sau khi connect
        } else if (m_wsBuffer.contains("\r\n\r\n")) {
            // HTTP response nhưng không phải 101
            qDebug() << "[Zalo WS] Upgrade failed:" << m_wsBuffer.left(200);
            disconnectWebSocket();
            if (!m_wsReconnectTimer->isActive())
                m_wsReconnectTimer->start(wsNextReconnectDelayMs());
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
        wsWriteRaw(pong);
        return;
    }
    if (opcode != 0x2 && opcode != 0x1) return; // Chỉ xử lý binary/text

    handleWsMessage(opcode, payload);
}

// Giải mã payload thô của 1 command WS { data: "<base64>", encrypt: 0|1|2|3 }
// thành QVariantMap. Tách ra từ logic xử lý cmd=501/521 (tin nhắn mới) để
// cmd=601 (group_event) dùng lại cùng pipeline GCM-decrypt/gzip-inflate/
// AES-CBC-fallback. debugTag chỉ ảnh hưởng prefix trong log.
QVariantMap ZaloService::decodeWsEnvelope(const QVariantMap &outer, const QString &debugTag)
{
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
        qDebug() << "[Zalo WS] GCM decrypt" << debugTag << ": keyLen=" << m_wsCipherKey.size()
                 << "cipherLen=" << cipherBytes.size()
                 << "keyHex8=" << m_wsCipherKey.left(8).toHex()
                 << "ivHex8=" << cipherBytes.left(8).toHex();
        QByteArray plain = aesGcmDecrypt(m_wsCipherKey, cipherBytes);
        qDebug() << "[Zalo WS] GCM result" << debugTag << ": plainLen=" << plain.size();
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
                qDebug() << "[Zalo WS] inflated" << debugTag << "(first500):" << QString::fromUtf8(inflated.left(500));
                QVariantMap parsed = jsonToMap(inflated);
                // zca-js: inflate result is direct JSON, no "data" wrapper
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed; // direct struct: { ms:[], msgs:[], controls:[], ... }
            } else {
                qDebug() << "[Zalo WS] inflate FAILED" << debugTag << ", trying raw plain";
                qDebug() << "[Zalo WS] plain (first150):" << QString::fromUtf8(plain.left(150));
                QVariantMap parsed = jsonToMap(plain);
                if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                    d = parsed["data"].toMap();
                else
                    d = parsed;
            }
        } else if (!plain.isEmpty()) {
            qDebug() << "[Zalo WS] encType=3 plain" << debugTag << "(first150):" << QString::fromUtf8(plain.left(150));
            QVariantMap parsed = jsonToMap(plain);
            if (parsed.contains("data") && parsed["data"].type() == QVariant::Map)
                d = parsed["data"].toMap();
            else
                d = parsed;
        } else {
            qDebug() << "[Zalo WS] decrypt returned empty" << debugTag << "for encType=" << encType;
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
        QVariantMap d = decodeWsEnvelope(outer, "cmd501");

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
                if (m_seenMsgIds.contains(msgId)) {
                    // Đã lưu rồi — thường qua response HTTP của onSendMsgDone()
                    // cho tin nhắn gửi đi của MÌNH, lúc đó chưa có ts server
                    // nên fallback dùng giờ máy (có thể lệch hàng giờ so với
                    // server thật). Push WS này mang ts server thật cho cùng
                    // tin nhắn — patch lại ngay để row DB được sửa đúng, thay
                    // vì bị continue bỏ qua âm thầm.
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

            // Cùng guard tombstone như history path cmd=510 bên dưới: nếu
            // msgId này đã bị xóa cứng qua "xóa cho tôi", không bao giờ cho
            // nó chạm lại UI/DB, kể cả khi push bị trùng/retry.
            if (isMessageDeletedForMe(msgId)) {
                qDebug() << "[Zalo WS] cmd=501/521: skip msg" << msgId << "(tombstoned, deleted-for-me)";
                continue;
            }

            // chat.undo = thông báo thu hồi/unsend, không phải tin nhắn thật —
            // update tin gốc tại chỗ thay vì append thành 1 bubble JSON lạc lõng.
            QString recalledId = extractRecalledMsgId(m);
            if (!recalledId.isEmpty()) {
                markMessageRecalled(threadId, recalledId);
                continue;
            }

            // chat.delete = thông báo "xóa cho tôi". Zalo gửi cho CẢ 2 phía
            // trong thread bất kể ai thực sự bấm xóa, nhưng hành động chỉ nên
            // ảnh hưởng màn hình của người xóa. Nên: chỉ hard-delete row local
            // nếu CHÍNH MÌNH là người xóa; ngược lại là action local-only của
            // người khác, bỏ qua ở đây.
            QString delMsgId, deleterUid;
            if (extractDeleteInfo(m, delMsgId, deleterUid)) {
                if (deleterUid == m_uid) {
                    markMessageDeletedForMe(threadId, delMsgId);
                }
                continue;
            }

            // chat.reaction fallback — vẫn giữ như 1 check phụ vô hại, nhưng
            // cmd=612 (xem handler riêng ở dưới) mới là kênh reaction thật đã
            // xác nhận qua log thiết bị thật. "chat.reaction" trong luồng
            // message thường cmd=501/521 này là suy đoán ban đầu chưa xác
            // nhận, thực tế gần như không khớp gì — vẫn để đây phòng khi có
            // đường dẫn server khác dùng, nhưng cmd=612 mới là cái bắn
            // reactionUpdated() hiện tại.
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

            // Reply/quote: Zalo WS gửi kèm object "quote" trên tin nhắn reply
            // 1 tin trước đó. globalMsgId là msgId thật của tin được quote
            // (tap-to-jump); "msg" là snippet text; "fromD" là tên hiển thị
            // người gửi tin được quote — đủ cho reply-preview strip mà không
            // cần lookup lần 2. Không có ở tin nhắn thường, nên quoteMsgId
            // rỗng nghĩa là "không phải reply".
            QVariantMap quoteObj = m["quote"].toMap();
            if (!quoteObj.isEmpty()) {
                // globalMsgId/ownerId are read as plain .toString() now —
                // jsonToMap() (see quoteBigJsonInts() in ZaloServiceUtils.hpp)
                // already rewrites any bare 16+-digit JSON number into a
                // quoted string before QScriptEngine's JSON.parse ever sees
                // it, so these arrive as exact strings straight out of
                // QVariantMap. Previously this code round-tripped them
                // through toLongLong()/QString::number() to "recover" the
                // real id — but by that point the value had ALREADY been
                // silently rounded by JSON.parse's IEEE-754 double parsing
                // (confirmed on-device: 860110201644973228 was coming back
                // as 860110201644973200), so no amount of care here could
                // undo it. Fixing it at the parse layer instead of here
                // means every big-int field benefits, not just these two.
                out["quoteMsgId"]      = quoteObj["globalMsgId"].toString();
                out["quoteContent"]    = quoteObj["msg"].toString();
                out["quoteSenderName"] = quoteObj["fromD"].toString();
                out["quoteMsgType"]    = quoteObj["cliMsgType"].toInt();
                // ownerId là uid thật của người gửi tin nhắn được quote —
                // QML dùng để phân biệt "quote chính mình" hay "quote người
                // khác" (fromD/dName không đủ tin cậy).
                //
                // "0" = quy ước self (giống field uidFrom ở top-level) cũng
                // áp dụng bên TRONG object quote lồng nhau ở chat 1-1: khi
                // tin được quote là tin MÌNH gửi, Zalo báo quote.ownerId =
                // "0", không phải uid thật. Nếu giữ nguyên chuỗi "0" thì
                // quoteOwnerId sẽ không bao giờ khớp selfUid phía QML, khiến
                // quoteIsMine luôn false.
                QString qOwnerIdStr = quoteObj["ownerId"].toString();
                out["quoteOwnerId"] = (qOwnerIdStr == "0") ? m_uid : qOwnerIdStr;
            }

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
                // Video/file (msgType wire = "share.file"): content mang
                // title (tên file gốc), href (URL CDN tải), description,
                // thumb, params ({fileSize, checksum,...}). Chuẩn hóa content
                // thành {"fileName":...,"href":...,"fileSize":...} để QML
                // render bubble file — cùng shape với content video mình tự
                // gửi (xem sendVideo()/handleFileUploadDone() ở
                // ZaloService_Messages.cpp) để 1 bubble QML dùng chung được
                // cho cả 2 chiều.
                else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                    mt = 3;
                // Call log bubble (msgType wire = "chat.recommended"): content
                // carries action ("recommened.calltime" = ended normally,
                // "recommened.misscall" = missed/rejected/no-answer) and a
                // stringified params JSON with duration/calltype(0=voice,1=video)/
                // isCaller. Normalize into {"callResult","callKind","duration"}
                // so QML has one flat shape to key off, same idea as the
                // share.file normalization above.
                else if (mtStr.contains("chat.recommended"))
                    mt = 4;
                // Sticker (msgType wire = "chat.sticker"): content =
                // {"id":18009,"catId":10130,"type":7}. Xác nhận bằng thực
                // nghiệm (không phải suy luận): id trong content ghép thẳng
                // vào eid của endpoint ảnh public
                // https://zalo-api.zadn.vn/api/emoticon/sticker/webpc?eid={id}
                // cho ra đúng sticker đã gửi — không cần catId, không cần
                // gọi thêm API tra cứu nào (đã thử và loại: eid KHÔNG suy ra
                // được từ catId, và getStickersDetail của zca-js là 1 API
                // riêng chỉ cần khi *tìm/gửi* sticker, không cần để hiển thị
                // sticker đã *nhận*). Chuẩn hóa content thành
                // {"stickerId":N} cho gọn, QML tự ghép URL.
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
                qDebug() << "[Zalo WS] chat.sticker bubble: id=" << stickerId;
                // Kick off the image download here (C++, right when we first learn the
                // stickerId) instead of waiting on ChatView.qml to trigger it once the
                // bubble renders. QML-side init signals (Component.onCompleted,
                // onVisibleChanged) both proved unreliable for this on device — either
                // zService wasn't resolvable yet from that nested delegate Container, or
                // the signal just never fired for the initial state. Firing eagerly here
                // means downloadSticker()'s own dedup (m_pendingStickers/m_stickerCache)
                // does the "already downloading/cached" check, and the QML side only ever
                // needs to *read* ListItemData.stickerLocalPath once applyStickerUpdate()
                // patches it in — no trigger logic left in QML at all.
                if (stickerId > 0) downloadSticker(QString::number(stickerId));
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
                qDebug() << "[Zalo WS] chat.recommended call bubble: result=" << callResult
                         << "kind=" << callKind << "duration=" << duration;
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
                    qDebug() << "[Zalo WS] share.file detected: fileName=" << fTitle
                             << "href=" << fHref.left(80) << "size=" << fSize;
                } else {
                    qDebug() << "[Zalo WS] share.file but no href found, keys=" << fm.keys();
                }
            }

            // QScriptEngine của Qt4 tự convert nested JSON object thành
            // QVariantMap, nên content.toString() trả rỗng với payload
            // ảnh/object — re-serialize lại thành chuỗi JSON để phần còn lại
            // của pipeline parse như bình thường.
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

            // Chưa tìm được content — fallback sang paramsExt/previewThumb, nơi
            // WS real-time đôi khi mang data ảnh thay vì content trực tiếp.
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

            // Zalo WS real-time photo: msgType may be 0 but photo URLs are in paramsExt/previewThumb.
            // mt == 3 (video/file) loại trừ tường minh — nếu share.file thiếu
            // href, để nó rơi vào text thô thay vì bị normalizePhotoContent()
            // nhận nhầm thành ảnh.
            if (mt != 3 && mt != 4 && mt != 5 && (mt == 2 || rawContent.isEmpty())) {
                QString normalized = normalizePhotoContent(m, rawContent);
                if (normalized != rawContent && !normalized.isEmpty()) {
                    rawContent = normalized;
                    mt = 2;
                    out["msgType"] = 2;
                    qDebug() << "[Zalo WS] photo detected via paramsExt/previewThumb: content=" << rawContent.left(80);
                    // Log toàn bộ payload (rút gọn) vì content/attach có thể
                    // vượt 500 ký tự, và URL ảnh có thể nằm ở 1 trong 3 chỗ này.
                    qDebug() << "[Zalo WS] PHOTO content=" << m["content"].toString().left(500);
                    qDebug() << "[Zalo WS] PHOTO attach="  << m["attach"].toString().left(500);
                    qDebug() << "[Zalo WS] PHOTO params="  << m["params"].toString().left(500);
                }
            }
            if (mt == 2) {
                // Chuẩn hóa content ảnh thành {"normalUrl":"...","thumbUrl":"...","hdUrl":"..."}
                // WS có thể gửi qua content JSON (href/thumb), top-level, hoặc trong object con "attach"
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
                // Check field top-level
                if (nUrl.isEmpty()) nUrl = m["normalUrl"].toString();
                if (hUrl.isEmpty()) hUrl = m["hdUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumbUrl"].toString();
                if (nUrl.isEmpty()) nUrl = m["oriUrl"].toString();
                if (tUrl.isEmpty()) tUrl = m["thumb"].toString();
                if (fSizeStr.isEmpty()) fSizeStr = m["hdSize"].toString();
                // Check object con "attach" (Zalo WS gửi real-time)
                if (nUrl.isEmpty()) {
                    QVariantMap att = m["attach"].toMap();
                    if (att.isEmpty()) {
                        // attach có thể là chuỗi JSON
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
                // Check object con "params"
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
                    // Lấy caption từ kết quả normalizePhotoContent (đã gọi ở
                    // trên, có thể đã đặt key "caption" vào rawContent)
                    // trước khi mình ghi đè nó.
                    QString caption;
                    if (!rawContent.isEmpty() && rawContent.contains("\"caption\":\"")) {
                        QVariantMap prevCm = jsonToMap(rawContent.toUtf8());
                        caption = prevCm["caption"].toString();
                    }
                    // Thử lấy trực tiếp từ message map (field title)
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

                    // Nếu đây là WS echo của ảnh MÌNH vừa gửi, đã biết chính
                    // xác fileSize/fileName gốc từ sendPhoto() — ưu tiên dùng
                    // cái đó thay vì hdSize (có thể thiếu) server echo lại.
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

                    // Self-echo của ảnh mình vừa gửi: dùng lại file cache local
                    // thay vì tải lại từ CDN. Fetch bản CDN ngay sau khi upload
                    // là 1 cuộc đua — file có thể vẫn 404/rỗng phía server, từng
                    // khiến ảnh "của mình" hiện ô xám vĩnh viễn sau khi
                    // placeholder trong RAM bị thay bằng row DB này. File local
                    // đã được copy vào cache "/tmp" persistent lúc chọn ảnh (xem
                    // cacheLocalImage()) và không bao giờ bị xóa trừ clearCache().
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
                // Log toàn bộ key set khi không tìm được URL, giúp chẩn đoán
                // dạng payload mà logic parse ở trên chưa xử lý được.
                if (nUrl.isEmpty()) {
                    qDebug() << "[Zalo WS] photo msgType=2 but no URL found. m.keys=" << m.keys()
                             << "content=" << rawContent.left(100);
                }
                // Nếu nUrl KHÔNG phải URL HTTP thật (là protobuf thumbnail
                // của Zalo):
                // 1. Decode thumbnail ngay để preview nhanh.
                // 2. Fetch bản full-res qua WS cmd=510 history để lấy URL HTTP thật.
                if (!nUrl.isEmpty() && !nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    qDebug() << "[Zalo WS] photo has protobuf thumb (not HTTP URL), decoding thumbnail msgId=" << msgId;
                    downloadImageMessage(msgId, nUrl, threadId);
                    // Fetch full-res qua WS cmd=510 VÀ HTTP API song song
                    if (!m_pendingPhotoMsgIds.contains(msgId)) {
                        fetchPhotoViaWs510(msgId, threadId);
                        fetchPhotoViaHttp(msgId, threadId);
                    }
                } else if (!nUrl.isEmpty() && nUrl.startsWith("http") && !msgId.isEmpty() && out["localImage"].toString().isEmpty()) {
                    // URL ảnh HTTP thường: trước đây chỉ được ChatView fetch
                    // lazy khi user thực sự mở thread và bubble được render.
                    // Nghĩa là ảnh bị thu hồi trước khi user xem chat sẽ
                    // không có file cache local để fallback, nên "Show
                    // Recalled Messages" chỉ hiện được placeholder. Cache
                    // eager ngay ở đây khi tin nhắn vừa đến qua WS, bất kể
                    // thread nào đang mở. downloadImageMessage() idempotent
                    // (check m_avatarCache/m_pendingAvatars trước), nên đây
                    // là no-op nếu ChatView đã request/có sẵn rồi.
                    downloadImageMessage(msgId, nUrl, threadId);
                }
            }
            // FIX: out["msgType"] được set từ đầu (m["msgType"].toInt()) trước
            // khi 'mt' được resolve xong, và Zalo đôi khi gửi msgType dạng
            // chuỗi ("chat.photo") thay vì số — QVariant::toInt() trên chuỗi
            // không phải số sẽ âm thầm trả về 0. 'mt' được sửa thành 2 qua
            // fallback theo tên chuỗi ở phía trên, nhưng out["msgType"] chỉ
            // được đồng bộ lại bên trong 1 nhánh điều kiện không chạy khi
            // content đã được parse đầy đủ qua bước serialize nested-object ở
            // trên (normalized == rawContent trong trường hợp đó, nhánh bị
            // bỏ qua). Kết quả: out["msgType"] vẫn là 0 với hầu hết tin nhắn
            // ảnh thật dù 'mt' đã đúng là 2 — và vì onNewMessage phía QML chỉ
            // copy file ảnh cache local từ placeholder "local_img_..." sang
            // row đã confirm khi msg.msgType === 2, việc đó không xảy ra, nên
            // bubble ảnh không có hình. Luôn đồng bộ out["msgType"] theo mt
            // đã resolve local ở đây, vô điều kiện.
            out["msgType"] = mt;
            out["content"] = rawContent;

            qDebug() << "[Zalo WS] new msg from" << uidFrom
                     << "thread" << threadId << "content=" << out["content"].toString().left(60);
            dbSaveMessage(out, threadId);
            // Giữ m_threadLastMsgId cập nhật cho cả tin nhắn real-time
            // (cmd=501/521), không chỉ history cmd=510 — nếu không,
            // fetchMessages() (ZaloService_Messages.cpp) sẽ luôn fallback về
            // lastId="0" lần sau mở lại thread, re-fetch TOÀN BỘ history từ
            // server thay vì chỉ phần thay đổi.
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
                } else if (mt == 4) {
                    QString cs = out["content"].toString();
                    bool missed = cs.contains("\"callResult\":\"missed\"");
                    bool video  = cs.contains("\"callKind\":\"video\"");
                    msgPreview = missed ? (video ? "Missed video call" : "Missed call")
                                        : (video ? "Video call" : "Voice call");
                } else if (mt == 5) {
                    msgPreview = "[Sticker]";
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

        // Kết quả rỗng cho 1 thread không còn active nghĩa là response đến
        // muộn cho request user đã rời đi — bỏ qua, không xóa model của
        // thread đang hiện hiện tại.
        if (rawMsgs.isEmpty() && emitThread != m_activeThreadId) {
            qDebug() << "[Zalo WS] cmd=510 empty stale response, discarding (emitThread="
                     << emitThread << "activeThread=" << m_activeThreadId << ")";
            return;
        }

        // Guard chống response cũ từ request trước: xác nhận ít nhất 1 tin
        // nhắn trong batch thực sự thuộc emitThread trước khi tin tưởng nó.
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

            // cmd=510 dùng chung 1 lastId toàn cục, nên 1 response có thể
            // mang tin nhắn từ nhiều thread cùng lúc — chỉ giữ tin thuộc emitThread.
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

            // Bỏ qua tin nhắn user đã xóa cứng qua "xóa cho tôi" ở phiên/lần
            // trước. Vòng lặp removeAt() bên dưới (xử lý chat.delete) chỉ bắt
            // được trường hợp event thông báo xóa nằm cùng batch history này
            // — nhưng khi thread đóng rồi mở lại, server vẫn replay tin nhắn
            // gốc mỗi lần resync mà không nhất thiết kèm event thông báo đó.
            // dbSaveMessage() đã từ chối lưu msgId bị tombstone, nhưng không
            // ngăn được nó lọt vào list `msgs` trả cho UI qua messagesReady —
            // nên vẫn có thể thoáng hiện trên màn hình dù đúng ra đã bị xóa
            // khỏi DB. Bỏ qua ở đây luôn.
            if (isMessageDeletedForMe(msgId)) {
                qDebug() << "[Zalo WS] old_messages: skip msg" << msgId << "(tombstoned, deleted-for-me)";
                continue;
            }

            // chat.undo = thông báo thu hồi, không phải tin nhắn thật. Patch tin
            // gốc nếu nó nằm trước đó trong cùng batch history, lưu recall vào
            // SQLite dù thế nào, và bỏ qua không thêm event này thành bubble riêng.
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

            // chat.delete = thông báo "xóa cho tôi", cùng guard self-only như
            // real-time path ở trên. Nếu batch history này cũng chứa tin nhắn
            // gốc, bỏ hẳn nó khỏi batch để không bị hồi sinh lần mở thread kế.
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

            // chat.reaction — xử lý giống real-time path ở trên, thêm bỏ event
            // thô khỏi batch history này để không bị hồi sinh thành bubble
            // riêng lẻ lần mở thread kế.
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
            // Cùng trường hợp msgType kiểu chuỗi như trên ("chat.photo" thay vì số).
            if (mtH == 0) {
                QString mtStr = m["msgType"].toString().toLower();
                if (mtStr.isEmpty()) mtStr = m["msgtype"].toString().toLower();
                if (mtStr.contains("photo") || mtStr.contains("image") || mtStr == "2")
                    mtH = 2;
                // FIX: nhánh real-time (line ~896) đã nhận diện "share.file" ->
                // mt=3, nhưng nhánh old_messages/history (cmd=510 poll) này thì
                // chưa từng có — nên khi cùng 1 tin nhắn video bị poll lại lần 2
                // (rất hay xảy ra, "DM incremental poll cmd=510" định kỳ), nó
                // rơi thẳng vào nhánh "nested content obj -> mt=2" bên dưới,
                // ghi đè msgType=2 + content sai dạng lên bản ghi DB đúng đã lưu
                // từ lần nhận real-time trước đó -> video hiển thị sai vĩnh
                // viễn kể cả sau khi khởi động lại app (vì DB đã bị hỏng thật,
                // không phải chỉ là dòng trùng tạm trong bộ nhớ).
                else if (mtStr.contains("share.file") || mtStr.contains("sharefile"))
                    mtH = 3;
                // Call log bubble history counterpart — cùng lý do đã ghi ở
                // real-time path (cmd=501/521, ~dòng 900): thiếu nhánh này ở
                // "old_messages" (cmd=510 poll) khiến content thô ("action":
                // "recommened.misscall", chưa chuẩn hóa) ghi đè lên bản ghi
                // DB đúng (đã chuẩn hóa) mỗi lần WS poll lại history, làm
                // callBubble ở ChatView.qml "biến mất" khi mở lại thread
                // (msgType vẫn=4 nhưng content không còn khớp {"callResult"
                // ...} mà callBubble.extractJsonStringField parse nữa).
                else if (mtStr.contains("chat.recommended"))
                    mtH = 4;
                // Sticker history counterpart (msgType wire = "chat.sticker")
                // — cùng lý do các nhánh trên: thiếu ở đây thì content thô
                // {"catId":...,"id":...,"type":...} ghi đè bản {"stickerId":N}
                // đã chuẩn hóa mỗi lần cmd=510 poll lại history.
                else if (mtStr.contains("chat.sticker"))
                    mtH = 5;
            }

            // Re-serialize nested content object lại thành JSON, cùng lý do như trên.
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
            // FIX: video/file (mt==3) counterpart to the photo normalization
            // right below — was missing entirely, so old_messages left
            // rawContentH as the raw {"title":...,"href":...,"params":"{...}"}
            // shape instead of the {"fileName":...,"href":...,"fileSize":...}
            // shape ChatView.qml's videoBubble parser expects. Same field
            // extraction as the real-time path (see share.file handling above
            // in this file's cmd=501/521 branch).
            if (mtH == 5) {
                QVariantMap smH = m["content"].toMap();
                if (smH.isEmpty() && !rawContentH.isEmpty() && rawContentH.trimmed().startsWith("{"))
                    smH = jsonToMap(rawContentH.toUtf8());
                qint64 stickerIdH = smH["id"].toString().toLongLong();
                if (stickerIdH == 0) stickerIdH = smH["id"].toLongLong();
                rawContentH = QString("{\"stickerId\":%1}").arg(stickerIdH);
                out["msgType"] = 5;
                qDebug() << "[Zalo WS] old_messages: chat.sticker bubble id=" << stickerIdH;
                // Eager download, same reasoning as the real-time (mt==5) branch above.
                if (stickerIdH > 0) downloadSticker(QString::number(stickerIdH));
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
                qDebug() << "[Zalo WS] old_messages: chat.recommended call bubble result=" << callResultH
                         << "kind=" << callKindH << "duration=" << durationH;
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
                    qDebug() << "[Zalo WS] old_messages: share.file detected fileName=" << fTitleH
                             << "href=" << fHrefH.left(80) << "size=" << fSizeH;
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
            // FIX: same root cause as the real-time WS path above — keep
            // out["msgType"] in sync with the locally-resolved mtH regardless
            // of which branch above did (or didn't) touch it.
            out["msgType"] = mtH;
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
                    // DUMP TOÀN BỘ RAW: in mọi field của message map để chẩn
                    // đoán field nào mang URL CDN thật trong bản build sau.
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

    // cmd=601 subCmd=0: group_event — hoạt động pin/note/poll board từ BẤT
    // KỲ thành viên nhóm nào (kể cả chính mình), cộng thêm thay đổi
    // membership/settings/... không liên quan ở đây.
    // Cùng shape envelope như cmd=501/521 (decodeWsEnvelope() xử lý được
    // cả 2), nhưng cấu trúc ngoài của payload là { controls: [...] } thay
    // vì { msgs: [...] } — mỗi control entry quan trọng ở đây có
    // content.act_type == "group" và chuỗi content.act xác định loại group
    // event; content.data là payload riêng theo từng loại (đôi khi encode
    // JSON lồng 2 lớp dưới dạng chuỗi, giống cách fetchGroupBoard() đã phải
    // xử lý phòng thủ cho field "params" của nó).
    //
    // Phần này mới/chưa xác nhận qua server thật ở lần sửa này — nếu wire
    // shape khác đi, các dòng qDebug() bên dưới in ra envelope thô và
    // content từng control để debug qua log thiết bị mà không cần suy
    // luận lại từ đầu.
    if (cmd == 601 && subCmd == 0) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d = decodeWsEnvelope(outer, "cmd601");
        qDebug() << "[Zalo WS] cmd601 envelope keys:" << d.keys();

        QVariantList controls = d["controls"].toList();
        for (int i = 0; i < controls.size(); ++i) {
            QVariantMap control = controls[i].toMap();
            QVariantMap content = control["content"].toMap();
            QString actType = content["act_type"].toString();

            // Video/file upload xong: server báo async qua đây, không phải
            // response HTTP của asyncfile/upload. content.data có thể là
            // QVariantMap hoặc chuỗi JSON tùy phiên bản — thử cả hai.
            // Khớp fileId với m_pendingVideoUpload (set trong sendVideo())
            // rồi tiếp tục bước 2 (gửi tin nhắn) qua handleFileUploadDone().
            if (actType == "file_done") {
                QString fileId = content["fileId"].toString();
                QVariant fdV = content["data"];
                QVariantMap fdMap = (fdV.type() == QVariant::String)
                    ? jsonToMap(fdV.toString().toUtf8())
                    : fdV.toMap();
                QString fileUrl = fdMap["url"].toString();
                if (fileUrl.isEmpty()) fileUrl = fdMap["fileUrl"].toString();
                qDebug() << "[Zalo WS] cmd601 file_done fileId=" << fileId
                         << "fileUrl=" << fileUrl.left(80);
                if (!fileId.isEmpty() && !fileUrl.isEmpty())
                    handleFileUploadDone(fileId, fileUrl);
                continue;
            }

            if (actType != "group") continue;

            QString act = content["act"].toString();
            if (act == "join_reject") continue; // event bị lặp lại nhiều lần, bỏ qua

            QVariant dataV = content["data"];
            QVariantMap eventData = (dataV.type() == QVariant::String)
                ? jsonToMap(dataV.toString().toUtf8())
                : dataV.toMap();

            qDebug() << "[Zalo WS] cmd601 group_event act=" << act << "keys=" << eventData.keys();

            QString groupId = eventData.contains("groupId") ? eventData["groupId"].toString()
                                                              : eventData["group_id"].toString();
            if (groupId.isEmpty()) continue;

            QString title;
            bool isSelf = false;
            bool relevant = true;
            // Thống nhất giữa các nhánh bên dưới để emit boardEventOccurred
            // duy nhất ở cuối không cần biết nhánh nào tạo ra nó. -1/""
            // nghĩa là "chưa rõ", không phải "không có".
            int topicType = -1;
            QString topicId;
            QString actorNameOut;

            if (act == "new_pin_topic" || act == "unpin_topic" || act == "update_pin_topic") {
                QString actorId = eventData["actorId"].toString();
                isSelf = (actorId == m_uid);
                QVariantMap topic = eventData["topic"].toMap();
                topicType = topic["type"].toInt(); // GroupTopicType: Note=0 Message=2 Poll=3
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
                    // update_board cũng cover poll vote (Zalo gửi lại toàn
                    // bộ topic mỗi lần có vote thay đổi) — không phân biệt
                    // được "tạo mới" với "vote" chỉ từ topicType, nên dùng
                    // câu chung chung thay vì đoán.
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
                relevant = false; // membership/settings/... — không phải hoạt động board, không liên quan ở đây
            }

            if (relevant && !title.isEmpty()) {
                emit boardEventOccurred(groupId, act, actorNameOut, isSelf, topicType, topicId, title);

                // Self-action đã có Hub notification từ chính path thành công
                // của onPinGroupMessageDone()/onCreateGroupNoteDone()/
                // onCreateGroupPollDone()/onVoteGroupPollDone() — bỏ qua ở
                // đây để tránh trùng. Cũng bỏ qua nếu board của group này
                // đang mở (cùng kiểu suppress "đang xem rồi" như tin nhắn
                // thường) — ChatView tự phản ứng qua boardEventOccurred ở
                // trên thay vì Hub notification.
                if (!isSelf && groupId != m_activeThreadId && !m_mutedThreads.contains(groupId)) {
                    QString grpName = m_groupNames.value(groupId, "Zalo10");
                    sendHubNotification(grpName, title, groupId, true);
                    sendBannerNotification(grpName, title, groupId, true);
                }
            }
        }
    }

    // cmd=612: push reaction real-time — kênh reaction RIÊNG, không phải
    // "chat.reaction" gộp trong luồng tin nhắn cmd=501/521. Payload decode
    // thành 2 list song song trong envelope: "reacts" (reaction 1-1) và
    // "reactGroups" (reaction nhóm) — đây là lý do 2 lần thử trước đều fail
    // âm thầm ở cả 2 chiều: reactMessage() gửi đi 1 shape payload server
    // không nhận ra là reaction hợp lệ (xem comment cập nhật của
    // reactMessage() trong ZaloService_Messages.cpp), còn reaction đến thì
    // không được bắt vì không có gì lắng nghe cmd=612 cả — reaction đến
    // dạng "chat.reaction" bên trong cmd=501/521 là suy đoán chưa xác nhận
    // của codebase này và không khớp traffic thật.
    //
    // Cùng shape envelope như cmd=501/521 (decodeWsEnvelope() xử lý được
    // cả 2). Field "content" của mỗi entry đến dạng chuỗi JSON, unpack
    // thành:
    //   { rMsg: [{ gMsgID, cMsgID, msgType }], rIcon, rType, source }
    // cùng với uidFrom/idTo/msgId/cliMsgId ở top-level của entry.
    // uidFrom == "0" nghĩa là "reaction này là CỦA MÌNH, echo lại từ 1 thiết
    // bị khác đang login" — cùng quy ước "0" = self đã dùng cho tin nhắn
    // cmd=501/521 ở trên.
    if (cmd == 612) {
        QVariantMap outer = jsonToMap(data);
        QVariantMap d = decodeWsEnvelope(outer, "cmd612");
        qDebug() << "[Zalo WS] cmd612 envelope keys:" << d.keys();

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
                bool isSelf = (rawUidFrom == "0"); // our own reaction, echoed from another device

                // threadId: với reaction nhóm, luôn là idTo (id nhóm); với
                // 1-1, isSelf cũng nghĩa idTo LÀ người kia.
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

                qDebug() << "[Zalo WS] cmd612" << (isGroupPass ? "reactGroups" : "reacts")
                         << "threadId=" << threadId << "msgId=" << reactMsgId
                         << "uidFrom=" << resolvedUidFrom << "icon=" << icon << "isSelf=" << isSelf;

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
    qDebug() << "[Zalo WS] Disconnected"
              << (m_webSocket ? m_webSocket->errorString() : QString("(socket already gone)"));
    m_wsConnected = false;
    m_wsCipherKey.clear();
    if (!m_loggedIn) return;
    if (!m_wsReconnectTimer->isActive())
        m_wsReconnectTimer->start(wsNextReconnectDelayMs());
}

void ZaloService::onWsReconnectTimer()
{
    qDebug() << "[Zalo WS] Reconnecting...";
    connectWebSocket();
}

// Backoff tăng dần: 5s, 10s, 20s, 40s, cap ở 60s. Mỗi lần gọi tăng
// m_wsConsecutiveFailCount thêm 1 — bộ đếm này được reset về 0 ngay khi WS
// upgrade thành công thật sự (xem onWsReadyRead), nên nó phản ánh đúng số
// lần thất bại LIÊN TIẾP kể từ lần connect thành công gần nhất, không phải
// tổng số lần thử từ đầu phiên.
int ZaloService::wsNextReconnectDelayMs()
{
    int n = m_wsConsecutiveFailCount;
    if (n > 3) n = 3; // cap luỹ thừa ở 2^3 = 8x base
    int delay = 5000 << n; // 5000,10000,20000,40000
    m_wsConsecutiveFailCount++;
    return delay;
}

