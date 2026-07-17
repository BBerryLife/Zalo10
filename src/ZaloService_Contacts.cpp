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
#include <QTimer>
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
#include <exception>

// Conversations, friends, group details/avatars, invites, and per-thread
// settings (mute, block, clear history, leave group).

void ZaloService::fetchConversations(){
    if (!m_loggedIn) return;
    if (m_isFetchingConversations) {
        qDebug() << "[Zalo] fetchConversations: already in progress, skipping duplicate call";
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchConvoTime > 0 && (now - m_lastFetchConvoTime) < FETCH_COOLDOWN_MS) {
        qDebug() << "[Zalo] fetchConversations: cooldown active, skipping ("
                 << (now - m_lastFetchConvoTime) << "ms since last fetch, need" << FETCH_COOLDOWN_MS << "ms)";
        return;
    }
    m_isFetchingConversations = true;

    QVariantMap qp;
    qp["zpw_ver"]  = QString::number(API_VERSION);
    qp["zpw_type"] = QString::number(API_TYPE);

    QString base = m_groupPollServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupPollServiceUrl;
    QString urlStr = buildRawUrl(base + "/api/group/getlg/v4", qp);
    qDebug() << "[Zalo] fetchConversations URL:" << urlStr;
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchConvoDone()));

}

void ZaloService::onFetchConvoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    // Cùng lý do với fetchFriends() — lỗi MẠNG (không phải lỗi ứng dụng
    // ec!=0) trước đây chỉ được log rồi rơi xuống nhánh raw.isEmpty() coi
    // như "0 nhóm" thành công, refresh trông như im lặng không làm gì. Log
    // mới cho thấy raw rỗng có thể xảy ra CẢ KHI reply->error()==NoError
    // (nghi tranh chấp SSL/socket lúc khởi động, cùng nguyên nhân với
    // fetchFriends — xem ghi chú chi tiết ở onFetchFriendsDone()). Coi cả
    // hai trường hợp là lỗi tạm thời, retry 1 lần trước khi chấp nhận thất bại.
    bool networkFailed = (reply->error() != QNetworkReply::NoError) || raw.isEmpty();
    if (networkFailed) {
        if (reply->error() != QNetworkReply::NoError)
            qDebug() << "[Zalo Error] fetchConversations Network Error:" << reply->errorString();
        else
            qDebug() << "[Zalo Error] fetchConversations: empty response body with no network error (transient?)";
        m_isFetchingConversations = false;
        if (m_fetchConvoNetRetryCount < 1) {
            m_fetchConvoNetRetryCount++;
            qDebug() << "[Zalo] fetchConversations: retrying once in 1.5s";
            QTimer::singleShot(1500, this, SLOT(onFetchConvoNetRetryTimer()));
        } else {
            qDebug() << "[Zalo] fetchConversations: failure persisted after retry, giving up for now";
            m_fetchConvoNetRetryCount = 0;
            emit conversationsReady(QVariantList());
        }
        return;
    }
    m_fetchConvoNetRetryCount = 0;

    qDebug() << "[Zalo] fetchConvo raw (first200):" << raw.left(200);

    // Xử lý HTTP 429 Too Many Requests — raw là HTML, không phải JSON
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] fetchConversations: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    QVariantMap root = jsonToMap(raw);
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo Error] fetchConvo error_code:" << ec << root["error_message"].toString();
        m_isFetchingConversations = false;
        // ec=600: zpw_sek expired. QUAN TRỌNG: đây KHÔNG đồng nghĩa với việc
        // cookie đăng nhập gốc (zpsid) đã hết hạn — server có thể tự xoay
        // vòng riêng zpw_sek trong khi cookie gốc vẫn còn dùng được (đã thấy
        // thực tế: refreshSessionKey() lúc HeadlessService khởi động dùng
        // đúng cookie này và thành công). Trước đây (1-process, không
        // headless) code này coi 600 = phải QR lại ngay — giả định đó không
        // còn đúng khi Headless giữ session sống lâu hơn nhiều, zpw_sek có
        // nhiều cơ hội hơn để bị server xoay vòng giữa chừng.
        // Thử refreshSessionKey() trước (dùng lại đúng flow auto-renew có
        // sẵn qua step7_checkSession — xem ZaloService_Network.cpp) — chỉ
        // khi chính flow đó thất bại thật sự thì sessionExpired() mới được
        // emit (bên trong step7_checkSession/step8, xem m_isAutoRenew).
        if (ec == 600) {
            qDebug() << "[Zalo] fetchConvo: got 600, attempting silent secretKey refresh before forcing QR re-login";
            m_pendingRetryFetchConvoOldKey = m_secretKey;
            refreshSessionKey();
            // Cùng lý do như sendMessage()'s ec==600 retry (xem
            // ZaloService_Messages.cpp): sessionRefreshed() không emit khi
            // refresh thất bại, nên poll m_secretKey sau 1 khoảng ngắn thay
            // vì connect trực tiếp vào signal đó.
            QTimer::singleShot(2000, this, SLOT(onRetryFetchConvoCheckTimer()));
            return;
        }
        emit conversationsReady(QVariantList());
        return;
    }
    m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch();

    // QUAN TRỌNG — trước đây đoạn decrypt/parse dưới đây KHÔNG có try/catch,
    // khác với fetchFriends() (đã có từ trước). Quan sát thực tế: 1 bad_alloc
    // ném ra ngay trong lúc đang xử lý payload getlg/v4 (bắt được ở tầng
    // notify() nhờ circuit-breaker mới thêm — không còn làm treo cả process
    // nữa) khiến hàm này thoát NGANG CHỪNG, trước khi chạy tới dòng
    // "m_isFetchingConversations = false" ở cuối — cờ kẹt ở true VĨNH VIỄN,
    // mọi lần bấm refresh nhóm sau đó đều bị chặn ở check "already in
    // progress" ngay từ đầu fetchConversations(), dù thực ra không còn request
    // nào đang chạy thật. Bọc try/catch ở đây, giống hệt cách fetchFriends()
    // đã làm, để bất kỳ exception nào trong lúc decrypt/parse cũng chắc chắn
    // reset được cờ trước khi thoát.
    QVariantList threads;
    try {
        QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
        qDebug() << "[Zalo] fetchConvo decrypted (first150):" << dec.left(150);

        QVariantMap outer = jsonToMap(dec);
        QVariantMap inner;
        if (outer.contains("data") && outer["data"].type() == QVariant::Map)
            inner = outer["data"].toMap();
        else
            inner = outer;

        QVariantMap gridVerMap = inner["gridVerMap"].toMap();
        qDebug() << "[Zalo] fetchConvo gridVerMap size:" << gridVerMap.size();

        QStringList groupIds = gridVerMap.keys();

        qDebug() << "[Zalo] fetchConvo found" << groupIds.size() << "groups, fetching details...";

        if (!groupIds.isEmpty()) {
            fetchGroupDetails(groupIds); // m_isFetchingConversations reset ở onGroupDetailsDone() (đã bọc try/catch riêng bên dưới)
        } else {
            m_isFetchingConversations = false;
            emit conversationsReady(QVariantList());
        }
    } catch (const std::exception &e) {
        qDebug() << "[Zalo Error] fetchConversations: EXCEPTION during parse/decrypt:" << e.what();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
    } catch (...) {
        qDebug() << "[Zalo Error] fetchConversations: UNKNOWN exception during parse/decrypt";
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
    }
}

