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

    QString urlStr = buildRawUrl(m_groupServiceUrl + "/api/group/getlg/v4", qp);
    qDebug() << "[Zalo] fetchConversations URL:" << urlStr;
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchConvoDone()));

}

void ZaloService::onFetchConvoDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << "[Zalo Error] fetchConversations Network Error:" << reply->errorString();
    }

    QByteArray raw = reply->readAll();
    reply->deleteLater();

    if (raw.isEmpty()) {
        emit conversationsReady(QVariantList());
        m_isFetchingConversations = false;
        return;
    }

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
        // ec=600: zpw_sek expired — session cookies invalid, must re-login
        if (ec == 600) {
            qDebug() << "[Zalo] fetchConvo: session expired (600), emitting sessionExpired";
            m_loggedIn = false;
            emit loggedInChanged();
            emit sessionExpired();
            return;
        }
        emit conversationsReady(QVariantList());
        return;
    }
    m_lastFetchConvoTime = QDateTime::currentMSecsSinceEpoch();

    QVariantList threads;
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchConvo decrypted (first150):" << dec.left(150);

    QVariantMap outer = jsonToMap(dec.toUtf8());
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
        fetchGroupDetails(groupIds);
    } else {
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

    QVariantMap outer = jsonToMap(dec.toUtf8());
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

            // currentMems: mỗi member {id, dName, zaloName, ...} — nguồn
            // đáng tin cậy cho uid->name (m_memberNames), khác field dName
            // trên wire mỗi tin nhắn (không đáng tin với tin nhắn đến).
            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
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

            QVariantList mems = g["currentMems"].toList();
            for (int j = 0; j < mems.size(); ++j) {
                QVariantMap mem = mems[j].toMap();
                QString memId = mem["id"].toString();
                QString memName = mem["dName"].toString();
                if (memName.isEmpty()) memName = mem["zaloName"].toString();
                if (!memId.isEmpty() && !memName.isEmpty()) {
                    m_memberNames[memId] = memName;
                }
            }
        }
    }

    qDebug() << "[Zalo] groupDetails found" << threads.size() << "groups with names";
    if (!threads.isEmpty())
        emit conversationsReady(threads);
    m_isFetchingConversations = false;
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

    // Check dedup persistent: so hash của URL mới với cái đã lưu trên đĩa
    // cho đúng threadId này (khóa ổn định theo từng người — background
    // avatar truyền vào là "bg_"+threadId nên không đụng avatar chính).
    // Đây là lý do cache sống được qua app restart, logout/login, và toggle
    // "Show Recalled Messages" (không đụng gì tới ảnh): nếu ảnh đại diện
    // chưa đổi thật thì dùng lại
    // the file already sitting in tmp instead of re-downloading it.
    QString newHash = md5Hex(baseUrl);
    QString storedHash, storedPath;
    if (avatarMetaLookup(threadId, storedHash, storedPath)) {
        QString fsPath = storedPath;
        if (fsPath.startsWith("file://")) fsPath = fsPath.mid(7);
        if (storedHash == newHash && !fsPath.isEmpty() && QFile::exists(fsPath)) {
            // Cùng URL avatar như lần trước, và file cache vẫn còn (chưa
            // chạy "Clear Cache") — dùng lại, không gọi mạng.
            m_avatarCache[url] = storedPath;
            m_avatarCache[baseUrl] = storedPath;
            emit avatarReady(threadId, storedPath);
            return;
        }
        // Hash URL đổi (đổi avatar thật) hoặc file bị mất — tải lại bên dưới.
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

    QString httpUrl = url;
    if (httpUrl.startsWith("https://"))
        httpUrl = "http://" + httpUrl.mid(8);

    QUrl avatarQUrl(httpUrl);
    QNetworkRequest avatarReq(avatarQUrl);
    avatarReq.setRawHeader("Referer",    "https://chat.zalo.me/");
    avatarReq.setRawHeader("User-Agent", m_userAgent.toUtf8());
    avatarReq.setRawHeader("Accept",     "image/webp,image/apng,image/*,*/*;q=0.8");
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

    // Filename cố định theo từng người (md5 threadId, KHÔNG phải md5 URL):
    // đổi avatar sẽ ghi đè đúng file cũ thay vì để lại ảnh cũ mồ côi trong
    // tmp mỗi lần CDN trả URL khác cho cùng 1 avatar chưa đổi.
    QString fname = "/tmp/avatar_" + md5Hex(threadId) + ".jpg";
    QFile f(fname);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    QString localPath = "file://" + fname;
    m_avatarCache[url] = localPath;
    int qmark = url.indexOf('?');
    if (qmark > 0) m_avatarCache[url.left(qmark)] = localPath;

    // Lưu lại mapping threadId -> urlHash -> localPath để lần mở app sau
    // (hoặc sau logout/login) nhận ra đúng avatar này mà không cần tải lại.
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
    innerParams["count"]       = 20000;
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

    qDebug() << "[Zalo] fetchFriends raw (first300):" << raw.left(300);

    // Xử lý HTTP 429 Too Many Requests — raw là HTML, không phải JSON
    if (raw.contains("429 Too Many Requests") || raw.contains("<html")) {
        qDebug() << "[Zalo] fetchFriends: got 429, backing off for" << (FETCH_COOLDOWN_MS * 3) << "ms";
        m_isFetchingFriends = false;
        // Đặt cooldown dài hơn bình thường khi bị rate-limit
        m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch() + (FETCH_COOLDOWN_MS * 2);
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root["error_code"].toInt() != 0) {
        qDebug() << "[Zalo Error] fetchFriends:" << root["error_message"].toString();
        m_isFetchingFriends = false;
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchFriends decrypted (first300):" << dec.left(300);

    QVariantList friends;
    QVariantMap outer = jsonToMap(dec.toUtf8());
    if (outer.contains("data") && outer["data"].type() == QVariant::List)
        friends = outer["data"].toList();
    else if (outer.contains("friends") && outer["friends"].type() == QVariant::List)
        friends = outer["friends"].toList();
    else {
        QVariantList arr = jsonToList(dec.toUtf8());
        if (!arr.isEmpty())
            friends = arr;
    }

    qDebug() << "[Zalo] fetchFriends found" << friends.size() << "friends";

    QVariantList threads;
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

    qDebug() << "[Zalo] fetchFriends parsed" << threads.size() << "valid friends";
    m_lastFetchFriendsTime = QDateTime::currentMSecsSinceEpoch();

    if (!threads.isEmpty()) {
        // QUAN TRỌNG: tính needDownload và set m_pendingFriends/
        // m_pendingFriendAvatarCount TRƯỚC emit friendsReady() bên dưới,
        // không phải sau. emit ở đây là đồng bộ — handler onFriendsReady
        // phía QML chạy hết (kể cả gần 87 lệnh zService.downloadAvatar()
        // gọi liên tiếp) ngay trên cùng call stack này trước khi trả lại
        // quyền điều khiển cho hàm này. Trước đây bookkeeping chạy SAU
        // emit, nên suốt vòng lặp QML tái nhập đó, m_pendingFriends vẫn là
        // giá trị CŨ — gây SIGSEGV trong nội bộ QHash của Qt khi cache
        // avatar rỗng và cả 87 friend cùng chạm path bookkeeping đồng thời.
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

// Lấy list quick-message của user từ tài khoản Zalo thật (api/quickmessage/list
// — cùng dạng GET + query param AES-encrypt như fetchFriends() ở trên) rồi
// merge vào bảng quick_messages local. "keyword" -> tên quick message,
// "message.title" -> nội dung. Khớp theo tên (không phân biệt hoa/thường)
// với cái đã lưu local, cùng quy tắc "data cũ luôn thắng" như importData()
// trong ZaloService_Db.cpp.
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

    // Cùng dạng bọc 2 lớp như mọi endpoint khác ở đây (fetchFriends,
    // fetchInvites, ...): blob giải mã ra chính nó là {error_code,
    // error_message, data:{...}}, với payload thật của quickmessage/list —
    // {cursor, version, items} — nằm trong key "data" lồng bên trong, không
    // phải ở top level.
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap payload = outer.value("data").toMap();
    QVariantList items = payload.value("items").toList();
    if (items.isEmpty() && outer.contains("items"))
        items = outer.value("items").toList(); // fallback in case the shape ever changes
    qDebug() << "[Zalo] fetchServerQuickMessages found" << items.size() << "items";

    // Tên đã có local, cùng cách dedup như importData().
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
    QVariantMap outer = jsonToMap(dec.toUtf8());
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

        // Có 2 field dễ nhầm: "recommItemType" ở ngoài cùng cấp với dataInfo
        // là số chưa rõ nghĩa, còn field thật sự đánh dấu loại (1=PYMK,
        // 2=ReceivedFriendRequest) là dataInfo.recommType. Lọc theo field
        // ngoài là bug — không phân biệt được PYMK với lời mời thật, phải
        // dùng dataInfo.recommType.
        int itemType = info["recommType"].toInt();

        if (i < 5)
            qDebug() << "[Zalo] fetchInvites item[" << i << "] outer.recommItemType=" << item["recommItemType"].toInt()
                     << "dataInfo.recommType=" << itemType
                     << "dataInfo keys=" << info.keys();

        if (itemType != 2) continue;

        if (info.isEmpty()) continue;

        // userId là field đúng
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

        // message từ recommInfo.message
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

// ─── fetchGroupBoard ──────────────────────────────────────────────────────
// Group board = pinned message + note + poll gộp chung 1 chỗ (xem
// GroupBoardSheet.qml). GET {group_board service}/api/board/list?
// params=AES({group_id, board_type:0, page, count, last_id:0, last_type:0,
// imei}). board_type=0 xin server trả tất cả loại 1 lần thay vì lọc phía
// server — 4 tab trong sheet (All/Pinned Message/Note/Poll) chỉ là filter
// client-side trên 1 list đã fetch (BoardType enum: Note=1,
// PinnedMessage=2, Poll=3 — dùng bên dưới để tag từng item cho QML lọc).
//
// LƯU Ý: zpw_service_map_v3 có CẢ key "group_poll" lẫn key "group_board"
// riêng — đây là 2 host khác nhau, không phải bí danh của nhau. Endpoint
// board dùng zpwServiceMap.group_board; "group_poll" không dùng cho board
// (và cũng không dùng cho poll vote/create/lock — xem votePoll bên dưới,
// service giống m_groupServiceUrl). Dùng m_groupPollServiceUrl ở đây từng
// bị nhầm host, khiến mọi fetch board đều 404.
void ZaloService::fetchGroupBoard(const QString &groupId, int page, int count)
{
    if (!m_loggedIn || groupId.isEmpty()) return;

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap innerParams;
    innerParams["group_id"]   = groupId;
    innerParams["board_type"] = 0;
    innerParams["page"]       = (page > 0) ? page : 1;
    innerParams["count"]      = (count > 0) ? count : 50;
    innerParams["last_id"]    = 0;
    innerParams["last_type"]  = 0;
    innerParams["imei"]       = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QString urlStr = base + "/api/board/list"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] fetchGroupBoard GET" << urlStr.left(120) << "groupId:" << groupId;
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onFetchGroupBoardDone()));
}

