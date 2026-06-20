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

ZaloService::ZaloService(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)),
      m_qrExpireTimer(new QTimer(this)), m_listenTimer(new QTimer(this)),
      m_wsReconnectTimer(new QTimer(this)),
      m_webSocket(0), m_wsUrlIndex(0), m_wsConnected(false), m_wsHandshakeSent(false),
      m_userAgent(USER_AGENT), m_language("vi"), m_loggedIn(false), m_qrCancelled(false),
      m_isFetchingFriends(false), m_isFetchingConversations(false), m_loginEmitted(false),
      m_lastFetchFriendsTime(0), m_lastFetchConvoTime(0), m_db(0)
{
    m_qrExpireTimer->setSingleShot(true);
    m_wsReconnectTimer->setSingleShot(true);
    connect(m_qrExpireTimer,    SIGNAL(timeout()), this, SLOT(onQRExpired()));
    connect(m_listenTimer,      SIGNAL(timeout()), this, SLOT(onListenTimer()));
    connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onWsReconnectTimer()));
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
            "  imgHeight  INTEGER DEFAULT 0"
            ");";
        sqlite3_exec(m_db, sql, 0, 0, 0);
        // Migrations for existing DBs
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN msgType    INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN localImage TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgWidth   INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgHeight  INTEGER DEFAULT 0;",  0, 0, 0);
        // Preserves the original text of a recalled message so it can still be
        // shown (with a "(This message was recalled)" tag) when the user has
        // "Show Recalled Messages" enabled in Settings.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN recalledOriginalContent TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_thread ON messages(threadId,ts);", 0, 0, 0);
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
        qDebug() << "[Zalo] SQLite DB opened:" << dbPath;
    } else {
        qDebug() << "[Zalo] SQLite open FAILED";
        m_db = 0;
    }
}

ZaloService::~ZaloService() {
    if (m_db) { sqlite3_close(m_db); m_db = 0; }
}