void ZaloService::fetchGroupDetails(const QStringList &groupIds)
{
    if (groupIds.isEmpty()) return;

    QVariantMap gridVerMapObj;
    for (int i = 0; i < groupIds.size(); ++i)
        gridVerMapObj[groupIds[i]] = 0;
    QString gridVerMapStr = QString::fromUtf8(mapToJson(gridVerMapObj));
    qDebug() << "[Zalo] gridVerMap string (first100):" << gridVerMapStr.left(100);

    QVariantMap inner;
    inner["gridVerMap"] = gridVerMapStr;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(inner)));

    QString urlStr = m_groupServiceUrl + "/api/group/getmg-v2"
        + "?zpw_ver=" + QString::number(API_VERSION)
        + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    req.setRawHeader("Origin",  "https://chat.zalo.me");
    req.setRawHeader("Referer", "https://chat.zalo.me/");

    QByteArray body = "params=" + QUrl::toPercentEncoding(encParams);
    qDebug() << "[Zalo] fetchGroupDetails POST" << urlStr;
    qDebug() << "[Zalo] fetchGroupDetails body (first100):" << QString::fromUtf8(body.left(100));

    QNetworkReply *reply = m_manager->post(req, body);
    connect(reply, SIGNAL(finished()), this, SLOT(onGroupDetailsDone()));
}

void ZaloService::onGroupDetailsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();
    qDebug() << "[Zalo] groupDetails raw (first200):" << raw.left(200);

    // Xử lý HTTP 429 Too Many Requests
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] groupDetails: got 429, backing off";
        m_isFetchingConversations = false;
        m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        emit conversationsReady(QVariantList());
        return;
    }

    // QUAN TRỌNG — bọc toàn bộ phần parse/decrypt/build-threads dưới đây
    // trong try/catch: đây chính là chỗ ghi nhận thực tế từng ném bad_alloc
    // (payload getmg-v2 có thể khá lớn khi nhiều group cùng lúc). Nếu không
    // bọc, exception sẽ thoát ngang hàm này TRƯỚC dòng reset
    // "m_isFetchingConversations = false" ở cuối — cờ kẹt vĩnh viễn, mọi lần
    // fetchConversations() sau đó đều bị chặn ở check "already in progress"
    // dù không còn request nào thật sự đang chạy (đúng triệu chứng "refresh
    // nhóm lần 2 không được" đã quan sát).
    try {
        QVariantMap root = jsonToMap(raw);
        int ec = root["error_code"].toInt();
        if (ec != 0) {
            qDebug() << "[Zalo Error] groupDetails outer error:" << ec << root["error_message"].toString();
            m_isFetchingConversations = false;
            emit conversationsReady(QVariantList());
            return;
        }

        QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
        qDebug() << "[Zalo] groupDetails decrypted (first200):" << dec.left(200);

        QVariantMap outer = jsonToMap(dec);
        int ec2 = outer["error_code"].toInt();
        if (ec2 != 0) {
            qDebug() << "[Zalo Error] groupDetails inner error:" << ec2 << outer["error_message"].toString();
            m_isFetchingConversations = false;
            emit conversationsReady(QVariantList());
            return;
        }

        QVariantMap inner;
        if (outer.contains("data") && outer["data"].type() == QVariant::Map)
            inner = outer["data"].toMap();
        else
            inner = outer;

        qDebug() << "[Zalo] groupDetails inner keys:" << inner.keys();

        QVariantList threads;

        QVariantMap gridInfoMap = inner["gridInfoMap"].toMap();
        if (!gridInfoMap.isEmpty()) {
            QStringList keys = gridInfoMap.keys();
            for (int i = 0; i < keys.size(); ++i) {
                QVariantMap g = gridInfoMap[keys[i]].toMap();
                QVariantMap t;
                QString gname = g["name"].toString();
                if (gname.isEmpty()) gname = "Nhom " + g["groupId"].toString().right(6);
                QString gid = g["groupId"].toString();
                t["threadId"] = gid;
                t["name"]     = gname;
                t["isGroup"]  = true;
                t["avatar"]   = g["avt"].toString();
                t["unread"]   = 0;
                threads.append(t);
                m_groupNames[gid] = gname; // cache for notifications
            }
        } else {
            QVariantList grids = inner["gridInfos"].toList();
            for (int i = 0; i < grids.size(); ++i) {
                QVariantMap g = grids[i].toMap();
                QVariantMap t;
                QString gid = g["groupId"].toString();
                t["threadId"] = gid;
                t["name"]     = g["name"].toString();
                t["isGroup"]  = true;
                t["avatar"]   = g["avt"].toString();
                t["unread"]   = 0;
                threads.append(t);
                m_groupNames[gid] = g["name"].toString(); // cache
            }
        }

        qDebug() << "[Zalo] groupDetails found" << threads.size() << "groups with names";
        if (!threads.isEmpty())
            emit conversationsReady(threads);
        m_isFetchingConversations = false;
    } catch (const std::exception &e) {
        qDebug() << "[Zalo Error] groupDetails: EXCEPTION during parse/decrypt:" << e.what();
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
    } catch (...) {
        qDebug() << "[Zalo Error] groupDetails: UNKNOWN exception during parse/decrypt";
        m_isFetchingConversations = false;
        emit conversationsReady(QVariantList());
    }
}