void ZaloService::onFetchGroupBoardDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] fetchGroupBoard raw (first200):" << raw.left(200);

    // Cùng rủi ro false-positive-success như onPinGroupMessageDone: trang
    // lỗi HTML (vd 404 do gọi nhầm host) parse ra QVariantMap rỗng qua
    // jsonToMap(), nên error_code âm thầm mặc định 0 nếu không check
    // HTTP status/kết quả parse trước.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] fetchGroupBoard HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit groupBoardReady(groupId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] fetchGroupBoard: response did not parse as JSON";
        emit groupBoardReady(groupId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        qDebug() << "[Zalo] fetchGroupBoard error_code:" << ec << root["error_message"].toString();
        emit groupBoardReady(groupId, QVariantList(), root["error_message"].toString().isEmpty()
                              ? QString("Error %1").arg(ec) : root["error_message"].toString());
        return;
    }

    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] fetchGroupBoard decrypted (first1500):" << dec.left(1500);
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantMap boardData = (outer.contains("data") && outer["data"].type() == QVariant::Map)
                             ? outer["data"].toMap() : outer;
    QVariantList rawItems = boardData["items"].toList();
    qDebug() << "[Zalo] fetchGroupBoard items count:" << rawItems.size();

    QVariantList items;
    for (int i = 0; i < rawItems.size(); ++i) {
        QVariantMap raw_ = rawItems[i].toMap();
        int boardType = raw_["boardType"].toInt();
        QVariantMap data = raw_["data"].toMap();

        QVariantMap item;
        // BoardType per zca-js: Note=1, PinnedMessage=2, Poll=3
        if (boardType == 1) {
            item["boardType"] = "note";
        } else if (boardType == 2) {
            item["boardType"] = "pin";
        } else if (boardType == 3) {
            item["boardType"] = "poll";
        } else {
            continue; // unknown type — skip rather than showing a broken card
        }

        // Field chung cho cả 3 dạng chi tiết (Note/PinnedMessage cùng chung
        // envelope; Poll có shape riêng — xem bên dưới).
        if (boardType == 1 || boardType == 2) {
            item["id"]         = data["id"].toString();
            item["creatorId"]  = data["creatorId"].toString();
            item["createTime"] = data["createTime"].toLongLong();
            // "params" đến trên wire dạng chuỗi JSON lồng trong JSON —
            // jsonToMap() đã tự decode object lồng từ bản decrypt ngoài,
            // nhưng params vẫn có thể là chuỗi thô nếu server gửi encode 2
            // lớp, nên xử lý phòng thủ cả 2 dạng.
            QVariant paramsV = data["params"];
            QVariantMap params_;
            if (paramsV.type() == QVariant::String) {
                params_ = jsonToMap(paramsV.toString().toUtf8());
            } else {
                params_ = paramsV.toMap();
            }
            item["title"] = params_["title"].toString();
            item["extra"] = params_["extra"].toString();
            // id của chính tin nhắn chat gốc (msgId trong msgModel) — KHÔNG
            // giống item["id"] ở trên (id của topic board/pin này). Cần để
            // tap vào pin trong PinboardBar có thể scroll tới + highlight
            // đúng dòng tin nhắn.
            if (boardType == 2) item["msgId"] = params_["global_msg_id"].toString();
        } else {
            // Poll
            item["id"]              = QString::number(data["poll_id"].toLongLong());
            item["creatorId"]       = data["creator"].toString();
            item["createTime"]      = data["created_time"].toLongLong();
            item["question"]        = data["question"].toString();
            item["closed"]          = data["closed"].toBool();
            item["numVote"]         = data["num_vote"].toInt();
            item["allowMultiChoices"] = data["allow_multi_choices"].toBool();
            QVariantList opts = data["options"].toList();
            QVariantList outOpts;
            for (int j = 0; j < opts.size(); ++j) {
                QVariantMap o = opts[j].toMap();
                QVariantMap oo;
                oo["content"]  = o["content"].toString();
                oo["votes"]    = o["votes"].toInt();
                oo["voted"]    = o["voted"].toBool();
                oo["optionId"] = o["option_id"].toInt();
                outOpts.append(oo);
            }
            item["options"] = outOpts;
        }
        items.append(item);
    }
    emit groupBoardReady(groupId, items, QString());
}

