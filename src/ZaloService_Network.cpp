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

// Low-level HTTP request building, cookie handling, and persisted-session
// (save/load/refresh) management.

QString ZaloService::buildRawUrl(const QString &base, const QVariantMap &params)
{
    QString safeBase = base.trimmed().isEmpty() ? "https://wpa.chat.zalo.me" : base;

    QStringList qp;
    for (QVariantMap::const_iterator it = params.constBegin(); it != params.constEnd(); ++it) {
        QString k = QString::fromUtf8(QUrl::toPercentEncoding(it.key()));
        QString v = QString::fromUtf8(QUrl::toPercentEncoding(it.value().toString()));
        qp << (k + "=" + v);
    }
    if (qp.isEmpty()) return safeBase;
    return safeBase + "?" + qp.join("&");
}

QNetworkRequest ZaloService::buildRequest(const QString &urlStr, const QString &referer, bool jsonAccept)
{
    QUrl url = QUrl::fromEncoded(urlStr.toUtf8());
    QNetworkRequest req(url);

    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(sslConf);

    req.setRawHeader("User-Agent", m_userAgent.toUtf8());
    req.setRawHeader("Accept", jsonAccept ? "application/json, text/plain, */*" : "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");

    if (!referer.isEmpty()) {
        req.setRawHeader("Referer", referer.toUtf8());
        QString origin = referer;
        int slashCount = 0;
        int pos = 0;
        for(int i=0; i<referer.length(); i++) {
            if(referer[i] == '/') slashCount++;
            if(slashCount == 3) { pos = i; break; }
        }
        if(pos > 0) origin = referer.left(pos);
        req.setRawHeader("Origin", origin.toUtf8());
    }

    req.setRawHeader("Connection", "keep-alive");
    req.setRawHeader("sec-fetch-dest", "empty");
    req.setRawHeader("sec-fetch-mode", "cors");
    req.setRawHeader("sec-fetch-site", "same-origin");

    QString ck = buildCookieHeader();
    if (!ck.isEmpty()) {
        req.setRawHeader("Cookie", ck.toUtf8());
    }

    return req;
}

QString ZaloService::buildCookieHeader() const
{
    QStringList p;
    for (QMap<QString,QString>::const_iterator it = m_cookies.constBegin(); it != m_cookies.constEnd(); ++it)
        p << it.key() + "=" + it.value();
    return p.join("; ");
}

void ZaloService::parseCookiesFromReply(QNetworkReply *reply)
{
    typedef QPair<QByteArray, QByteArray> HeaderPair;
    foreach (const HeaderPair &hp, reply->rawHeaderPairs()) {
        if (hp.first.toLower() == "set-cookie") {
            QString line = QString::fromUtf8(hp.second);
            QString kv   = line.split(";").first().trimmed();
            int eq = kv.indexOf('=');
            if (eq > 0) {
                m_cookies[kv.left(eq).trimmed()] = kv.mid(eq + 1).trimmed();
            }
        }
    }
}

void ZaloService::saveSession()
{
    QSettings s("BerryLife", "Zalo10");
    s.setValue("uid",        m_uid);
    s.setValue("secretKey",  m_secretKey);
    s.setValue("imei",       m_imei);
    s.setValue("userAgent",  m_userAgent);
    s.setValue("chatUrl",    m_chatServiceUrl);
    s.setValue("groupUrl",   m_groupServiceUrl);
    s.setValue("profileUrl", m_profileServiceUrl);
    s.setValue("grpPollUrl", m_groupPollServiceUrl);
    s.setValue("grpBoardUrl", m_groupBoardServiceUrl);
    s.setValue("friendUrl",  m_friendServiceUrl);
    s.setValue("fileUrl",    m_fileServiceUrl);
    s.setValue("quickMsgUrl", m_quickMessageServiceUrl);
    s.setValue("zpwWsUrls",  m_zpwWsUrls);
    s.setValue("mutedThreads", QStringList(m_mutedThreads.toList()));

    QVariantMap cookieMap;
    QMapIterator<QString, QString> it(m_cookies);
    while (it.hasNext()) {
        it.next();
        cookieMap[it.key()] = it.value();
    }
    QString cookieJson = mapToJson(cookieMap);
    s.setValue("cookies", cookieJson);
    s.sync();
    qDebug() << "[Zalo] saveSession: saved" << m_cookies.size() << "cookies, uid=" << m_uid;
}

