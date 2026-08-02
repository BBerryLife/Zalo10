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

// Construction/teardown, static identity constants, and small image-info helpers.
// The bulk of ZaloService's behavior lives in the ZaloService_*.cpp files alongside
// this one (Auth, WebSocket, Contacts, Messages, Crypto, Network, Db) — see ZaloService.hpp
// for the full class declaration.

const char *ZaloService::USER_AGENT =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

const char *ZaloService::AES_FIXED_KEY = "3FC4F0D2AB50057BCE0D90D9187A22B1";

QSize ZaloService::imageDimensions(const QString &localFileUrlOrPath) const
{
    QString p = localFileUrlOrPath;
    if (p.startsWith("file://")) p = p.mid(7);
    if (p.isEmpty()) return QSize();
    QImage img(p);
    if (img.isNull()) return QSize();
    return img.size();
}

QVariantMap ZaloService::getImageDimensions(const QString &localFilePath) const
{
    QSize sz = imageDimensions(localFilePath);
    QVariantMap m;
    m["width"]  = (sz.width()  > 0) ? sz.width()  : 0;
    m["height"] = (sz.height() > 0) ? sz.height() : 0;
    return m;
}

qint64 ZaloService::getFileSize(const QString &localFilePath) const
{
    QString path = localFilePath;
    if (path.startsWith("file://")) path = path.mid(7);
    return QFileInfo(path).size();
}