// ─── pinGroupMessage ──────────────────────────────────────────────────────
// Ghim 1 tin nhắn vào group board. Dùng cùng endpoint
// {group_board}/api/board/topic/createv2 mà createNote dùng — chỉ khác
// type:2 (PinnedMessage board item) thay vì type:0 (Note), và params.params
// là 1 JSON string mô tả tin nhắn được pin thay vì tiêu đề note:
//   payload.params = { grid, type:2, color:-14540254, emoji:"📌",
//     startTime:-1, duration:-1, repeat:0, src:-1, imei, pinAct:1,
//     params: JSON.stringify({ client_msg_id, global_msg_id, senderUid,
//       senderName, title, msg_type }) }
// Chỉ hỗ trợ dạng "webchat" (text) và "chat.photo" — 2 loại tin nhắn duy
// nhất app này thực sự tạo/pin được. msgType ở đây là mã message type của
// Zalo (1=webchat, 32=chat.photo), cùng quy ước sendMessageQuote() đã dùng
// cho qmsgType; QML convert từ msgType nội bộ 1/2 trước khi gọi hàm này.
void ZaloService::pinGroupMessage(const QString &groupId, const QString &msgId,
                                   const QString &cliMsgId, const QString &senderId,
                                   const QString &senderName, const QString &content,
                                   int msgType)
{
    if (!m_loggedIn || groupId.isEmpty() || msgId.isEmpty()) {
        emit pinMessageDone(false, "Not ready to pin");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap pinParams;
    pinParams["client_msg_id"] = cliMsgId;
    pinParams["global_msg_id"] = msgId;
    pinParams["senderUid"]     = senderId.isEmpty() ? m_uid : senderId;
    pinParams["senderName"]    = senderName;
    pinParams["title"]         = content;
    pinParams["msg_type"]      = (msgType == 32) ? 32 : 1;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 2; // BoardType.PinnedMessage
    innerParams["color"]     = -14540254;
    innerParams["emoji"]     = QString::fromUtf8("\xF0\x9F\x93\x8C"); // 📌
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = -1;
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = 1;
    // Field JSON-string lồng bên trong: mapToJson() chỉ xử lý flat (xem
    // comment của nó trong ZaloServiceUtils.hpp), nên serialize object bên
    // trong thành chuỗi JSON riêng trước, rồi nhúng như 1 giá trị chuỗi
    // bình thường vào map ngoài.
    innerParams["params"]    = QString::fromUtf8(mapToJson(pinParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] pinGroupMessage POST" << urlStr.left(120) << "groupId:" << groupId << "msgId:" << msgId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onPinGroupMessageDone()));
}

void ZaloService::onPinGroupMessageDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] pinGroupMessage raw (first200):" << raw.left(200);

    // Status HTTP không phải 2xx (vd trang lỗi 404 nginx do gọi nhầm host)
    // hoặc lỗi tầng mạng nghĩa là request chưa từng chạm tới logic API
    // thật. jsonToMap() trên trang lỗi HTML âm thầm trả về QVariantMap
    // RỖNG (không phải JSON), nên root["error_code"].toInt() trước đây
    // mặc định về 0 — đọc y hệt mã "không lỗi" của Zalo và báo pin thành
    // công dù thực ra 404. Guard tường minh thay vì tin error_code trên
    // body rỗng/không parse được.
    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] pinGroupMessage HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pinMessageDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] pinGroupMessage: response did not parse as JSON";
        emit pinMessageDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] pinGroupMessage error_code:" << ec << err;
        emit pinMessageDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit pinMessageDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You pinned a message", groupId, true);
}