void ZaloService::downloadAvatar(const QString &threadId, const QString &url)
{
    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;

    // Check cache theo cả full URL và base URL (fast path within this session)
    if (m_avatarCache.contains(url)) {
        emit avatarReady(threadId, m_avatarCache[url]);
        return;
    }
    if (m_avatarCache.contains(baseUrl)) {
        emit avatarReady(threadId, m_avatarCache[baseUrl]);
        return;
    }

    // Persistent dedup check: compare the new URL's hash against what we
    // already have on disk for this exact threadId (a stable per-person key —
    // background avatars are passed in as "bg_"+threadId, so they never collide
    // with the main avatar). This is what makes the cache survive app restarts,
    // logout/login, and toggling "Show Recalled Messages" (which never touches
    // images): if the person hasn't actually changed their picture, we reuse
    // the file already sitting in tmp instead of re-downloading it.
    QString newHash = md5Hex(baseUrl);
    QString storedHash, storedPath;
    if (avatarMetaLookup(threadId, storedHash, storedPath)) {
        QString fsPath = storedPath;
        if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
        if (storedHash == newHash && !fsPath.isEmpty() && QFile::exists(fsPath)) {
            // Same avatar URL as last time, and the cached file is still there
            // (i.e. the user hasn't run "Clear Cache") — reuse it, no network call.
            m_avatarCache[url] = storedPath;
            m_avatarCache[baseUrl] = storedPath;
            emit avatarReady(threadId, storedPath);
            return;
        }
        // Either the URL hash changed (genuinely a new profile picture) or the
        // file went missing — fall through and re-download below.
    }

    // Check cả full URL và base URL
    if (m_pendingAvatars.contains(url) || m_pendingAvatars.contains(baseUrl)) {
        QString pendingKey = m_pendingAvatars.contains(url) ? url : baseUrl;
        m_pendingAvatarWaiters[pendingKey].insert(threadId);
        return;
    }
    m_pendingAvatars.insert(baseUrl);
    m_pendingAvatarWaiters[baseUrl].clear();
    m_pendingAvatarWaiters[baseUrl].insert(threadId);

    if (m_activeAvatarDownloads >= MAX_CONCURRENT_AVATAR_DOWNLOADS) {
        m_avatarDownloadQueue.append(qMakePair(url, threadId));
        return;
    }
    startAvatarNetworkFetch(url, threadId);
}

void ZaloService::startNextQueuedAvatarDownload()
{
    // Có thể bị gọi "thừa" vô hại nếu queue đã trống lúc singleShot(0,...)
    // thực sự chạy (ví dụ nếu có nhiều singleShot xếp hàng cùng lúc) — chỉ
    // cần kiểm tra rỗng ở đây, không cần đồng bộ hoá gì thêm (single-threaded).
    if (m_avatarDownloadQueue.isEmpty()) return;
    if (m_activeAvatarDownloads >= MAX_CONCURRENT_AVATAR_DOWNLOADS) return;
    QPair<QString, QString> next = m_avatarDownloadQueue.takeFirst();
    startAvatarNetworkFetch(next.first, next.second);
}

