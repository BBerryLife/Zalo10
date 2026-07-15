#include "ZaloService.hpp"
#include "ZaloServiceUtils.hpp"
#include <sqlite3.h>
#include <QDateTime>
#include <QVariant>
#include <QDebug>
#include <time.h>
#include <exception>

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

void ZaloService::enqueueCommand(const QString &command, const QString &argsJson)
{
    if (!m_db) {
        qDebug() << "[Zalo] enqueueCommand: no db connection for" << command;
        return;
    }
    const char *sql = "INSERT INTO command_queue(command, argsJson, createdAt, processed) VALUES(?,?,?,0);";

    QByteArray cmdUtf8  = command.toUtf8();
    QByteArray argsUtf8 = argsJson.toUtf8();
    QByteArray tsUtf8   = QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();

    // Manual retry loop on top of PRAGMA busy_timeout: when the UI fires many
    // enqueueCommand() calls back-to-back in the same tick (e.g. downloadAvatar
    // for an entire 80+ item friends list, see ChatsTab.qml/GroupsTab.qml), a
    // request can land at the exact instant HeadlessService's own writer
    // (processCommandQueue()'s UPDATE/DELETE, running on its own QTimer in a
    // separate process) holds the write lock. busy_timeout only auto-retries
    // SQLITE_BUSY; a handful of these attempts still come back SQLITE_LOCKED
    // (schema/table lock contention) which busy_timeout does NOT retry on its
    // own. Retrying a few times here with a short sleep is enough to ride out
    // that window without silently dropping the command (which previously
    // meant vanished avatars / stalled sends the caller had no way to notice).
    const int maxAttempts = 5;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        sqlite3_stmt *stmt = 0;
        int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, cmdUtf8.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, argsUtf8.constData(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, tsUtf8.constData(), -1, SQLITE_TRANSIENT);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc == SQLITE_DONE) return; // success
        } else if (stmt) {
            sqlite3_finalize(stmt);
        }

        if (rc == SQLITE_LOCKED || rc == SQLITE_BUSY) {
            if (attempt < maxAttempts) {
                // Short, increasing backoff (10ms, 20ms, 30ms, 40ms) — cheap
                // enough not to visibly stall the UI thread, long enough to
                // clear a single UPDATE/DELETE from the other process.
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 10000000L * attempt;
                nanosleep(&ts, 0);
                continue;
            }
        }

        qDebug() << "[Zalo] enqueueCommand: insert failed for" << command
                 << "after" << attempt << "attempt(s) -" << sqlite3_errmsg(m_db);
        return;
    }
}