// ─── createGroupNote ──────────────────────────────────────────────────────
// Ported from zca-js's createNote.ts: same {group_board}/api/board/topic/
// createv2 endpoint pinGroupMessage() uses, type:0 (BoardType.Note) instead
// of type:2 (PinnedMessage), and params.params only carries {title} — no
// client_msg_id/global_msg_id/senderUid, since a note isn't tied to an
// existing chat message the way a pin is.
void ZaloService::createGroupNote(const QString &groupId, const QString &title, bool pinAct)
{
    if (!m_loggedIn || groupId.isEmpty() || title.trimmed().isEmpty()) {
        emit createNoteDone(false, "Not ready to create note");
        return;
    }

    QString base = m_groupBoardServiceUrl.isEmpty() ? m_groupServiceUrl : m_groupBoardServiceUrl;

    QVariantMap noteParams;
    noteParams["title"] = title;

    QVariantMap innerParams;
    innerParams["grid"]      = groupId;
    innerParams["type"]      = 0; // BoardType.Note
    innerParams["color"]     = -16777216;
    innerParams["emoji"]     = QString();
    innerParams["startTime"] = -1;
    innerParams["duration"]  = -1;
    innerParams["repeat"]    = 0;
    innerParams["src"]       = 1; // src của note là 1 (khác src:-1 của pinGroupMessage)
    innerParams["imei"]      = m_imei;
    innerParams["pinAct"]    = pinAct ? 1 : 0;
    innerParams["params"]    = QString::fromUtf8(mapToJson(noteParams));

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(innerParams)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = base + "/api/board/topic/createv2"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] createGroupNote POST" << urlStr.left(120) << "groupId:" << groupId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupNoteDone()));
}