bool ZaloService::loadSession()
{
    QSettings s("BerryLife", "Zalo10");
    QString uid = s.value("uid").toString();
    if (uid.isEmpty()) {
        qDebug() << "[Zalo] loadSession: no saved session";
        return false;
    }

    m_uid              = uid;
    m_secretKey        = s.value("secretKey").toString();
    m_imei             = s.value("imei").toString();
    m_userAgent        = s.value("userAgent", QString::fromLatin1(USER_AGENT)).toString();
    m_chatServiceUrl      = s.value("chatUrl").toString();
    m_groupServiceUrl     = s.value("groupUrl").toString();
    m_profileServiceUrl   = s.value("profileUrl").toString();
    m_groupPollServiceUrl = s.value("grpPollUrl").toString();
    m_groupBoardServiceUrl = s.value("grpBoardUrl").toString();
    m_friendServiceUrl    = s.value("friendUrl").toString();
    m_fileServiceUrl      = s.value("fileUrl").toString();
    m_quickMessageServiceUrl = s.value("quickMsgUrl").toString();
    m_zpwWsUrls           = s.value("zpwWsUrls").toStringList();
    m_mutedThreads        = QSet<QString>::fromList(s.value("mutedThreads").toStringList());

    QString cookieJson = s.value("cookies").toString();
    if (!cookieJson.isEmpty()) {
        QVariantMap cookieMap = jsonToMap(cookieJson.toUtf8());
        QMapIterator<QString, QVariant> it(cookieMap);
        while (it.hasNext()) {
            it.next();
            m_cookies[it.key()] = it.value().toString();
        }
    }

    if (m_uid.isEmpty() || m_secretKey.isEmpty() || m_chatServiceUrl.isEmpty()) {
        qDebug() << "[Zalo] loadSession: incomplete session data";
        return false;
    }

    qDebug() << "[Zalo] loadSession: restored session uid=" << m_uid
             << "cookies=" << m_cookies.size();

    if (m_loggedIn) {
        m_loggedIn = false;
        emit loggedInChanged();
    }

    qDebug() << "[Zalo] loadSession: cookies restored, refreshing secretKey...";
    refreshSessionKey();

    return true;
}

// ─── refreshSessionKey ────────────────────────────────────────────────────
void ZaloService::refreshSessionKey()
{
    qDebug() << "[Zalo] refreshSessionKey: calling getLoginInfo with saved cookies";
    QVariantMap data;
    data["computer_name"] = QString("Web");
    data["imei"]          = m_imei;
    data["language"]      = m_language;
    data["ts"]            = QString::number(QDateTime::currentMSecsSinceEpoch());

    EncryptedParams ep = buildEncryptedParams(data);
    m_pendingEncryptKey = ep.encryptKey;

    QVariantMap paramsForSign;
    paramsForSign["zcid"]           = ep.zcid;
    paramsForSign["zcid_ext"]       = ep.zcid_ext;
    paramsForSign["enc_ver"]        = ep.enc_ver;
    paramsForSign["params"]         = ep.encryptedData;
    paramsForSign["type"]           = QString::number(API_TYPE);
    paramsForSign["client_version"] = QString::number(API_VERSION);
    paramsForSign["nretry"]         = QString("0");

    QVariantMap params = paramsForSign;
    params["signkey"] = buildSignKey("getlogininfo", paramsForSign);
    params["imei"]    = m_imei;

    QString urlStr = buildRawUrl("https://wpa.chat.zalo.me/api/login/getLoginInfo", params);
    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setRawHeader("zpw_ver",  QByteArray::number(API_VERSION));
    req.setRawHeader("zpw_type", QByteArray::number(API_TYPE));

    QNetworkReply *reply = m_manager->get(req);
    connect(reply, SIGNAL(finished()), this, SLOT(onRefreshSessionKeyDone()));
}