void ZaloService::startAvatarNetworkFetch(const QString &url, const QString &threadId)
{
    ++m_activeAvatarDownloads;
    QString httpUrl = url;
    if (httpUrl.startsWith("https://"))
        httpUrl = "http://" + httpUrl.mid(8);

    QUrl avatarQUrl(httpUrl);
    QNetworkRequest avatarReq(avatarQUrl);
    avatarReq.setRawHeader("Referer",    "https://chat.zalo.me/");
    avatarReq.setRawHeader("User-Agent", m_userAgent.toUtf8());
    avatarReq.setRawHeader("Accept",     "image/webp,image/apng,image/*,*/*;q=0.8");
    // QUAN TRỌNG — ĐÃ REVERT "Connection: close" thêm ở lần sửa trước:
    // log đầy đủ sau đó cho thấy lỗi "Host X not found" (và bad_alloc hàng
    // loạt kèm theo) VẪN xảy ra, thậm chí lan sang cả sendMessage cá nhân —
    // và bằng chứng mới trỏ tới nguyên nhân NGƯỢC LẠI với suy đoán trước:
    // không phải do Qt GIỮ LẠI quá nhiều kết nối, mà do mỗi avatar (hàng
    // trăm cái, từ hàng trăm host CDN khác nhau, dồn dập trong ~30-90s) mở
    // MỘT KẾT NỐI TCP MỚI — và "Connection: close" ép server đóng kết nối
    // ngay sau mỗi request càng làm việc này tệ hơn: mỗi socket bị đóng rơi
    // vào trạng thái TIME_WAIT (hệ điều hành giữ lại 30-240s trước khi thực
    // sự giải phóng fd/port), dồn dập hàng trăm cái trong thời gian ngắn có
    // thể ăn hết số file-descriptor/ephemeral-port khả dụng của thiết bị —
    // khớp với "Host not found" (không mở nổi socket MỚI để phân giải DNS)
    // VÀ với bad_alloc lan rộng (network stack cạn buffer). Để Qt tự GIỮ và
    // TÁI SỬ DỤNG kết nối (hành vi mặc định, KHÔNG set Connection: close)
    // mới là hướng đúng — giảm tổng số kết nối TCP MỚI cần mở, do đó giảm
    // tích tụ TIME_WAIT. Xem thêm spacing giữa các lần tải trong
    // startNextQueuedAvatarDownload() để giảm tốc độ mở kết nối mới.
    QNetworkReply *reply = m_manager->get(avatarReq);
    reply->setProperty("avatarUrl",      url);
    reply->setProperty("avatarThreadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onAvatarDownloaded()));
}

void ZaloService::onAvatarDownloaded()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString url        = reply->property("avatarUrl").toString();
    QString threadId   = reply->property("avatarThreadId").toString();
    bool hasError      = (reply->error() != QNetworkReply::NoError);
    QByteArray data    = reply->readAll();
    reply->deleteLater();

    // 1 slot vừa trống — cho request kế tiếp trong hàng đợi (nếu có) bay ra,
    // giữ đúng luôn tối đa MAX_CONCURRENT_AVATAR_DOWNLOADS request bay cùng
    // lúc bất kể queue ban đầu dài bao nhiêu. Phải làm TRƯỚC nhánh lỗi return
    // sớm bên dưới, không thì 1 request lỗi sẽ làm nghẽn cả hàng đợi.
    //
    // QUAN TRỌNG: gọi qua QTimer::singleShot(...) — xem giải thích chi tiết ở
    // khai báo startNextQueuedAvatarDownload() trong ZaloService.hpp (phá vỡ
    // đệ quy đồng bộ). Delay 80ms (không phải 0ms) — thêm SAU KHI phát hiện
    // dồn dập mở kết nối TCP mới quá nhanh (hàng trăm avatar trong ~30-90s)
    // có thể làm tích tụ socket ở trạng thái TIME_WAIT, cạn tài nguyên hệ
    // thống (xem giải thích chi tiết ở startAvatarNetworkFetch()). Giãn nhịp
    // mở kết nối mới ra giúp hệ điều hành có thời gian giải phóng TIME_WAIT
    // giữa chừng, thay vì dồn cục hàng trăm cái liên tiếp gần như không nghỉ.
    // TUYỆT ĐỐI không gọi startAvatarNetworkFetch() trực tiếp/đồng bộ ở đây.
    --m_activeAvatarDownloads;
    if (!m_avatarDownloadQueue.isEmpty()) {
        QTimer::singleShot(80, this, SLOT(startNextQueuedAvatarDownload()));
    }

    QString baseUrl = url.contains('?') ? url.left(url.indexOf('?')) : url;
    QSet<QString> waiters = m_pendingAvatarWaiters.take(baseUrl);
    if (waiters.isEmpty()) waiters = m_pendingAvatarWaiters.take(url);
    m_pendingAvatars.remove(baseUrl);
    m_pendingAvatars.remove(url);

    if (hasError || data.isEmpty()) {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "[Zalo] avatar download failed for" << threadId
                 << "error:" << reply->errorString()
                 << "HTTP:" << httpStatus;
        return;
    }

    // Fixed filename per-person (md5 of threadId, NOT of the URL): this means
    // a changed profile picture overwrites the same file in place instead of
    // leaving the old image as an orphaned file in tmp every time the CDN
    // hands back a different URL for an unchanged picture.
    //
    // IMPORTANT: must live under QDir::homePath() (app data dir), NOT plain
    // "/tmp/". Since the headless split, avatars are downloaded by
    // Zalo10Headless (a separate process with its own "/tmp/" sandbox) but
    // displayed by the Zalo10 UI process (a different sandbox) — plain
    // "/tmp/" is NOT shared between the two on BB10/QNX, so the UI process
    // could never see files the headless process wrote there. homePath()
    // IS shared (it's the same dir the SQLite DB lives in, which both
    // processes already read/write successfully).
    QString fname = QDir::homePath() + "/tmp/avatar_" + md5Hex(threadId) + ".jpg";
    QDir().mkpath(QDir::homePath() + "/tmp");
    QFile f(fname);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    QString localPath = "file://" + fname;
    m_avatarCache[url] = localPath;
    int qmark = url.indexOf('?');
    if (qmark > 0) m_avatarCache[url.left(qmark)] = localPath;

    // Persist the threadId -> urlHash -> localPath mapping so next launch (or
    // next refresh after logout/login) can recognise this exact avatar again
    // without re-downloading it.
    avatarMetaUpsert(threadId, md5Hex(baseUrl), localPath);

    qDebug() << "[Zalo] avatar saved:" << threadId << "->" << fname;
    foreach (const QString &wid, waiters)
        emit avatarReady(wid, localPath);
    if (!waiters.contains(threadId))
        emit avatarReady(threadId, localPath);

    // Cập nhật pending friends list
    if (!m_pendingFriends.isEmpty() && m_pendingFriendAvatarCount > 0) {
        QSet<QString> allWaiters = waiters;
        allWaiters.insert(threadId);
        bool updated = false;
        for (int i = 0; i < m_pendingFriends.size(); ++i) {
            QVariantMap t = m_pendingFriends[i].toMap();
            QString tid = t["threadId"].toString();
            if (allWaiters.contains(tid) && t["localAvatar"].toString().isEmpty()) {
                t["localAvatar"] = localPath;
                m_pendingFriends[i] = t;
                m_loadedFriendAvatarCount++;
                updated = true;
            }
        }
        Q_UNUSED(updated);
        // Re-emit khi đủ avatar
        if (m_loadedFriendAvatarCount >= m_pendingFriendAvatarCount) {
            qDebug() << "[Zalo] all friend avatars loaded, re-emitting friendsReady";
            QVariantList finalList = m_pendingFriends;
            m_pendingFriends.clear();
            m_pendingFriendAvatarCount = 0;
            m_loadedFriendAvatarCount  = 0;
            emit friendsReady(finalList);
        }
    }
}

void ZaloService::fetchFriends()
{
    if (!m_loggedIn) return;
    if (m_isFetchingFriends) {
        qDebug() << "[Zalo] fetchFriends: already in progress, skipping duplicate call";
        return;
    }
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastFetchFriendsTime > 0 && (now - m_lastFetchFriendsTime) < FETCH_COOLDOWN_MS) {
        qDebug() << "[Zalo] fetchFriends: cooldown active, skipping ("
                 << (now - m_lastFetchFriendsTime) << "ms since last fetch, need" << FETCH_COOLDOWN_MS << "ms)";
        return;
    }
    m_isFetchingFriends = true;

    // zca-js dùng GET, params trong query string (không phải POST body)
    QVariantMap innerParams;
    innerParams["incInvalid"]  = 1;
    innerParams["page"]        = 1;
    // count=20000 trước đây yêu cầu server trả TOÀN BỘ friend list trong 1
    // lần gọi — với các tài khoản có nhiều bạn bè/dữ liệu profile đính kèm
    // (bio, ảnh nền...), phản hồi JSON sau decrypt có thể lên tới hàng trăm
    // KB. Nghi vấn: đây là nguồn gốc "bad allocation" quan sát được trong
    // parse/decrypt (dù đã có sanity-cap 20MB ở aesDecryptBase64 — không có
    // dòng log "vuot nguong" nào xuất hiện, nên payload chưa chạm ngưỡng đó,
    // nhưng vẫn đủ lớn để gây áp lực cấp phát bộ nhớ trên thiết bị thật).
    // 2000 vẫn dư thừa so với số bạn bè thực tế của hầu hết tài khoản
    // (~87 trong log thực tế) trong khi giảm đáng kể kích thước response.
    innerParams["count"]       = 2000;
    innerParams["avatar_size"] = 120;
    innerParams["actiontime"]  = 0;
    innerParams["imei"]        = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = m_profileServiceUrl + "/api/social/friend/getfriends"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchFriends GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchFriendsDone()));
}

