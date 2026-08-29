#include "ZaloService.hpp"
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

// Constructor/destructor, hằng số định danh, và helper nhỏ đọc thông tin ảnh.
// Phần lớn logic của ZaloService nằm ở các file ZaloService_*.cpp bên cạnh
// (Auth, WebSocket, Contacts, Messages, Crypto, Network, Db) — xem
// ZaloService.hpp để biết khai báo class đầy đủ.

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
      m_updateReply(0), m_videoDownloadReply(0), m_contactPicker(0),
      m_hub(new HubIntegration(this))
{
    m_qrExpireTimer->setSingleShot(true);
    m_wsReconnectTimer->setSingleShot(true);
    connect(m_qrExpireTimer,    SIGNAL(timeout()), this, SLOT(onQRExpired()));
    connect(m_listenTimer,      SIGNAL(timeout()), this, SLOT(onListenTimer()));
    connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onWsReconnectTimer()));
    connect(m_keepAliveTimer,   SIGNAL(timeout()), this, SLOT(onKeepAliveTimer()));
    qsrand((uint)QDateTime::currentMSecsSinceEpoch());
    qDebug() << "[Zalo] ZaloService initialized";
    // Đăng ký account Hub ngay lúc khởi động (không đợi tin nhắn đầu tiên) —
    // để tab "Zalo10" xuất hiện trong Hub ngay cả khi chưa có tin nhắn mới,
    // giống TBBX/BBM luôn hiện sẵn. init() tự no-op an toàn nếu UDS lỗi.
    m_hub->init();

    // Bật mặc định Instant Preview = Allow cho app, KHÔNG bắt user tự vào
    // Settings > Application Settings > Zalo10 tự bật tay.
    //
    // Vì sao cần: NotificationDialog::show() (sendBannerNotification() ở
    // ZaloService_Messages.cpp) chỉ *trigger* hiệu ứng — banner/peek có thực
    // sự hiện lên màn hình hay không do chính sách "preview" trong
    // Notification Settings của app quyết định (xem
    // bb::platform::NotificationDialog: "The notification settings determine
    // whether and where the title/body is shown"). Với app có Hub account
    // (Zalo10 luôn có, từ HubIntegration::init() ở trên), default preview
    // policy của hệ thống không đảm bảo là Allow — nếu là Deny/PriorityOnly
    // thì banner sẽ không hiện dù Hub item vẫn tạo bình thường.
    //
    // NotificationDefaultApplicationSettings::apply() CHỈ có tác dụng 1 lần
    // duy nhất — "will only be applied if the default settings haven't been
    // modified already" (xem NotificationDefaultApplicationSettings.hpp).
    // Nghĩa là: nếu user đã tự tay đổi setting này trong Settings UI (dù
    // trước hay sau lần gọi này), gọi lại apply() ở đây sẽ KHÔNG ghi đè lựa
    // chọn của họ — đây là hành vi mong muốn, không phải bug. Gọi mỗi lần
    // khởi động là an toàn và rẻ (no-op nếu đã set).
    //
    // Cùng lúc set luôn tonePath mặc định thay vì dùng "Essential" chung
    // của hệ thống. setTonePath() yêu cầu "a file URI to a public asset or
    // a shared asset on the device" (NotificationDefaultApplicationSettings.hpp)
    // — present.m4a ở /usr/share/sounds/notification-tones/bbm/ là tone
    // nằm trong kho âm thanh hệ thống dùng chung (chính là danh sách mà
    // Settings > Notification Sound liệt kê cho user chọn), không phải
    // asset sandbox riêng của app BBM, nên app khác trỏ file URI thẳng vào
    // đây là hợp lệ.
    {
        bb::platform::NotificationDefaultApplicationSettings defaultSettings;
        defaultSettings.setPreview(bb::platform::NotificationPriorityPolicy::Allow);
        defaultSettings.setTonePath(QUrl("file:///usr/share/sounds/notification-tones/bbm/present.m4a"));
        bb::platform::NotificationSettingsError::Type err = defaultSettings.apply();
        qDebug() << "[Zalo] NotificationDefaultApplicationSettings preview=Allow, tonePath=present.m4a, apply() result:" << err;
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
        // Migrations for existing DBs
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN msgType    INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN localImage TEXT    DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgWidth   INTEGER DEFAULT 0;",  0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN imgHeight  INTEGER DEFAULT 0;",  0, 0, 0);
        // cliMsgId: id tin nhắn do client tự sinh, được Zalo echo lại qua
        // WS/HTTP. API xóa/thu hồi cần đúng field này (không phải msgId).
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN cliMsgId   TEXT    DEFAULT '';", 0, 0, 0);
        // Giữ lại nội dung gốc của tin nhắn đã thu hồi, để hiện lại (kèm
        // tag) khi bật "Show Recalled Messages" trong Settings.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN recalledOriginalContent TEXT DEFAULT '';", 0, 0, 0);
        // Reply/quote: lưu đủ thông tin tin nhắn được quote để render preview
        // strip trong bubble reply (tên người gửi + snippet nội dung) mà
        // không cần query DB lần 2 lúc render. quoteMsgId dùng để tap-to-jump.
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgId     TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteContent   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteSenderName TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteMsgType   INTEGER DEFAULT 0;", 0, 0, 0);
        // uid của người gửi tin nhắn được quote ("" nếu không rõ) — để phân
        // biệt "đang reply chính mình" hay "reply người khác".
        sqlite3_exec(m_db, "ALTER TABLE messages ADD COLUMN quoteOwnerId   TEXT DEFAULT '';", 0, 0, 0);
        sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_thread ON messages(threadId,ts);", 0, 0, 0);
        // Mỗi row là 1 reaction của 1 người trên 1 tin nhắn — PRIMARY
        // KEY(msgId,uid) nên INSERT OR REPLACE tự động "đổi reaction ghi
        // đè reaction cũ" (mỗi người chỉ có 1 reaction active/tin nhắn),
        // DELETE thì xóa hẳn khi un-react. threadId lưu kèm để
        // dbLoadThreadReactions() query 1 lần cho cả thread.
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
        // Ép tên command duy nhất (không phân biệt hoa/thường) ở tầng DB —
        // addQuickMessage/updateQuickMessage dựa vào SQLITE_CONSTRAINT để
        // biết bị trùng, không cần SELECT check riêng.
        sqlite3_exec(m_db, "CREATE UNIQUE INDEX IF NOT EXISTS idx_qm_name ON quick_messages(name COLLATE NOCASE);", 0, 0, 0);
        // Metadata avatar persistent: sống qua cả restart app và logout/login,
        // chỉ clearCache() mới xóa. Map threadId -> md5 của URL avatar đã
        // tải + path file local. Mỗi lần downloadAvatar() so hash URL mới
        // với urlHash cũ; trùng thì bỏ qua tải lại, khác thì tải và ghi đè
        // cùng 1 file (avatar_<md5(threadId)>.jpg — cố định theo người,
        // không theo URL, nên đổi avatar không để lại file rác).
        sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS avatar_meta ("
            "  threadId  TEXT PRIMARY KEY,"
            "  urlHash   TEXT NOT NULL,"
            "  localPath TEXT NOT NULL,"
            "  updatedAt TEXT"
            ");", 0, 0, 0);
        // Tombstone các msgId đã bị xóa cứng qua "delete for me". Delete-for-me
        // không xóa ở server, nên lần history re-sync sau (mở lại thread, hoặc
        // restart app) có thể trả tin đã xóa về lại — mọi msgId ở đây phải bị
        // dbSaveMessage() bỏ qua vĩnh viễn dù server có gửi lại bao nhiêu lần.
        // Không bị clearCache() xóa (đây là lựa chọn của user, không phải cache).
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

    // Log số avatar cache còn trên đĩa từ phiên trước — chỉ để chẩn đoán,
    // logic dùng lại thật sự nằm ở downloadAvatar().
    loadAvatarCacheFromDb();
}

ZaloService::~ZaloService() {
    if (m_db) { sqlite3_close(m_db); m_db = 0; }
}