void ZaloService::onRefreshSessionKeyDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[Zalo] refreshSessionKey network error:" << reply->errorString()
                 << "- session may be invalid, triggering re-login";
        reply->deleteLater();
        // Do NOT fake loginSuccess here — cookies may be expired.
        // Fall through to step7 to attempt cookie renewal; if that also fails,
        // sessionExpired will be emitted and QR login sheet will open.
        m_isAutoRenew = true;
        step7_checkSession();
        return;
    }

    parseCookiesFromReply(reply);
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] refreshSessionKey response (first200):" << raw.left(200);

    // Parse outer để lấy error_code + encrypted data
    QScriptEngine outerEng;
    outerEng.globalObject().setProperty("__raw", QString::fromUtf8(raw));
    outerEng.evaluate("var __o=null;try{__o=JSON.parse(__raw);}catch(e){__o=null;}");
    QScriptValue outerObj = outerEng.globalObject().property("__o");

    int ec = outerObj.isObject() ? outerObj.property("error_code").toInt32() : -1;
    QString encData = outerObj.isObject() ? outerObj.property("data").toString() : QString();

    // Decrypt
    QString decrypted;
    if (!encData.isEmpty() && !m_pendingEncryptKey.isEmpty()) {
        decrypted = aesDecryptBase64_256(m_pendingEncryptKey, encData);
        qDebug() << "[Zalo] refreshSessionKey decrypted (first100):" << decrypted.left(100);
    }

    bool refreshOk = false;
    if (ec == 0 && !decrypted.isEmpty()) {
        QScriptEngine eng;
        eng.globalObject().setProperty("__dec", decrypted);
        eng.evaluate("var __info=null; var __innerEc=0;"
                     "try{"
                     "  var tmp=JSON.parse(__dec);"
                     "  __innerEc = (tmp.error_code !== undefined) ? tmp.error_code : 0;"
                     "  if(tmp&&tmp.data){__info=tmp.data;}else{__info=tmp;}"
                     "}catch(e){__info=null;}");
        QScriptValue info = eng.globalObject().property("__info");
        int innerEc = eng.globalObject().property("__innerEc").toInt32();

        if (innerEc != 0) {
            qDebug() << "[Zalo] refreshSessionKey: inner error_code=" << innerEc << "- session expired";
        } else if (info.isObject() && !info.isNull()) {
            QString newKey = info.property("zpw_enk").toString();
            if (!newKey.isEmpty()) {
                m_secretKey   = newKey;
                m_displayName = info.property("display_name").toString();
                qDebug() << "[Zalo] refreshSessionKey: new secretKey, first20:" << m_secretKey.left(20);
                refreshOk = true;

                // Update service URLs
                QScriptValue svcMap = info.property("zpw_service_map_v3");
                if (svcMap.isObject()) {
                    QScriptValue c = svcMap.property("chat");
                    QScriptValue g = svcMap.property("group");
                    QScriptValue p = svcMap.property("profile");
                    QScriptValue gp= svcMap.property("group_poll");
                    QScriptValue gb= svcMap.property("group_board");
                    QScriptValue f = svcMap.property("friend");
                    QScriptValue qm= svcMap.property("quick_message");
                    QScriptValue rc= svcMap.property("reaction");
                    if (c.isArray())  m_chatServiceUrl      = c.property(0).toString();
                    if (g.isArray())  m_groupServiceUrl     = g.property(0).toString();
                    if (p.isArray())  m_profileServiceUrl   = p.property(0).toString();
                    if (gp.isArray()) m_groupPollServiceUrl = gp.property(0).toString();
                    if (gb.isArray()) m_groupBoardServiceUrl = gb.property(0).toString();
                    if (f.isArray())  m_friendServiceUrl    = f.property(0).toString();
                    if (qm.isArray()) m_quickMessageServiceUrl = qm.property(0).toString();
                    if (rc.isArray()) m_reactionServiceUrl  = rc.property(0).toString();
                }

                // Update WS URLs
                QScriptValue wsArr = info.property("zpw_ws");
                if (wsArr.isArray()) {
                    m_zpwWsUrls.clear();
                    int len = wsArr.property("length").toInt32();
                    for (int i = 0; i < len && i < 10; ++i) {
                        QString wsUrl = wsArr.property(i).toString();
                        if (!wsUrl.isEmpty()) m_zpwWsUrls << wsUrl;
                    }
                    disconnectWebSocket();
                    connectWebSocket();
                }

                saveSession();
            }
        }
    }

    if (!refreshOk) {
        qDebug() << "[Zalo] refreshSessionKey: secretKey expired - tự động renew qua step7/step8";
        m_secretKey.clear();
        disconnectWebSocket();
        m_isAutoRenew = true;  // đánh dấu: step7/step8 này là auto-renew, KHÔNG hiện QR nếu thất bại
        step7_checkSession();
        return;
    }

    if (!m_loggedIn) {
        m_loggedIn = true;
        emit loggedInChanged();
        m_listenTimer->start(8000);
        m_keepAliveTimer->start(KEEPALIVE_INTERVAL_MS);
    }
    // Chỉ emit loginSuccess lần đầu; các lần refresh dùng sessionRefreshed
    if (!m_loginEmitted) {
        m_loginEmitted = true;
        emit loginSuccess(m_uid, m_displayName);
    } else {
        emit sessionRefreshed();
    }
}