void ZaloService::onCreateGroupNoteDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] createGroupNote raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] createGroupNote HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createNoteDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] createGroupNote: response did not parse as JSON";
        emit createNoteDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] createGroupNote error_code:" << ec << err;
        emit createNoteDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createNoteDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a note", groupId, true);
}

// ─── createGroupPoll ──────────────────────────────────────────────────────
// Khác createGroupNote/pinGroupMessage, hàm này dùng service "group" thường
// (m_groupServiceUrl) — KHÔNG phải group_board và không phải group_poll
// (xem doc comment fetchGroupBoard() lý do 2 cái đó dễ nhầm nhau). Action
// poll (create/vote/lock/detail/option-add/share) đều dùng "group", còn
// board listing/pin/note dùng "group_board".
void ZaloService::createGroupPoll(const QString &groupId, const QString &question,
                                   const QStringList &optionsList, bool allowMultiChoices,
                                   bool allowAddNewOption, bool hideVotePreview,
                                   bool isAnonymous)
{
    if (!m_loggedIn || groupId.isEmpty() || question.trimmed().isEmpty() || optionsList.size() < 2) {
        emit createPollDone(false, "Poll needs a question and at least 2 options");
        return;
    }

    QVariantMap params;
    params["group_id"]             = groupId;
    params["question"]             = question;
    QVariantList optsVariant;
    foreach (const QString &opt, optionsList) optsVariant.append(opt);
    params["options"]               = optsVariant;
    params["expired_time"]          = 0; // "Set deadline" UI chưa làm — 0 = không hết hạn
    params["pinAct"]                = false;
    params["allow_multi_choices"]   = allowMultiChoices;
    params["allow_add_new_option"]  = allowAddNewOption;
    params["is_hide_vote_preview"]  = hideVotePreview;
    params["is_anonymous"]          = isAnonymous;
    params["poll_type"]             = 0;
    params["src"]                   = 1;
    params["imei"]                  = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/create"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] createGroupPoll POST" << urlStr.left(120) << "groupId:" << groupId << "options:" << optionsList.size();
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onCreateGroupPollDone()));
}