void ZaloService::onFetchFriendsDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchFriends raw size:" << raw.size() << "bytes, first300:" << raw.left(300);

    // QUAN TRỌNG — trước đây chỉ coi lỗi MẠNG (reply->error() != NoError, vd
    // "Host not found") là đáng retry. Log mới quan sát được 1 kiểu lỗi
    // KHÁC: raw.size() == 0 NHƯNG reply->error() == NoError (không log được
    // "Network Error" nào cả) — tức Qt/BB10 coi request "thành công" nhưng
    // thân response trống rỗng thật sự. Xảy ra ngay trong cửa sổ khởi động
    // rất hẹp (fetchFriends bắn đi lúc WS handshake CÙNG LÚC đang hoàn tất
    // HTTP Upgrade) — nghi ngờ tranh chấp tài nguyên SSL/socket dùng chung
    // trên BB10 khi có nhiều kết nối HTTPS/WSS cùng lúc lúc mới khởi động.
    // Trước đây trường hợp này lọt qua nhánh network-error, rơi thẳng vào
    // parse "" như 1 response hợp lệ rỗng → "0 bạn bè" bị coi là thành công,
    // friendsReady() không emit, refresh coi như không làm gì. Giờ coi CẢ
    // network-error LẪN raw rỗng bất thường đều là lỗi tạm thời, retry đúng
    // 1 lần như nhau.
    bool networkFailed = (reply->error() != QNetworkReply::NoError) || raw.isEmpty();
    if (networkFailed) {
        if (reply->error() != QNetworkReply::NoError)
            qDebug() << "[Zalo Error] fetchFriends Network Error:" << reply->errorString();
        else
            qDebug() << "[Zalo Error] fetchFriends: empty response body with no network error (transient?)";
        m_isFetchingFriends = false;
        if (m_fetchFriendsNetRetryCount < 1) {
            m_fetchFriendsNetRetryCount++;
            qDebug() << "[Zalo] fetchFriends: retrying once in 1.5s";
            QTimer::singleShot(1500, this, SLOT(onFetchFriendsNetRetryTimer()));
        } else {
            qDebug() << "[Zalo] fetchFriends: failure persisted after retry, giving up for now";
            m_fetchFriendsNetRetryCount = 0;
        }
        return;
    }
    m_fetchFriendsNetRetryCount = 0;

    // Xử lý HTTP 429 Too Many Requests — raw là HTML, không phải JSON
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] fetchFriends: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingFriends = false;
        // Đặt cooldown dài hơn bình thường khi bị rate-limit
        m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        return;
    }

    QVariantList threads;
    try {
        QVariantMap root = jsonToMap(raw);
        int ec = root["error_code"].toInt();
        if (ec != 0) {
            qDebug() << "[Zalo Error] fetchFriends:" << root["error_message"].toString();
            m_isFetchingFriends = false;
            // Cùng lý do với fetchConversations()'s ec==600 handling ở trên —
            // zpw_sek có thể bị server xoay vòng độc lập với cookie đăng nhập
            // gốc, thử refresh trước khi bỏ cuộc.
            if (ec == 600) {
                qDebug() << "[Zalo] fetchFriends: got 600, attempting silent secretKey refresh";
                m_pendingRetryFetchFriendsOldKey = m_secretKey;
                refreshSessionKey();
                QTimer::singleShot(2000, this, SLOT(onRetryFetchFriendsCheckTimer()));
            }
            return;
        }

        QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
        qDebug() << "[Zalo] fetchFriends decrypted (first300):" << dec.left(300);

        QVariantList friends;
        QVariantMap outer = jsonToMap(dec);
        if (outer.contains("data") && outer["data"].type() == QVariant::List)
            friends = outer["data"].toList();
        else if (outer.contains("friends") && outer["friends"].type() == QVariant::List)
            friends = outer["friends"].toList();
        else {
            QVariantList arr = jsonToList(dec);
            if (!arr.isEmpty())
                friends = arr;
        }

        qDebug() << "[Zalo] fetchFriends found" << friends.size() << "friends";

        for (int i = 0; i < friends.size(); ++i) {
            QVariantMap f = friends[i].toMap();
            QString uid  = f["userId"].toString();
            if (uid.isEmpty()) uid = f["uid"].toString();
            QString name = f["zaloName"].toString();
            if (name.isEmpty()) name = f["displayName"].toString();
            if (name.isEmpty()) name = f["username"].toString();
            QString avatarUrl   = f["avatar"].toString();
            QString bgAvatarUrl = f["bgavatar"].toString();
            QString avatarBase   = avatarUrl.contains('?')   ? avatarUrl.left(avatarUrl.indexOf('?'))   : avatarUrl;
            QString bgAvatarBase = bgAvatarUrl.contains('?') ? bgAvatarUrl.left(bgAvatarUrl.indexOf('?')) : bgAvatarUrl;
            QString localAvatar   = m_avatarCache.value(avatarBase,   m_avatarCache.value(avatarUrl,   ""));
            QString localBgAvatar = m_avatarCache.value(bgAvatarBase, m_avatarCache.value(bgAvatarUrl, ""));

            QVariantMap t;
            t["threadId"]      = uid;
            t["name"]          = name;
            t["isGroup"]       = false;
            t["avatar"]        = avatarUrl;
            t["bgavatar"]      = bgAvatarUrl;
            t["localAvatar"]   = localAvatar;
            t["localBgAvatar"] = localBgAvatar;
            t["unread"]        = 0;
            t["lastMessage"]   = "";
            if (!uid.isEmpty() && !name.isEmpty())
                threads.append(t);
        }
    } catch (const std::exception &e) {
        qDebug() << "[Zalo Error] fetchFriends: EXCEPTION during parse/decrypt:" << e.what();
        m_isFetchingFriends = false;
        emit friendsReady(QVariantList());
        return;
    } catch (...) {
        qDebug() << "[Zalo Error] fetchFriends: UNKNOWN exception during parse/decrypt";
        m_isFetchingFriends = false;
        emit friendsReady(QVariantList());
        return;
    }

    qDebug() << "[Zalo] fetchFriends parsed" << threads.size() << "valid friends";
    m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch();

    if (!threads.isEmpty()) {
        // IMPORTANT: compute needDownload and set m_pendingFriends/
        // m_pendingFriendAvatarCount BEFORE emit friendsReady() below, not
        // after. emit is synchronous here — QML's onFriendsReady handler
        // runs to completion (including up to ~87 back-to-back
        // zService.downloadAvatar() calls) on top of this very call stack
        // before control ever returns to this function. Previously the
        // bookkeeping below ran AFTER the emit, so for the entire duration
        // of that reentrant QML loop, m_pendingFriends was still the OLD
        // (possibly stale/empty) value — a real ordering hazard on top of
        // heavy QSet churn in downloadAvatar()/onAvatarDownloaded(). This
        // crashed with SIGSEGV inside Qt's own QHash internals
        // (QHash<QString,QHashDummyValue>::duplicateNode) on a fresh-login
        // run where the avatar cache was empty and all 87 friends hit that
        // pending-avatar bookkeeping path back-to-back in one synchronous
        // burst — the exact conditions this reordering removes.
        int needDownload = 0;
        for (int i = 0; i < threads.size(); ++i) {
            QVariantMap t = threads[i].toMap();
            if (t["localAvatar"].toString().isEmpty() && !t["avatar"].toString().isEmpty())
                needDownload++;
        }
        qDebug() << "[Zalo] friends: need to download" << needDownload << "avatars";
        if (needDownload > 0) {
            m_pendingFriends = threads;
            m_pendingFriendAvatarCount = needDownload;
            m_loadedFriendAvatarCount  = 0;
        }
        emit friendsReady(threads);
    }
    m_isFetchingFriends = false;
}