// ─── Keep-Alive (HTTP session ping) ────────────────────────────────────────
// Gọi định kỳ endpoint "/keepalive" của chat-service để gia hạn session
// phía server (cookie zpsid/zpw_sek), tách biệt với WS-level ping (cmd=2/1)
// ở ZaloService_WebSocket.cpp — cái đó chỉ giữ socket khỏi timeout.
//
// Chạy mỗi KEEPALIVE_INTERVAL_MS (2 phút) khi app còn sống. Không đảm bảo
// cookie sống vĩnh viễn khi app bị đóng hẳn, nhưng ping liên tục giúp giữ
// session lâu hơn so với để TTL tự hết hạn.
void ZaloService::onKeepAliveTimer()
{
    sendKeepAlive();
}

void ZaloService::sendKeepAlive()
{
    if (!m_loggedIn || m_secretKey.isEmpty() || m_chatServiceUrl.isEmpty()) {
        qDebug() << "[Zalo] sendKeepAlive: skipped (not logged in or missing secretKey/chatUrl)";
        return;
    }

    QVariantMap innerParams;
    innerParams["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_chatServiceUrl + "/keepalive"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] sendKeepAlive GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onKeepAliveDone()));
}

void ZaloService::onKeepAliveDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // QUAN TRỌNG: gia hạn session thực chất nằm ở chỗ Zalo trả Set-Cookie
    // mới (hết hạn xa hơn) trong chính response /keepalive, không phải do
    // server tự nhớ theo uid/imei. Nếu không đọc + lưu lại Set-Cookie này,
    // request vẫn "thành công" (error_code=0) nhưng cookie cũ trong
    // QSettings vẫn y nguyên, hết hạn đúng giờ cũ.
    int cookiesBefore = m_cookies.size();
    parseCookiesFromReply(reply);
    int cookiesAfter = m_cookies.size();

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec == 0) {
        qDebug() << "[Zalo] keepAlive OK, cookies" << cookiesBefore << "->" << cookiesAfter;
        saveSession(); // Lưu cookie (đã gia hạn, nếu có) xuống QSettings NGAY —
                        // không chờ tới lần saveSession() kế tiếp, để app bị
                        // đóng/kill đột ngột ngay sau đó vẫn giữ được session mới nhất.
    } else {
        qDebug() << "[Zalo] keepAlive error_code=" << ec
                 << "msg=" << root["error_message"].toString();
        // Không tự emit sessionExpired ở đây: keepAlive lỗi có thể chỉ là lỗi
        // mạng/HTTP tạm thời. Việc phát hiện session chết "chuẩn" (ec=600) đã
        // được xử lý sẵn ở fetchConversations/fetchFriends — cứ để các API đó
        // làm nguồn sự thật, keepAlive chỉ là best-effort gia hạn.
    }
}

// and notifies QML so the already-displayed bubble can update in place instead
// of a stray "chat.undo" JSON blob appearing as its own message.
//
// The original text is preserved in recalledOriginalContent regardless of the
// current "Show Recalled Messages" setting, so toggling the setting later (or
// re-fetching the thread) can still recover it instead of it being gone forever.
//
// QUAN TRỌNG: phải idempotent. Server có thể gửi lại cùng event "chat.undo"
// (vd mở lại thread, resync từ checkpoint cũ) khiến cả tin gốc và recall
// đều replay lại. Lúc đó `content` có thể đã rỗng (đã recall) hoặc vừa được
// khôi phục tạm — nên chỉ copy `content` vào recalledOriginalContent lần
// đầu (khi nó còn rỗng), không đụng vào nếu đã có giá trị.

