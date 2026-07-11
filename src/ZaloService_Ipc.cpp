#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include <sqlite3.h>
#include <QDateTime>
#include <QVariant>
#include <QDebug>

// ---------------------------------------------------------------------------
// IPC giữa HeadlessService (giữ WS thật, chạy nền không phụ thuộc UI) và
// ApplicationUI (thin client, chỉ hiển thị). Kênh IPC = chính file SQLite mà
// ZaloService đã dùng để lưu tin nhắn từ trước (WAL mode, xem ZaloService.cpp).
//
//   UI muốn làm gì đó (gửi tin, login...)
//     -> ghi 1 dòng vào command_queue (processed=0)
//   HeadlessService (sở hữu ZaloService thật) mỗi ~500ms
//     -> processCommandQueue() đọc các dòng processed=0, dispatch, đánh dấu xong
//   HeadlessService muốn UI biết trạng thái (loggedIn, qrCode...)
//     -> publishState() ghi vào service_state
//   UI đọc service_state bằng QTimer/QFileSystemWatcher riêng của nó (xem
//   ZaloServiceProxy trong applicationui.cpp)
// ---------------------------------------------------------------------------

void ZaloService::onPublishLoggedInState()
{
    publishState("loggedIn", m_loggedIn ? "1" : "0");
    if (!m_loggedIn) {
        // Đăng xuất: xoá luôn các key phụ thuộc để UI không hiển thị nhầm
        // thông tin của phiên trước đó.
        publishState("uid", "");
        publishState("displayName", "");
    }
}

void ZaloService::onPublishQrState(const QString &qrImagePath, const QString &qrCodeRaw)
{
    publishState("qrImagePath", qrImagePath);
    publishState("qrCodeRaw", qrCodeRaw);
}

void ZaloService::onPublishSessionExpired()
{
    // sessionExpired được emit khi refresh session key thất bại (cookie/secretKey
    // không còn hợp lệ) — đây chính là trường hợp "phải QR lại" mà Jim muốn tránh.
    // UI cần biết ngay để hiển thị lại màn QR thay vì đứng im chờ mãi.
    publishState("sessionExpired", "1");
}

void ZaloService::onPublishLoginSuccess(const QString &uid, const QString &displayName)
{
    publishState("uid", uid);
    publishState("displayName", displayName);
    publishState("sessionExpired", "0");
}

void ZaloService::publishState(const QString &key, const QString &value)
{
    if (!m_db) return;
    const char *sql =
        "INSERT INTO service_state(key,value,updatedAt) VALUES(?,?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value, updatedAt=excluded.updatedAt;";
    sqlite3_stmt *stmt = 0;
    // Bản sqlite3 trên BB10 NDK khá cũ, một số bản không có "ON CONFLICT" (cần
    // sqlite >= 3.24). Nếu prepare lỗi (do cú pháp không hỗ trợ), fallback về
    // cách cũ INSERT OR REPLACE — an toàn hơn về mặt tương thích.
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0) != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        const char *sqlFallback = "INSERT OR REPLACE INTO service_state(key,value,updatedAt) VALUES(?,?,?);";
        if (sqlite3_prepare_v2(m_db, sqlFallback, -1, &stmt, 0) != SQLITE_OK) {
            qDebug() << "[ZaloIpc] publishState prepare failed for" << key;
            return;
        }
    }
    QByteArray keyUtf8 = key.toUtf8();
    QByteArray valUtf8 = value.toUtf8();
    QByteArray tsUtf8  = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
    sqlite3_bind_text(stmt, 1, keyUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, valUtf8.constData(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        qDebug() << "[ZaloIpc] publishState step failed for" << key;
    }
    sqlite3_finalize(stmt);
}

// NOTE: argsJson được ZaloServiceProxy ghi bằng mapToJsonSimple() — chỉ phẳng
// (String/Bool/Int/StringList), không có object/array lồng nhau — nên
// jsonToMap() (parser thủ công trong ZaloServiceUtils.hpp, không dùng
// QScriptEngine/JIT) là đủ và an toàn để chạy trong HeadlessService.
static QVariantMap parseArgsJson(const QString &json)
{
    return jsonToMap(json);
}

