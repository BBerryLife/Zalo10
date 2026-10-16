#include "ZaloService.hpp"
#include "ZaloNetworkManager.hpp"
#include "ZaloCookieJar.hpp"
#include "ZaloServiceUtils.hpp"
#include "HubIntegration.hpp"
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
    : QObject(parent), m_manager(new ZaloNetworkAccessManager(this)),
      m_qrExpireTimer(new QTimer(this)), m_listenTimer(new QTimer(this)),
      m_wsReconnectTimer(new QTimer(this)), m_keepAliveTimer(new QTimer(this)),
      m_webSocket(0), m_wsUrlIndex(0), m_wsAdvanceUrlOnReconnect(false),
      m_wsConsecutiveFailCount(0),
      m_wsSslCtx(0), m_wsSsl(0), m_wsUseSsl(false), m_wsTlsEstablished(false),
      m_wsConnected(false), m_wsHandshakeSent(false),
      m_userAgent(USER_AGENT), m_language("vi"), m_loggedIn(false), m_qrCancelled(false), m_isAutoRenew(false), m_renewRetry(0),
      m_isFetchingFriends(false), m_isFetchingConversations(false), m_loginEmitted(false),
      m_lastFetchFriendsTime(0), m_lastFetchConvoTime(0), m_db(0),
      m_updateReply(0), m_videoDownloadReply(0), m_contactPicker(0),
      m_hub(new HubIntegration(this))
{
    m_manager->setCookieJar(new ZaloCookieJar(m_manager));
    m_realtimeSuspended = false;
    m_appForeground = true;
    m_qrExpireTimer->setSingleShot(true);
    m_wsReconnectTimer->setSingleShot(true);
    connect(m_qrExpireTimer,    SIGNAL(timeout()), this, SLOT(onQRExpired()));
    connect(m_listenTimer,      SIGNAL(timeout()), this, SLOT(onListenTimer()));
    connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onWsReconnectTimer()));
    connect(m_keepAliveTimer,   SIGNAL(timeout()), this, SLOT(onKeepAliveTimer()));
    qsrand((uint)QDateTime::currentMSecsSinceEpoch());
    m_hub->init();

    // Default Instant Preview = Allow so banners can show. apply() only takes effect once
    // and never overrides a user's own setting.
    {
        bb::platform::NotificationDefaultApplicationSettings defaultSettings;
        defaultSettings.setPreview(bb::platform::NotificationPriorityPolicy::Allow);
        defaultSettings.setTonePath(QUrl("file:///usr/share/sounds/notification-tones/bbm/present.m4a"));
        bb::platform::NotificationSettingsError::Type err = defaultSettings.apply();
    }

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
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN msgType    INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN localImage TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgWidth   INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgHeight  INTEGER DEFAULT 0;",  0, 0, 0);
        // Client-generated id echoed back by Zalo; delete/recall APIs need it, not msgId.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN cliMsgId   TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN recalledOriginalContent TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgId     TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteContent   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteSenderName TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgType   INTEGER DEFAULT 0;", 0, 0, 0);
        // Sender uid of the quoted message ("" if unknown).
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteOwnerId   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_thread ON messages(threadId,ts);", 0, 0, 0);
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
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS cleared_threads ("
            "  threadId TEXT PRIMARY KEY,"
            "  clearedAt TEXT NOT NULL"
            ");", 0, 0, 0);
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS quick_messages ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  name      TEXT NOT NULL,"
            "  content   TEXT NOT NULL,"
            "  createdAt TEXT"
            ");", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_qm_name ON quick_messages(name COLLATE NOCASE);", 0, 0, 0);
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS avatar_meta ("
            "  threadId  TEXT PRIMARY KEY,"
            "  urlHash   TEXT NOT NULL,"
            "  localPath TEXT NOT NULL,"
            "  updatedAt TEXT"
            ");", 0, 0, 0);
        // Tombstones of "delete for me" msgIds. The server keeps them and resync would bring
        // them back, so dbSaveMessage() must skip them. Not cleared by clearCache().
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS deleted_messages ("
            "  msgId     TEXT PRIMARY KEY,"
            "  threadId  TEXT NOT NULL,"
            "  deletedAt TEXT"
            ");", 0, 0, 0);
        // Outgoing message state: 2=delivered, 3=read; no row = sent. Separate table so
        // re-saving a message in dbSaveMessage() never resets it.
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS message_states ("
            "  msgId    TEXT PRIMARY KEY,"
            "  threadId TEXT NOT NULL,"
            "  state    INTEGER NOT NULL DEFAULT 1"
            ");", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_states_thread ON message_states(threadId);", 0, 0, 0);
        qDebug() << "[DB] opened";
    } else {
        qWarning() << "[DB] open failed";
        m_db = 0;
    }

    loadAvatarCacheFromDb();
}

ZaloService::~ZaloService() {
    if (m_db) { sqlite3_close(m_db); m_db = 0; }
}