// ── In-app update downloader ──────────────────────────────────────────────
// Called from AboutSheet.qml after the user confirms they want to update.
// Downloads `url` to /accounts/1000/shared/downloads/<filename>, emitting
// updateDownloadProgress(0-100), then updateDownloadFinished(localPath) or
// updateDownloadFailed(errorMsg).

void ZaloService::downloadUpdate(const QString &url, const QString &filename)
{
    if (m_updateReply) {
        m_updateReply->abort();
        m_updateReply->deleteLater();
        m_updateReply = 0;
    }

    QString dest = QString("/accounts/1000/shared/downloads/") + filename;
    m_updateDestPath = dest;

    QNetworkRequest req = QNetworkRequest(QUrl(url));
    req.setRawHeader("User-Agent", m_userAgent.toUtf8());

    // File update host trên cùng CDN với version manifest, cần TLS1.2+.
    // BB10's OpenSSL/Qt4 mặc định fail handshake — cùng nguyên nhân với
    // manifest fetch của AboutSheet.qml, fix giống buildManifestRequest():
    // ép AnyProtocol để OpenSSL tự thương lượng bản cao nhất 2 bên hỗ trợ.
    QSslConfiguration sslConf = req.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConf.setProtocol(QSsl::AnyProtocol);
    req.setSslConfiguration(sslConf);

    m_updateReply = m_manager->get(req);

    connect(m_updateReply, SIGNAL(sslErrors(const QList<QSslError>&)),
            this, SLOT(onUpdateSslErrors(const QList<QSslError>&)));
    connect(m_updateReply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(onUpdateDownloadProgress(qint64,qint64)));
    connect(m_updateReply, SIGNAL(finished()),
            this, SLOT(onUpdateDownloadFinished()));
}

void ZaloService::cancelUpdateDownload()
{
    if (!m_updateReply) return;
    m_updateReply->abort(); // trigger finished() -> onUpdateDownloadFinished() với lỗi, emit updateDownloadFailed()
}

// Giống cách sniff extension của ApplicationUI's copy/share fix — đủ dùng
// cho các format Zalo hay gửi.
static QString photoExtensionFor(const QString &path)
{
    QString lower = path.toLower();
    if (lower.endsWith(".png"))  return ".png";
    if (lower.endsWith(".gif"))  return ".gif";
    if (lower.endsWith(".webp")) return ".webp";
    return ".jpg";
}