// Đọc mọi lệnh đang chờ (processed=0) và thực thi. Được HeadlessService gọi
// định kỳ (~500ms) qua QTimer — KHÔNG bao giờ gọi từ UI process.
void ZaloService::processCommandQueue()
{
    if (!m_db) return;

    sqlite3_stmt *stmt = 0;
    const char *selSql = "SELECT id, command, argsJson FROM command_queue WHERE processed=0 ORDER BY id ASC;";
    if (sqlite3_prepare_v2(m_db, selSql, -1, &stmt, 0) != SQLITE_OK) return;

    QList<QPair<int, QPair<QString, QString> > > pending;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        QString command = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
        QString argsJson = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 2));
        pending.append(qMakePair(id, qMakePair(command, argsJson)));
    }
    sqlite3_finalize(stmt);

    if (pending.isEmpty()) return;

    for (int i = 0; i < pending.size(); ++i) {
        int id = pending[i].first;
        const QString &command = pending[i].second.first;
        const QString &argsJson = pending[i].second.second;
        QVariantMap args = parseArgsJson(argsJson);

        qDebug() << "[ZaloIpc] dispatching command:" << command << "id:" << id;

        // Dispatch thủ công — danh sách này chỉ cần cover các lệnh UI thực sự
        // gọi (queryShareTargets, getImageDimensions... vẫn có thể chạy ngay
        // trong UI process vì không đụng network/session).
        if (command == "sendMessage") {
            sendMessage(args.value("threadId").toString(), args.value("content").toString(), args.value("isGroup").toBool());
        } else if (command == "sendPhoto") {
            sendPhoto(args.value("threadId").toString(), args.value("localFilePath").toString(),
                      args.value("isGroup").toBool(), args.value("caption").toString());
        } else if (command == "sendFile") {
            sendFile(args.value("threadId").toString(), args.value("localFilePath").toString(), args.value("isGroup").toBool());
        } else if (command == "deleteMessage") {
            deleteMessage(args.value("threadId").toString(), args.value("isGroup").toBool(),
                           args.value("msgId").toString(), args.value("cliMsgId").toString(),
                           args.value("senderId").toString(), args.value("onlyMe").toBool());
        } else if (command == "recallMessage") {
            recallMessage(args.value("threadId").toString(), args.value("isGroup").toBool(),
                          args.value("msgId").toString(), args.value("cliMsgId").toString());
        } else if (command == "startQRLogin") {
            startQRLogin();
        } else if (command == "retryQRLogin") {
            retryQRLogin();
        } else if (command == "cancelQRLogin") {
            cancelQRLogin();
        } else if (command == "logout") {
            logout();
        } else if (command == "loginWithCookie") {
            loginWithCookie(args.value("zpsid").toString(), args.value("zpwSek").toString(),
                             args.value("imei").toString(), args.value("ua").toString(), args.value("token").toString());
        } else if (command == "fetchConversations") {
            fetchConversations();
        } else if (command == "fetchFriends") {
            fetchFriends();
        } else if (command == "fetchInvites") {
            fetchInvites();
        } else if (command == "acceptFriendRequest") {
            acceptFriendRequest(args.value("friendId").toString());
        } else if (command == "rejectFriendRequest") {
            rejectFriendRequest(args.value("friendId").toString());
        } else if (command == "fetchGroupDetails") {
            fetchGroupDetails(args.value("groupIds").toStringList());
        } else if (command == "fetchMessages") {
            fetchMessages(args.value("threadId").toString(), args.value("isGroup").toBool());
        } else if (command == "downloadImageMessage") {
            downloadImageMessage(args.value("msgId").toString(), args.value("url").toString(), args.value("threadId").toString());
        } else if (command == "downloadAvatar") {
            downloadAvatar(args.value("threadId").toString(), args.value("url").toString());
        } else if (command == "setActiveThread") {
            setActiveThread(args.value("threadId").toString(), args.value("isGroup").toBool());
        } else if (command == "clearActiveThread") {
            clearActiveThread();
        } else if (command == "blockUser") {
            blockUser(args.value("userId").toString());
        } else if (command == "unblockUser") {
            unblockUser(args.value("userId").toString());
        } else if (command == "setMute") {
            setMute(args.value("threadId").toString(), args.value("isGroup").toBool(), args.value("mute").toBool());
        } else if (command == "clearHistory") {
            clearHistory(args.value("threadId").toString(), args.value("isGroup").toBool());
        } else if (command == "leaveGroup") {
            leaveGroup(args.value("groupId").toString());
        } else if (command == "fetchServerQuickMessages") {
            fetchServerQuickMessages();
        } else {
            qDebug() << "[ZaloIpc] unknown command:" << command;
        }

        // Đánh dấu đã xử lý ngay sau khi dispatch (không chờ callback async của
        // hàm trên hoàn tất — các hàm này vốn đã async, kết quả của chúng đi ra
        // qua DB/signal như thiết kế cũ, không qua resultJson).
        sqlite3_stmt *upd = 0;
        if (sqlite3_prepare_v2(m_db, "UPDATE command_queue SET processed=1 WHERE id=?;", -1, &upd, 0) == SQLITE_OK) {
            sqlite3_bind_int(upd, 1, id);
            sqlite3_step(upd);
            sqlite3_finalize(upd);
        }
    }

    // Dọn bớt các lệnh đã xử lý lâu (giữ queue nhỏ) — chỉ xoá lệnh cũ hơn 1 giờ
    // để không ảnh hưởng nếu đang debug/đọc lại.
    sqlite3_exec(m_db,
        "DELETE FROM command_queue WHERE processed=1 AND createdAt < datetime('now','-1 hour');",
        0, 0, 0);
}