QMap<QString, QString> ZaloService::readServiceState()
{
    QMap<QString, QString> state;
    if (!m_db) return state;
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(m_db, "SELECT key, value FROM service_state;", -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QString key = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 0));
            QString val = QString::fromUtf8((const char*)sqlite3_column_text(stmt, 1));
            state[key] = val;
        }
        sqlite3_finalize(stmt);
    }
    return state;
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
//
// QUAN TRỌNG: trước đây SELECT không có LIMIT — nếu UI kịp ghi hàng trăm lệnh
// (ví dụ downloadAvatar cho toàn bộ friends+groups+group members sau 1 lần
// login) trước khi tick 500ms này chạy, cả TOÀN BỘ batch đó (quan sát thực
// tế: tới 300 lệnh) bị dispatch + UPDATE processed=1 đồng bộ, liên tục,
// trong CÙNG 1 lần gọi hàm này — tức 1 lần callback của Qt event loop bị
// chiếm dụng rất lâu không nhả ra. Trên BB10 Simulator, quan sát log cho
// thấy HeadlessService chết lặng lẽ (log dừng đột ngột, không dòng lỗi nào)
// đúng giữa những đợt batch lớn kiểu này — dù việc giới hạn số avatar tải
// ĐỒNG THỜI qua mạng (xem MAX_CONCURRENT_AVATAR_DOWNLOADS) đã làm giảm bớt,
// vẫn crash vì bản thân việc dispatch+ghi DB 300 lệnh liên tục trong 1 tick
// (không network) đã đủ giữ event loop bận quá lâu. Giới hạn batch mỗi lần
// poll xuống MAX_COMMANDS_PER_POLL: các lệnh còn lại (processed vẫn =0) sẽ
// được xử lý ở (các) tick 500ms kế tiếp — trải batch 300 lệnh ra ~7-8 giây
// thay vì dồn hết vào 1 lần, event loop có cơ hội "thở" giữa các batch.
// MAX_COMMANDS_PER_POLL = 8 (xem giải thích ở comment phía trên; giảm từ 20
// xuống 8 sau khi phát hiện "bad allocation" vẫn lặp lại nhiều lần dù đã
// giới hạn batch — process đang thực sự sát trần bộ nhớ trên Simulator, cần
// giảm áp lực bộ nhớ đỉnh xuống thấp hơn nữa, không chỉ chặn crash).
void ZaloService::processCommandQueue()
{
    if (!m_db) return;

    sqlite3_stmt *stmt = 0;
    const char *selSql = "SELECT id, command, argsJson FROM command_queue WHERE processed=0 ORDER BY id ASC LIMIT 8;";
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

        // QUAN TRỌNG: mỗi lệnh được bọc try/catch RIÊNG. Trước đây không có —
        // nếu 1 lệnh (vd downloadAvatar) ném exception (bad_alloc), nó lan
        // thẳng ra khỏi cả vòng for này, thoát luôn processCommandQueue() TRƯỚC
        // khi kịp UPDATE processed=1 cho id đó. Vì SELECT luôn ORDER BY id ASC,
        // lệnh chết này mãi mãi đứng đầu hàng đợi — CHẶN ĐỨNG toàn bộ command
        // phía sau nó (mọi downloadAvatar khác, sendMessage, fetchConversations...
        // đều không bao giờ được xử lý nữa). Quan sát thực tế: 1 lệnh downloadAvatar
        // (id 92) bị dispatch lại mỗi 500ms, hàng trăm lần liên tục, mãi không
        // qua được — đúng là nguyên nhân "gửi tin nhắn không được", "không nhận
        // thông báo", "refresh không hoạt động" mà notify() (fix trước) không
        // giải quyết được vì nó chỉ ngăn CRASH, không ngăn 1 lệnh chặn cả hàng đợi.
        try {
            dispatchCommand(command, args);
        } catch (const std::exception &e) {
            qWarning() << "[ZaloIpc] command" << command << "id:" << id
                       << "THREW exception, bo qua lenh nay va danh dau processed de khong chan hang doi:" << e.what();
        } catch (...) {
            qWarning() << "[ZaloIpc] command" << command << "id:" << id
                       << "THREW unknown exception, bo qua lenh nay va danh dau processed de khong chan hang doi";
        }
        // Đánh dấu processed=1 ngay dưới đây, BẤT KỂ dispatchCommand() ở trên
        // thành công hay ném exception (đã catch phía trên) — đảm bảo lệnh
        // này không bao giờ chặn các lệnh phía sau trong hàng đợi nữa.
        //
        // QUAN TRỌNG: retry-with-backoff — trước đây chỉ thử 1 lần, nếu
        // SQLite trả SQLITE_BUSY/LOCKED (do UI process đang ghi enqueueCommand
        // cùng lúc — file DB dùng chung giữa 2 process) thì ÂM THẦM bỏ qua,
        // không update được, không log gì cả. Quan sát thực tế trong log: 1
        // lệnh (fetchConversations id 3, downloadAvatar id 18/19/20...) bị
        // dispatch LẠI ở tick kế tiếp dù đã xử lý xong — đúng vì processed=1
        // chưa kịp ghi. Cùng cơ chế retry đã áp dụng cho enqueueCommand() phía
        // UI (fix cũ, xem ZaloServiceProxy) — giờ áp dụng đối xứng ở đây.
        bool marked = false;
        for (int attempt = 0; attempt < 5 && !marked; ++attempt) {
            sqlite3_stmt *upd = 0;
            if (sqlite3_prepare_v2(m_db, "UPDATE command_queue SET processed=1 WHERE id=?;", -1, &upd, 0) == SQLITE_OK) {
                sqlite3_bind_int(upd, 1, id);
                int rc = sqlite3_step(upd);
                sqlite3_finalize(upd);
                if (rc == SQLITE_DONE) {
                    marked = true;
                } else if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
                    struct timespec ts = { 0, 20L * 1000L * 1000L }; // 20ms
                    nanosleep(&ts, 0);
                }
            } else {
                struct timespec ts = { 0, 20L * 1000L * 1000L };
                nanosleep(&ts, 0);
            }
        }
        if (!marked) {
            qWarning() << "[ZaloIpc] command" << command << "id:" << id
                       << "KHONG the danh dau processed sau 5 lan thu (DB locked lien tuc) - se bi dispatch lai o tick ke tiep";
        }
    }

    sqlite3_exec(m_db,
        "DELETE FROM command_queue WHERE processed=1 AND createdAt < datetime('now','-1 hour');",
        0, 0, 0);
}

// Bảng dispatch thủ công tách riêng khỏi processCommandQueue() — để lệnh gọi
// này có thể được bọc try/catch RIÊNG cho TỪNG lệnh (xem processCommandQueue()).
// Danh sách này chỉ cần cover các lệnh UI thực sự gọi (queryShareTargets,
// getImageDimensions... vẫn có thể chạy ngay trong UI process vì không đụng
// network/session).
void ZaloService::dispatchCommand(const QString &command, const QVariantMap &args)
{
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
}