QString ZaloService::downloadPhotoToGallery(const QString &localImagePath, const QString &msgId)
{
    QString src = localImagePath;
    if (src.startsWith("file://")) src = src.mid(7);

    if (src.isEmpty() || !QFile::exists(src)) {
        qDebug() << "[Zalo] downloadPhotoToGallery: source missing" << src;
        return QString();
    }

    QString destDir = "/accounts/1000/shared/downloads/zalo10/photos";
    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qDebug() << "[Zalo] downloadPhotoToGallery: mkpath failed" << destDir;
        return QString();
    }

    // Đặt tên Zalo10_<msgId><ext> để lưu lại cùng ảnh sẽ ghi đè gọn gàng
    // thay vì chồng "(1)"/"(2)", và để truy ngược về tin nhắn gốc nếu cần.
    QString ext  = photoExtensionFor(src);
    QString name = msgId.isEmpty()
                 ? (QString("Zalo10_%1%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(ext))
                 : (QString("Zalo10_%1%2").arg(msgId).arg(ext));
    QString destPath = destDir + "/" + name;

    // QFile::copy() fail nếu đích đã tồn tại — xóa file cũ trước để
    // tải lại cùng ảnh chỉ đơn giản là ghi đè.
    if (QFile::exists(destPath))
        QFile::remove(destPath);

    if (!QFile::copy(src, destPath)) {
        qDebug() << "[Zalo] downloadPhotoToGallery: copy failed" << src << "->" << destPath;
        return QString();
    }

    qDebug() << "[Zalo] downloadPhotoToGallery: saved" << destPath;
    return destPath;
}

// ─── Download video/file message (msgType=3) ─────────────────────────────
// Tap bubble ("Tap to play") hoặc long-press "Download": tải file từ href
// CDN về "/accounts/1000/shared/downloads/zalo10/videos/<tên gốc>" — cùng
// thư mục chia sẻ với ảnh (downloadPhotoToGallery ở trên), thay vì /tmp
// trước đây (mất khi thoát app, không thấy trong File Manager/gallery).
// Giữ nguyên TÊN FILE GỐC (khác downloadPhotoToGallery đổi tên
// Zalo10_<msgId> cho ảnh) — video mở trong BB10 Media Player hiển thị tên
// file, đổi tên khiến người dùng không nhận ra video của mình
// ("na'vi s1mple.mp4" hiện thành "Zalo10_8131303682015.mp4"). Idempotent —
// nếu file cùng tên đã tồn tại (tải trước đó rồi) thì bắn
// videoDownloadFinished() luôn, không tải lại.
static QString videoExtensionFor(const QString &fileName)
{
    QString ext = fileName.section('.', -1).toLower();
    if (ext.isEmpty() || ext.length() > 4) ext = "mp4";
    return "." + ext;
}

// Bỏ ký tự không hợp lệ trên filesystem BB10 (QNX) khỏi tên file gốc —
// server không đảm bảo fileName sạch, và '/' đặc biệt nguy hiểm vì sẽ bị
// hiểu thành thư mục con.
static QString sanitizeFileName(const QString &name)
{
    QString s = name;
    s.replace(QRegExp("[/\\\\:*?\"<>|]"), "_");
    s = s.trimmed();
    return s;
}

// Thư mục tải về theo loại file, theo yêu cầu — video vẫn giữ nguyên
// "videos" (không nằm trong yêu cầu đổi), các loại còn lại route theo
// nhóm: documents / music / voice / misc. Phần mở rộng lạ (không khớp
// danh sách nào ở dưới, kể cả các loại tài liệu cũ như doc/docx/txt/pdf/
// xls/xlsx/ppt/pptx) rơi về "documents" — an toàn hơn "misc" vì đa số
// file người dùng gửi qua chat là tài liệu, và nhất quán với các định
// dạng tài liệu đã liệt kê rõ trong yêu cầu.
static QString destDirFor(const QString &fileName)
{
    QString ext = fileName.section('.', -1).toLower();
    static const char* kVideoExt[] = { "mp4", "mov", "3gp", "mkv", 0 };
    static const char* kMusicExt[] = { "mp3", "flac", 0 };
    static const char* kVoiceExt[] = { "m4a", 0 };
    static const char* kMiscExt[]  = { "apk", "cer", "zip", "rar", "7z", "vcf", "bar", 0 };
    for (int i = 0; kVideoExt[i]; ++i) if (ext == kVideoExt[i]) return "/accounts/1000/shared/downloads/zalo10/videos";
    for (int i = 0; kMusicExt[i]; ++i) if (ext == kMusicExt[i]) return "/accounts/1000/shared/downloads/zalo10/music";
    for (int i = 0; kVoiceExt[i]; ++i) if (ext == kVoiceExt[i]) return "/accounts/1000/shared/downloads/zalo10/voice";
    for (int i = 0; kMiscExt[i]; ++i)  if (ext == kMiscExt[i])  return "/accounts/1000/shared/downloads/zalo10/misc";
    return "/accounts/1000/shared/downloads/zalo10/documents";
}

void ZaloService::downloadVideoMessage(const QString &msgId, const QString &url, const QString &fileName)
{
    if (url.isEmpty() || msgId.isEmpty()) {
        emit videoDownloadFailed(msgId, "Missing URL");
        return;
    }

    QString destDir = destDirFor(fileName.isEmpty() ? url : fileName);
    QDir dir;
    if (!dir.exists(destDir) && !dir.mkpath(destDir)) {
        qDebug() << "[Zalo] downloadVideoMessage: mkpath failed" << destDir;
        emit videoDownloadFailed(msgId, "Cannot create " + destDir);
        return;
    }

    QString ext = videoExtensionFor(fileName.isEmpty() ? url : fileName);
    QString cleanName = sanitizeFileName(fileName);
    QString destPath;
    if (cleanName.isEmpty()) {
        // Không có tên gốc dùng được (rỗng, hoặc chỉ toàn ký tự bị lọc) —
        // dùng lại msgId để tránh file tên rỗng/trùng nhau.
        destPath = destDir + "/Zalo10_" + msgId + ext;
    } else {
        destPath = destDir + "/" + cleanName;
    }

    if (QFile::exists(destPath)) {
        qDebug() << "[Zalo] downloadVideoMessage: already cached" << destPath;
        emit videoDownloadFinished(msgId, destPath);
        return;
    }

    if (m_videoDownloadReply) {
        m_videoDownloadReply->abort();
        m_videoDownloadReply->deleteLater();
        m_videoDownloadReply = 0;
    }

    m_videoDownloadMsgId   = msgId;
    m_videoDownloadDestPath = destPath;

    QNetworkRequest req = buildRequest(url, "https://chat.zalo.me/");
    m_videoDownloadReply = m_manager->get(req);
    connect(m_videoDownloadReply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(onVideoDownloadProgress(qint64,qint64)));
    connect(m_videoDownloadReply, SIGNAL(finished()),
            this, SLOT(onVideoDownloadFinished()));

    qDebug() << "[Zalo] downloadVideoMessage: fetching" << url.left(100) << "-> " << destPath;
}

void ZaloService::onVideoDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) return;
    int pct = (int)((received * 100) / total);
    emit videoDownloadProgress(m_videoDownloadMsgId, pct);
}