void ZaloService::onCreateGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] createGroupPoll raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] createGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit createPollDone(false, QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] createGroupPoll: response did not parse as JSON";
        emit createPollDone(false, "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] createGroupPoll error_code:" << ec << err;
        emit createPollDone(false, err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }
    emit createPollDone(true, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You created a poll", groupId, true);
}

// ─── voteGroupPoll ────────────────────────────────────────────────────────
// GET với params đã encrypt trong query string (cùng dạng fetchGroupBoard
// đã dùng), gọi m_groupServiceUrl giống createGroupPoll ở trên (KHÔNG phải
// group_board, không phải group_poll). optionIds rỗng = xóa vote đã chọn.
void ZaloService::voteGroupPoll(const QString &groupId, const QString &pollId, const QList<int> &optionIds)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit votePollDone(false, pollId, QVariantList(), "Not ready to vote");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    QVariantList optIdsVariant;
    foreach (int optId, optionIds) optIdsVariant.append(optId);
    params["option_ids"] = optIdsVariant;
    params["imei"] = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QString urlStr = m_groupServiceUrl + "/api/poll/vote"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE)
                   + "&params=" + QUrl::toPercentEncoding(encParams);

    qDebug() << "[Zalo] voteGroupPoll GET" << urlStr.left(120) << "pollId:" << pollId << "options:" << optionIds.size();
    QNetworkReply *reply = m_manager->get(buildRequest(urlStr, "https://chat.zalo.me/"));
    reply->setProperty("pollId", pollId);
    reply->setProperty("groupId", groupId);
    connect(reply, SIGNAL(finished()), this, SLOT(onVoteGroupPollDone()));
}