void ZaloService::onRetryFetchConvoCheckTimer()
{
    if (m_secretKey.isEmpty() || m_secretKey == m_pendingRetryFetchConvoOldKey) {
        // Refresh thất bại hẳn hoặc không đổi gì — session thật sự có vấn đề
        // (step7/step8 auto-renew bên trong refreshSessionKey() đã xử lý,
        // bao gồm cả emit sessionExpired() nếu cần). Không retry nữa, để UI
        // ở trạng thái "chưa fetch được" như trước đây thay vì lặp vô hạn.
        qDebug() << "[Zalo] fetchConversations retry-after-600: secretKey refresh did not help, giving up";
        return;
    }
    qDebug() << "[Zalo] fetchConversations retry-after-600: secretKey refreshed, retrying fetch";
    fetchConversations();
}

void ZaloService::onRetryFetchFriendsCheckTimer()
{
    if (m_secretKey.isEmpty() || m_secretKey == m_pendingRetryFetchFriendsOldKey) {
        qDebug() << "[Zalo] fetchFriends retry-after-600: secretKey refresh did not help, giving up";
        return;
    }
    qDebug() << "[Zalo] fetchFriends retry-after-600: secretKey refreshed, retrying fetch";
    fetchFriends();
}

// Retry sau lỗi MẠNG (Host not found/timeout/...) — khác hẳn nhánh ec==600 ở
// trên (đó là lỗi ứng dụng, cần refresh secretKey trước). Ở đây chỉ đơn giản
// gọi lại fetchFriends(): m_isFetchingFriends đã được reset về false ngay
// trước khi hẹn giờ này, và m_lastFetchFriendsTime không đổi trong lần thất
// bại vừa rồi nên không bị cooldown 10s chặn.
void ZaloService::onFetchFriendsNetRetryTimer()
{
    fetchFriends();
}

void ZaloService::onFetchConvoNetRetryTimer()
{
    fetchConversations();
}

// Pulls the user's own quick-message list from their real Zalo account
// (api/quickmessage/list — same GET + AES-encrypted-query-param shape as
// fetchFriends() above) and merges it into the local quick_messages table.
// "keyword" -> quick message name, "message.title" -> content. Matched by
// name (case-insensitive) against what's already saved locally, same
// "existing wins" rule as importData() in ZaloService_Db.cpp.
void ZaloService::fetchServerQuickMessages()
{
    if (!m_loggedIn) {
        emit serverQuickMessagesReady(0, 0, "Not logged in");
        return;
    }

    QString svcUrl = m_quickMessageServiceUrl;
    if (svcUrl.isEmpty()) svcUrl = m_profileServiceUrl; // fallback, observed same host in practice
    if (svcUrl.isEmpty()) {
        emit serverQuickMessagesReady(0, 0, "Service URL unavailable — try logging in again");
        return;
    }

    QVariantMap innerParams;
    innerParams["version"] = 0;
    innerParams["lang"]    = 0;
    innerParams["imei"]    = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = svcUrl + "/api/quickmessage/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchServerQuickMessages GET" << urlStr.left(100);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchServerQuickMessagesDone()));
}

void ZaloService::onFetchServerQuickMessagesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchServerQuickMessages raw (first300):" << raw.left(300);

    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        emit serverQuickMessagesReady(0, 0, "Server is busy (429) — try again in a bit");
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        QString em = root["error_message"].toString();
        qDebug() << "[Zalo Error] fetchServerQuickMessages:" << em;
        emit serverQuickMessagesReady(0, 0, em.isEmpty() ? "Request failed" : em);
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchServerQuickMessages decrypted (first300):" << dec.left(300);

    // Same double-wrap shape as every other Zalo endpoint here (fetchFriends,
    // fetchInvites, ...): the decrypted blob is itself {error_code, error_message,
    // data:{...}}, and for quickmessage/list the real payload — {cursor, version,
    // items} — lives under that inner "data" key, not at the top level.
    QVariantMap outer = jsonToMap(dec);
    QVariantMap payload = outer.value("data").toMap();
    QVariantList items = payload.value("items").toList();
    if (items.isEmpty() && outer.contains("items"))
        items = outer.value("items").toList(); // fallback in case the shape ever changes
    qDebug() << "[Zalo] fetchServerQuickMessages found" << items.size() << "items";

    // Existing local names, same dedup approach as importData().
    QSet<QString> existingNames;
    QVariantList localQm = getQuickMessages();
    for (int i = 0; i < localQm.size(); ++i)
        existingNames.insert(localQm[i].toMap().value("name").toString().toLower());

    int imported = 0, skipped = 0;
    for (int i = 0; i < items.size(); ++i) {
        QVariantMap it = items[i].toMap();
        QString name = it.value("keyword").toString().trimmed();
        QString content = it.value("message").toMap().value("title").toString().trimmed();
        if (name.isEmpty() || content.isEmpty()) { skipped++; continue; }
        if (existingNames.contains(name.toLower())) { skipped++; continue; }

        int newId = addQuickMessage(name, content);
        if (newId >= 0) { imported++; existingNames.insert(name.toLower()); }
        else skipped++;
    }

    qDebug() << "[Zalo] fetchServerQuickMessages:" << imported << "imported," << skipped << "skipped";
    emit serverQuickMessagesReady(imported, skipped, "");
}