void ZaloService::onVideoDownloadFinished()
{
    if (!m_videoDownloadReply) return;
    QString msgId   = m_videoDownloadMsgId;
    QString dest    = m_videoDownloadDestPath;

    if (m_videoDownloadReply->error() != QNetworkReply::NoError) {
        QString err = m_videoDownloadReply->errorString();
        m_videoDownloadReply->deleteLater();
        m_videoDownloadReply = 0;
        qDebug() << "[Zalo] downloadVideoMessage: failed" << err;
        emit videoDownloadFailed(msgId, err);
        return;
    }

    QByteArray data = m_videoDownloadReply->readAll();
    m_videoDownloadReply->deleteLater();
    m_videoDownloadReply = 0;

    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly)) {
        emit videoDownloadFailed(msgId, "Cannot write to " + dest);
        return;
    }
    f.write(data);
    f.close();

    qDebug() << "[Zalo] downloadVideoMessage: saved" << dest;
    emit videoDownloadProgress(msgId, 100);
    emit videoDownloadFinished(msgId, dest);
}

// sslErrors() chỉ bắn cho lỗi phát hiện SAU khi handshake xong (cert lạ/
// self-signed). SslHandshakeFailedError thường không tới đây, nhưng giữ
// lại phòng hờ — giống ApplicationUI::onManifestSslErrors().
void ZaloService::onUpdateSslErrors(const QList<QSslError> &errors)
{
    for (int i = 0; i < errors.size(); ++i)
        qDebug() << "[Zalo] update download SSL error:" << errors.at(i).errorString();
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) reply->ignoreSslErrors();
}

void ZaloService::onUpdateDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit updateDownloadProgress(0);
        return;
    }
    int pct = (int)((received * 100) / total);
    emit updateDownloadProgress(pct);
}

void ZaloService::onUpdateDownloadFinished()
{
    if (!m_updateReply) return;

    if (m_updateReply->error() != QNetworkReply::NoError) {
        QString err = m_updateReply->errorString();
        m_updateReply->deleteLater();
        m_updateReply = 0;
        emit updateDownloadFailed(err);
        return;
    }

    QByteArray data = m_updateReply->readAll();
    m_updateReply->deleteLater();
    m_updateReply = 0;

    QFile f(m_updateDestPath);
    if (!f.open(QIODevice::WriteOnly)) {
        emit updateDownloadFailed("Cannot write to " + m_updateDestPath);
        return;
    }
    f.write(data);
    f.close();

    emit updateDownloadProgress(100);
    emit updateDownloadFinished(m_updateDestPath);
}