void ZaloService::onVoteGroupPollDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId  = reply->property("pollId").toString();
    QString groupId = reply->property("groupId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] voteGroupPoll raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] voteGroupPoll HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit votePollDone(false, pollId, QVariantList(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] voteGroupPoll: response did not parse as JSON";
        emit votePollDone(false, pollId, QVariantList(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] voteGroupPoll error_code:" << ec << err;
        emit votePollDone(false, pollId, QVariantList(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    // Response { options: [...] } — decrypt+parse cùng cách fetchGroupBoard()
    // làm, rồi map option_id/votes/voted vào cùng shape mà poll card phía
    // QML đã dùng từ groupBoardReady()'s "options" field, để QML dùng lại
    // 1 đường render cho cả fetch ban đầu lẫn cập nhật vote live.
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] voteGroupPoll decrypted (first300):" << dec.left(300);
    QVariantMap outer = jsonToMap(dec.toUtf8());
    QVariantList rawOpts = outer["options"].toList();

    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toInt();
        outOpts.append(oo);
    }
    emit votePollDone(true, pollId, outOpts, QString());
    sendHubNotification(m_groupNames.value(groupId, "Zalo10"), "You voted in a poll", groupId, true);
}

// ─── getPollDetail ────────────────────────────────────────────────────────
// POST với params đã encrypt trong body (cùng dạng createGroupPoll ở trên
// dùng), gọi m_groupServiceUrl giống create/vote (không phải group_board,
// không phải group_poll). Chỉ list "voters" uid theo từng option là thông
// tin mới ở đây; phần còn lại trùng với những gì groupBoardReady/
// votePollDone đã mang sẵn, nên giữ riêng trong map dưới key "options"
// thay vì merge vào model có sẵn.
void ZaloService::getPollDetail(const QString &pollId)
{
    if (!m_loggedIn || pollId.isEmpty()) {
        emit pollDetailReady(pollId, QVariantMap(), "Not ready");
        return;
    }

    QVariantMap params;
    params["poll_id"] = pollId.toLongLong();
    params["imei"]    = m_imei;

    QString encParams = aesEncryptBase64(m_secretKey, QString::fromUtf8(mapToJson(params)));
    QByteArray body    = "params=" + QUrl::toPercentEncoding(encParams);

    QString urlStr = m_groupServiceUrl + "/api/poll/detail"
                   + "?zpw_ver=" + QString::number(API_VERSION)
                   + "&zpw_type=" + QString::number(API_TYPE);

    QNetworkRequest req = buildRequest(urlStr, "https://chat.zalo.me/");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    qDebug() << "[Zalo] getPollDetail POST" << urlStr.left(120) << "pollId:" << pollId;
    QNetworkReply *reply = m_manager->post(req, body);
    reply->setProperty("pollId", pollId);
    connect(reply, SIGNAL(finished()), this, SLOT(onGetPollDetailDone()));
}

void ZaloService::onGetPollDetailDone()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QString pollId = reply->property("pollId").toString();
    QByteArray raw = reply->readAll();
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netErr = reply->error();
    reply->deleteLater();

    qDebug() << "[Zalo] getPollDetail raw (first200):" << raw.left(200);

    if (netErr != QNetworkReply::NoError || (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300))) {
        qDebug() << "[Zalo] getPollDetail HTTP failure, status:" << httpStatus << "netErr:" << netErr;
        emit pollDetailReady(pollId, QVariantMap(), QString("HTTP %1").arg(httpStatus));
        return;
    }

    QVariantMap root = jsonToMap(raw);
    if (root.isEmpty()) {
        qDebug() << "[Zalo] getPollDetail: response did not parse as JSON";
        emit pollDetailReady(pollId, QVariantMap(), "Invalid response");
        return;
    }
    int ec = root["error_code"].toInt();
    if (ec != 0) {
        QString err = root["error_message"].toString();
        qDebug() << "[Zalo] getPollDetail error_code:" << ec << err;
        emit pollDetailReady(pollId, QVariantMap(), err.isEmpty() ? QString("Error %1").arg(ec) : err);
        return;
    }

    // Response { creator, question, options: [...] (content/votes/voted/
    // voters[]/option_id), joined, closed, poll_id, allow_multi_choices,
    // ..., num_vote } — cùng bước decrypt-rồi-jsonToMap như mọi endpoint
    // poll/board khác ở đây.
    QString dec = aesDecryptBase64(m_secretKey, root["data"].toString());
    qDebug() << "[Zalo] getPollDetail decrypted (first300):" << dec.left(300);
    QVariantMap outer = jsonToMap(dec.toUtf8());

    QVariantList rawOpts = outer["options"].toList();
    QVariantList outOpts;
    for (int i = 0; i < rawOpts.size(); ++i) {
        QVariantMap o = rawOpts[i].toMap();
        QVariantMap oo;
        oo["content"]  = o["content"].toString();
        oo["votes"]    = o["votes"].toInt();
        oo["voted"]    = o["voted"].toBool();
        oo["optionId"] = o["option_id"].toInt();
        QVariantList votersList;
        QVariantList rawVoters = o["voters"].toList();
        for (int j = 0; j < rawVoters.size(); ++j) votersList.append(rawVoters[j].toString());
        oo["voters"] = votersList;
        outOpts.append(oo);
    }

    QVariantMap detail;
    detail["question"]           = outer["question"].toString();
    detail["creator"]            = outer["creator"].toString();
    detail["closed"]             = outer["closed"].toBool();
    detail["allowMultiChoices"]  = outer["allow_multi_choices"].toBool();
    detail["numVote"]            = outer["num_vote"].toInt();
    detail["options"]            = outOpts;

    emit pollDetailReady(pollId, detail, QString());
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