ZaloService::ZaloService(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)),
      m_qrExpireTimer(new QTimer(this)), m_listenTimer(new QTimer(this)),
      m_wsReconnectTimer(new QTimer(this)), m_keepAliveTimer(new QTimer(this)),
      m_webSocket(0), m_wsUrlIndex(0), m_wsAdvanceUrlOnReconnect(false),
      m_wsConsecutiveFailCount(0),
      m_wsSslCtx(0), m_wsSsl(0), m_wsUseSsl(false), m_wsTlsEstablished(false),
      m_wsConnected(false), m_wsHandshakeSent(false),
      m_userAgent(USER_AGENT), m_language("vi"), m_loggedIn(false), m_qrCancelled(false),
      m_isFetchingFriends(false), m_isFetchingConversations(false), m_loginEmitted(false),
      m_lastFetchFriendsTime(0), m_lastFetchConvoTime(0), m_db(0),
      m_updateReply(0)
{
    m_qrExpireTimer->setSingleShot(true);
    m_wsReconnectTimer->setSingleShot(true);
    connect(m_qrExpireTimer,    SIGNAL(timeout()), this, SLOT(onQRExpired()));
    connect(m_listenTimer,      SIGNAL(timeout()), this, SLOT(onListenTimer()));
    connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onWsReconnectTimer()));
    connect(m_keepAliveTimer,   SIGNAL(timeout()), this, SLOT(onKeepAliveTimer()));
    qsrand((uint)QDateTime::currentMSecsSinceEpoch());
    qDebug() << "[Zalo] ZaloService initialized";

    QString dbPath = QDir::homePath() + "/zalo_messages.db";
    if (sqlite3_open(dbPath.toUtf8().constData(), &m_db) == SQLITE_OK) {
        const char *sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "  msgId      TEXT PRIMARY KEY,"
            "  threadId   TEXT NOT NULL,"
            "  content    TEXT,"
            "  senderId   TEXT,"
            "  dName      TEXT,"
            "  ts         TEXT,"
            "  isMine     INTEGER DEFAULT 0,"
            "  isGroup    INTEGER DEFAULT 0,"
            "  msgType    INTEGER DEFAULT 0,"
            "  localImage TEXT DEFAULT '',"
            "  imgWidth   INTEGER DEFAULT 0,"
            "  imgHeight  INTEGER DEFAULT 0,"
            "  cliMsgId   TEXT DEFAULT ''"
            ");";
        sqlite3_exec(m_db, sql, 0, 0, 0);
        // Migrations for existing DBs
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN msgType    INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN localImage TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgWidth   INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgHeight  INTEGER DEFAULT 0;",  0, 0, 0);
        // cliMsgId: the client-generated message id echoed back by Zalo's WS/HTTP
        // responses. Needed as-is (not msgId) by the delete/recall (undo) APIs —
        // see deleteMessage.ts/undo.ts in zca-js, both key off cliMsgId + msgId.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN cliMsgId   TEXT    DEFAULT '';", 0, 0, 0);
        // Preserves the original text of a recalled message so it can still be
        // shown (with a "(This message was recalled)" tag) when the user has
        // "Show Recalled Messages" enabled in Settings.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN recalledOriginalContent TEXT DEFAULT '';", 0, 0, 0);
        // Reply/quote support: when a message quotes an earlier one, we persist
        // just enough of the quoted message to render the small preview strip
        // inside the reply bubble (sender name + a short snippet of its
        // content/type) without needing a second DB lookup at render time.
        // quoteMsgId is what a tap on the preview jumps to (see ChatView.qml's
        // scrollToMsgIndex + doJumpToQuoted()). Kept as flat columns (not a
        // join) to match every other message field in this table.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgId     TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteContent   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteSenderName TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgType   INTEGER DEFAULT 0;", 0, 0, 0);
        // Who sent the quoted message (their raw uid, "" if unknown) — lets
        // the QML delegate tell "replying to myself" apart from "replying to
        // the other party" (see quoteSenderResolved in ChatView.qml), which
        // quoteSenderName alone can't disambiguate reliably given Zalo's wire
        // dName/fromD quirks.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteOwnerId   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_thread ON messages(threadId,ts);", 0, 0, 0);
        // One row per (message, person) reaction — PRIMARY KEY(msgId,uid) means
        // a fresh INSERT OR REPLACE naturally implements "changing your
        // reaction icon overwrites your old one" (one active reaction per
        // person per message, exactly like Zalo/Messenger's own behavior),
        // and a DELETE removes it entirely when someone un-reacts. icon is
        // one of "like"/"heart"/"haha"/"wow"/"cry"/"angry" (see
        // ReactionPickerSheet.qml). threadId is stored purely so
        // dbLoadThreadReactions() can bulk-fetch every reaction for a whole
        // open chat in one query instead of one per message.
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS message_reactions ("
            "  msgId    TEXT NOT NULL,"
            "  threadId TEXT NOT NULL,"
            "  uid      TEXT NOT NULL,"
            "  icon     TEXT NOT NULL,"
            "  ts       INTEGER DEFAULT 0,"
            "  PRIMARY KEY (msgId, uid)"
            ");", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_reactions_thread ON message_reactions(threadId);", 0, 0, 0);
        // Track per-thread clear timestamps so re-fetched server msgs are filtered
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS cleared_threads ("
            "  threadId TEXT PRIMARY KEY,"
            "  clearedAt TEXT NOT NULL"
            ");", 0, 0, 0);
        // Quick Messages ("/command" canned replies) — global, not per-thread.
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS quick_messages ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name      TEXT NOT NULL,"
            "  content   TEXT NOT NULL,"
            "  createdAt TEXT"
            ");", 0, 0, 0);
        // Enforces unique commands (case-insensitive) at the DB level — addQuickMessage/
        // updateQuickMessage rely on the resulting SQLITE_CONSTRAINT to detect duplicates
        // instead of doing a separate SELECT check first.
        sqlite3_exec(m_db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_qm_name ON quick_messages(name COLLATE NOCASE);", 0, 0, 0);
        // Persistent avatar metadata: survives app restarts AND logout/login —
        // only clearCache() touches this table. Maps a stable threadId (user or
        // group id) to the md5 of the avatar URL we last downloaded for it, plus
        // the local file path. On every downloadAvatar() call we compare the new
        // URL's hash against urlHash; if it matches and the file is still on disk
        // we skip the network round-trip entirely. If the hash differs, the user
        // genuinely changed their profile picture, so we re-download and overwrite
        // the same file (avatar_<md5(threadId)>.jpg — fixed per-person, not
        // per-URL, so a changed avatar never leaves an orphaned old file behind).
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS avatar_meta ("
            "  threadId  TEXT PRIMARY KEY,"
            "  urlHash   TEXT NOT NULL,"
            "  localPath TEXT NOT NULL,"
            "  updatedAt TEXT"
            ");", 0, 0, 0);
        // Tombstone of msgIds the user hard-deleted via "delete for me" (chat.delete,
        // onlyMe=true). Delete-for-me never removes the message server-side, so any
        // later cmd=510 history re-sync (e.g. re-entering the thread, or app restart —
        // m_threadLastMsgId in ZaloService.hpp is in-memory only and always starts
        // empty, forcing a lastId=0 full re-fetch) will happily hand the "deleted"
        // message straight back to dbSaveMessage(), which would otherwise re-insert
        // it and make it reappear in the UI. Every msgId here must be permanently
        // skipped by dbSaveMessage() regardless of how many times it resurfaces from
        // the server. Deliberately NOT wiped by clearCache() (same reasoning as
        // cleared_threads: this is a durable user choice, not disposable cache).
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS deleted_messages ("
            "  msgId     TEXT PRIMARY KEY,"
            "  threadId  TEXT NOT NULL,"
            "  deletedAt TEXT"
            ");", 0, 0, 0);
        qDebug() << "[Zalo] SQLite DB opened:" << dbPath;
    } else {
        qDebug() << "[Zalo] SQLite open FAILED";
        m_db = 0;
    }

    // Sanity-check the persistent avatar cache at startup: log how many cached
    // avatar files from a previous session/login are still on disk. The actual
    // reuse logic lives in downloadAvatar(), which checks avatar_meta fresh on
    // every call — this is just a startup diagnostic.
    loadAvatarCacheFromDb();
}

ZaloService::~ZaloService() {
    if (m_db) { sqlite3_close(m_db); m_db = 0; }
}