void ZaloService::fetchInvites()
{
    if (!m_loggedIn) return;

    // zca-js: /api/friend/recommendsv2/list — recommItemType=2 = ReceivedFriendRequest
    // (per getFriendRecommendations.js in zca-js source)
    QString base = m_friendServiceUrl.isEmpty() ? m_profileServiceUrl : m_friendServiceUrl;
    QVariantMap innerParams;
    innerParams["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/friend/recommendsv2/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchInvites (recommendsv2/list) GET" << urlStr.left(120);
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchInvitesDone()));
}

void ZaloService::onFetchInvitesDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QByteArray raw = reply->readAll();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchInvites raw (first300):" << raw.left(300);
    QVariantMap root = jsonToMap(raw);

    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo] fetchInvites error_code:" << ec;
        emit invitesReady(QVariantList());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchInvites decrypted FULL:" << dec.left(800);
    QVariantMap outer = jsonToMap(dec);
    qDebug() << "[Zalo] fetchInvites outer keys:" << outer.keys();

    // recommendsv2/list response (per zca-js getFriendRecommendations.d.ts):
    // { recommItems: [{ recommItemType: 1=PYMK, 2=ReceivedFriendRequest,
    //                   dataInfo: { userId, zaloName, displayName, avatar,
    //                               recommInfo: { message } } }] }
    QVariantList recommItems = outer["recommItems"].toList();
    if (recommItems.isEmpty() && outer.contains("data")) {
        QVariant dataV = outer["data"];
        if (dataV.type() == QVariant::Map)
            recommItems = dataV.toMap()["recommItems"].toList();
        else if (dataV.type() == QVariant::List)
            recommItems = dataV.toList();
    }
    qDebug() << "[Zalo] fetchInvites recommItems count:" << recommItems.size();

    QVariantList invites;
    for (int i = 0; i < recommItems.size(); ++i) {
        QVariantMap item = recommItems[i].toMap();
        QVariantMap info = item["dataInfo"].toMap();

        // zca-js types this two different ways and it's easy to grab the wrong
        // one: the outer "recommItemType" sibling of dataInfo is just `number`
        // (untyped), while the field actually typed as FriendRecommendationsType
        // (1=RecommendedFriend/PYMK, 2=ReceivedFriendRequest) is dataInfo.recommType.
        // Filtering on the outer field was the bug — it doesn't reliably distinguish
        // PYMK from real pending requests; dataInfo.recommType does.
        int itemType = info["recommType"].toInt();

        if (i < 5)
            qDebug() << "[Zalo] fetchInvites item[" << i << "] outer.recommItemType=" << item["recommItemType"].toInt()
                     << "dataInfo.recommType=" << itemType
                     << "dataInfo keys=" << info.keys();

        if (itemType != 2) continue;

        if (info.isEmpty()) continue;

        // userId is the correct field per zca-js type definition
        QString uid = info["userId"].toString();
        if (uid.isEmpty()) uid = info["uid"].toString();
        if (uid.isEmpty()) uid = info["fid"].toString();
        if (uid.isEmpty()) continue;

        // zaloName or displayName
        QString name = info["zaloName"].toString();
        if (name.isEmpty()) name = info["displayName"].toString();
        if (name.isEmpty()) name = info["fullName"].toString();

        // avatar field
        QString avatarUrl = info["avatar"].toString();

        // message from recommInfo.message per type definition
        QString msg;
        QVariant recommInfoV = info["recommInfo"];
        if (recommInfoV.type() == QVariant::Map) {
            QVariantMap recommInfo = recommInfoV.toMap();
            msg = recommInfo["message"].toString();
            if (msg.isEmpty()) msg = recommInfo["customText"].toString();
        }
        // Fallback fields — reject anything that looks like a serialized JSON object
        if (msg.isEmpty()) msg = info["msg"].toString();
        if (msg.trimmed().startsWith("{") || msg.trimmed().startsWith("["))
            msg = QString();

        QVariantMap inv;
        inv["uid"]    = uid;
        inv["name"]   = name.isEmpty() ? uid : name;
        inv["avatar"] = avatarUrl;
        inv["msg"]    = msg.isEmpty() ? "Wants to be your friend" : msg;
        invites.append(inv);
    }
    qDebug() << "[Zalo] fetchInvites found" << invites.size() << "pending requests";
    emit invitesReady(invites);
}

// ─── acceptFriendRequest ──────────────────────────────────────────────────
// zca-js: POST /api/friend/accept  body=params=AES({fid, language})
void ZaloService::acceptFriendRequest(const QString &friendId)
{
    if (!m_loggedIn || friendId.isEmpty()) return;

    QVariantMap params;
    params["fid"]      = friendId;
    params["language"] = m_language;
    QString encParams  = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/accept"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] acceptFriendRequest fid=" << friendId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", true);
    connect(reply, SIGNAL(finished()), this, SLOT(onAcceptFriendDone()));
}

// ─── rejectFriendRequest ──────────────────────────────────────────────────
// zca-js: POST /api/friend/reject  body=params=AES({fid})
void ZaloService::rejectFriendRequest(const QString &friendId)
{
    if (!m_loggedIn || friendId.isEmpty()) return;

    QVariantMap params;
    params["fid"] = friendId;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/reject"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] rejectFriendRequest fid=" << friendId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("friendId", friendId);
    reply->setProperty("accepted", false);
    connect(reply, SIGNAL(finished()), this, SLOT(onRejectFriendDone()));
}

void ZaloService::onAcceptFriendDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString fid = reply->property("friendId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] acceptFriendRequest response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, true, ok);
}

void ZaloService::onRejectFriendDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString fid = reply->property("friendId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] rejectFriendRequest response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit friendRequestResponded(fid, false, ok);
}

// ─── blockUser ────────────────────────────────────────────────────────────
// zca-js: POST friend[0]/api/friend/block  body=params=AES({fid, imei})
void ZaloService::blockUser(const QString &userId)
{
    if (!m_loggedIn || userId.isEmpty()) return;

    QVariantMap params;
    params["fid"]  = userId;
    params["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/block"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] blockUser uid=" << userId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("userId", userId);
    connect(reply, SIGNAL(finished()), this, SLOT(onBlockUserDone()));
}

void ZaloService::onBlockUserDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid = reply->property("userId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] blockUser response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.insert(uid);
    emit blockUserDone(uid, ok);
}

void ZaloService::unblockUser(const QString &userId)
{
    if (!m_loggedIn || userId.isEmpty()) return;

    QVariantMap params;
    params["fid"]  = userId;
    params["imei"] = m_imei;
    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_friendServiceUrl + "/api/friend/unblock"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] unblockUser uid=" << userId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("userId", userId);
    connect(reply, SIGNAL(finished()), this, SLOT(onUnblockUserDone()));
}

void ZaloService::onUnblockUserDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString uid = reply->property("userId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] unblockUser response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) m_blockedUsers.remove(uid);
    emit unblockUserDone(uid, ok);
}

// ─── setMute ──────────────────────────────────────────────────────────────
// zca-js: POST profile[0]/api/social/profile/setmute
//   body=params=AES({toid, duration, action, startTime, muteType, imei})
//   muteType: 1=DM, 2=Group  action: 1=mute, 3=unmute  duration: -1=forever
void ZaloService::setMute(const QString &threadId, bool isGroup, bool mute)
{
    if (!m_loggedIn || threadId.isEmpty()) return;

    QVariantMap params;
    params["toid"]      = threadId;
    params["duration"]  = -1;
    params["action"]    = mute ? 1 : 3;
    params["startTime"] = static_cast<qint64>(QDateTime::currentMSecsSinceEpoch() / 1000);
    params["muteType"]  = isGroup ? 2 : 1;
    params["imei"]      = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_profileServiceUrl + "/api/social/profile/setmute"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] setMute toid=" << threadId << "mute=" << mute;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    reply->setProperty("muting", mute);
    connect(reply, SIGNAL(finished()), this, SLOT(onSetMuteDone()));
}

void ZaloService::onSetMuteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid  = reply->property("threadId").toString();
    bool muting  = reply->property("muting").toBool();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] setMute response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok) {
        if (muting) m_mutedThreads.insert(tid);
        else        m_mutedThreads.remove(tid);
        saveSession();
    }
    emit muteDone(tid, muting, ok);
}

// ─── clearHistory ─────────────────────────────────────────────────────────
// zca-js deleteChat.ts: POST chat[0]/api/message/deleteconver (DM)
//                    or group[0]/api/group/deleteconver (Group)
//   body=params=AES({toid/grid, cliMsgId, conver:{ownerId,cliMsgId,globalMsgId}, onlyMe:1, imei})
// We use empty conver (no last-message info) which clears from beginning.
void ZaloService::clearHistory(const QString &threadId, bool isGroup)
{
    if (!m_loggedIn || threadId.isEmpty()) return;

    QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());

    QVariantMap conver;
    conver["ownerId"]     = "";
    conver["cliMsgId"]    = "0";
    conver["globalMsgId"] = "0";

    QVariantMap params;
    if (isGroup) {
        params["grid"] = threadId;
    } else {
        params["toid"] = threadId;
    }
    params["cliMsgId"] = ts;
    params["conver"]   = conver;
    params["onlyMe"]   = 1;
    params["imei"]     = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString endpoint  = isGroup ? "/api/group/deleteconver" : "/api/message/deleteconver";
    QString baseUrl   = isGroup ? m_groupServiceUrl : m_chatServiceUrl;
    QString urlStr    = baseUrl + endpoint
                      + "?zpw_ver=" + QString::number(API_VERSION)
                      + "&zpw_type=" + QString::number(API_TYPE)
                      + "&nretry=0";

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] clearHistory toid=" << threadId << "isGroup=" << isGroup;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("threadId", threadId);
    connect(reply, SIGNAL(finished()), this, SLOT(onClearHistoryDone()));
}

void ZaloService::onClearHistoryDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString tid = reply->property("threadId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] clearHistory response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    if (ok && m_db && !tid.isEmpty()) {
        // Delete local messages
        const char *sqlDel = "DELETE FROM messages WHERE threadId=?";
        sqlite3_stmt *stmt = 0;
        if (sqlite3_prepare_v2(m_db, sqlDel, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        // Record clear timestamp so future server re-fetches are ignored
        QString clearedAt = QString::number(QDateTime::currentMSecsSinceEpoch());
        const char *sqlClear =
            "INSERT OR REPLACE INTO cleared_threads (threadId, clearedAt) VALUES (?,?)";
        if (sqlite3_prepare_v2(m_db, sqlClear, -1, &stmt, 0) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tid.toUtf8().constData(),       -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, clearedAt.toUtf8().constData(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        qDebug() << "[Zalo] clearHistory deleted local DB for thread:" << tid;
    }
    emit clearHistoryDone(tid, ok);
}

// ─── leaveGroup ───────────────────────────────────────────────────────────
// zca-js: POST group[0]/api/group/leave  body=params=AES({grids:[groupId], imei, silent:0, language})
void ZaloService::leaveGroup(const QString &groupId)
{
    if (!m_loggedIn || groupId.isEmpty()) return;

    QVariantList grids;
    grids.append(groupId);

    QVariantMap params;
    params["grids"]    = grids;
    params["imei"]     = m_imei;
    params["silent"]   = 0;
    params["language"] = m_language;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body   = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/group/leave"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] leaveGroup grid=" << groupId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onLeaveGroupDone()));
}

void ZaloService::onLeaveGroupDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString gid = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    qDebug() << "[Zalo] leaveGroup response:" << raw.left(200);
    if (ok) {
        QVariantMap outer = jsonToMap(raw);
        ok = (outer["error_code"].toInt() == 0);
    }
    emit leaveGroupDone(gid, ok);
}

