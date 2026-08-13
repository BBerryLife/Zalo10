#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>

// icon dùng cho cả account tab lẫn từng inbox item — phải nằm trong thư mục
// asset truyền vào uds_register_client() (xem publicAssetPath()/init()).
// File đã có sẵn tại assets/images/PreviewNoti.png, khai báo public="true"
// riêng trong bar-descriptor.xml (asset thường/private không đọc được từ
// Hub, vốn chạy ở process khác).
static const char *HUB_ICON_FILE = "PreviewNoti.png";
static const char *HUB_SERVICE_URL = "com.BerryLife.Zalo10.hub";
// Phải khớp <invoke-target id> trong bar-descriptor.xml — dùng chung target
// với sendHubNotification() hiện có, để tap vào item/account trong Hub mở
// đúng app qua cùng 1 cơ chế invoke đã hoạt động.
static const char *HUB_INVOKE_TARGET = "com.BerryLife.Zalo10.invoke";

HubIntegration::HubIntegration(QObject *parent)
    : QObject(parent), m_udsHandle(0), m_ready(false), m_initAttempted(false)
{
}

HubIntegration::~HubIntegration()
{
    if (m_udsHandle) {
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
    }
}

// __progname: biến toàn cục chuẩn POSIX chứa basename của argv[0] (tên file
// thực thi lúc runtime, ví dụ "Zalo10"). BB10 map "/apps/<progname>/public/..."
// tới đúng thư mục asset public đã cài đặt của app hiện tại — đây là cách
// lấy pAssetPath được khuyến nghị dựa trên kinh nghiệm thực chiến tích hợp
// UDS trên BB10 (nguồn: "Tips for Hub Integration on BlackBerry 10", H.E.C.
// Geek, 2014), thay vì tự dựng path qua QDir::currentPath() — cwd có thể
// không đáng tin cậy tuỳ context process chạy (ví dụ nếu sau này tách ra 1
// headless service riêng thay vì chạy trong process UI chính như hiện tại).
extern char *__progname;

QString HubIntegration::publicAssetPath()
{
    return QString("/apps/%1/public/assets/images/").arg(QString::fromLatin1(__progname));
}

QUrl HubIntegration::hubIconUrl()
{
    return QUrl::fromLocalFile(publicAssetPath() + QLatin1String(HUB_ICON_FILE));
}

bool HubIntegration::init()
{
    if (m_ready) return true;
    if (m_initAttempted) return false; // đã thử và lỗi, không retry mỗi lần gửi tin
    m_initAttempted = true;

    uds_context_t handle = 0;
    int rc = uds_init(&handle, false /* synchronous */);
    if (rc != UDS_SUCCESS || !handle) {
        qDebug() << "[Hub] uds_init failed, rc=" << rc
                  << "- app sẽ tiếp tục dùng bb::platform::Notification thường, "
                     "không có tab riêng trong Hub.";
        return false;
    }
    m_udsHandle = handle;

    QString assetPath = publicAssetPath();

    rc = uds_register_client(m_udsHandle, HUB_SERVICE_URL, "" /* libPath, không dùng */,
                              assetPath.toUtf8().constData());
    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_client failed, rc=" << rc << "assetPath=" << assetPath;
        uds_context_t h = static_cast<uds_context_t>(m_udsHandle);
        uds_close(&h);
        m_udsHandle = 0;
        return false;
    }

    int regStatus = uds_get_service_status(m_udsHandle);
    qDebug() << "[Hub] uds_register_client OK, serviceId=" << uds_get_service_id(m_udsHandle)
              << "status=" << regStatus << "assetPath=" << assetPath;

    uds_account_data_t *account = uds_account_data_create();
    uds_account_data_set_id(account, ACCOUNT_ID);
    uds_account_data_set_name(account, "Zalo10");
    uds_account_data_set_description(account, "Zalo10 messages");
    uds_account_data_set_icon(account, HUB_ICON_FILE);
    uds_account_data_set_target_name(account, HUB_INVOKE_TARGET);
    // false: account này không hỗ trợ tạo tin nhắn mới thẳng từ Hub (chưa
    // có handler cho action "bb.action.CREATE" phía app) — chỉ hiển thị +
    // mở tới thread có sẵn qua sendHubNotification()'s InvokeRequest.
    uds_account_data_set_supports_compose(account, false);
    uds_account_data_set_type(account, UDS_ACCOUNT_TYPE_IM);

    // Hub KHÔNG có API để hỏi "account này đã được add từ phiên trước
    // chưa" — không có query API. uds_get_service_status() chỉ nói lên bản
    // thân việc *đăng ký client* (uds_register_client()) là mới hay cũ,
    // không đảm bảo account thật sự đã tồn tại phía Hub. Cách an toàn
    // (theo kinh nghiệm thực chiến của cộng đồng dev BB10): thử theo đúng
    // thứ tự ưu tiên dựa trên regStatus, và fallback sang thao tác còn lại
    // nếu thao tác đầu tiên fail vì lý do khác hơn là do rớt kết nối.
    if (regStatus == UDS_REGISTRATION_EXISTS) {
        rc = uds_account_updated(m_udsHandle, account);
        if (rc != UDS_SUCCESS) rc = uds_account_added(m_udsHandle, account);
    } else {
        rc = uds_account_added(m_udsHandle, account);
        if (rc != UDS_SUCCESS) rc = uds_account_updated(m_udsHandle, account);
    }
    uds_account_data_destroy(account);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] account add/update failed, rc=" << rc;
        return false;
    }

    qDebug() << "[Hub] Zalo10 account registered in BlackBerry Hub, id=" << ACCOUNT_ID;
    m_ready = true;
    return true;
}

void HubIntegration::upsertThreadItem(const QString &threadId, bool isGroup,
                                       const QString &title, const QString &preview,
                                       qint64 timestampMs)
{
    if (threadId.isEmpty()) return;
    if (!init()) return; // init() tự no-op nếu đã ready; false nghĩa là Hub không khả dụng

    int unread = m_unreadCounts.value(threadId, 0) + 1;
    m_unreadCounts[threadId] = unread;

    QByteArray threadIdUtf8 = threadId.toUtf8();
    QByteArray titleUtf8    = title.toUtf8();
    QByteArray previewUtf8  = preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_icon(item, HUB_ICON_FILE);
    uds_inbox_item_data_set_mime_type(item, "text/plain");
    uds_inbox_item_data_set_timestamp(item, timestampMs);
    uds_inbox_item_data_set_unread_count(item, unread);
    uds_inbox_item_data_set_total_count(item, unread);
    uds_inbox_item_data_set_notification_state(item, true); // tin mới -> cho phép Hub trigger effects/instant preview

    // Không dựa hẳn vào m_knownThreadIds để quyết định add-vs-update: cache
    // này chỉ sống trong bộ nhớ của phiên hiện tại, trong khi item có thể
    // đã tồn tại phía Hub từ phiên trước (app bị kill/restart). Hub không
    // có query API để hỏi trước, nên làm theo pattern "thử update trước —
    // vì đây là trường hợp phổ biến hơn qua nhiều tin nhắn cùng thread —
    // fail thì thử add" là cách an toàn nhất, theo đúng khuyến nghị thực
    // chiến cho thao tác inbox item (thao tác được gọi thường xuyên nhất).
    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS) {
        rc = uds_item_added(m_udsHandle, item);
    }
    if (rc == UDS_SUCCESS) {
        m_knownThreadIds.insert(threadId);
    }
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] upsertThreadItem failed for thread" << threadId
                  << "isGroup=" << isGroup << "rc=" << rc;
    }
}

void HubIntegration::markThreadRead(const QString &threadId)
{
    if (threadId.isEmpty() || !m_ready) return;
    if (m_unreadCounts.value(threadId, 0) == 0) return; // đã 0 sẵn (hoặc chưa từng add), tránh gọi IPC thừa

    m_unreadCounts[threadId] = 0;

    QByteArray threadIdUtf8 = threadId.toUtf8();
    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    uds_inbox_item_data_set_unread_count(item, 0);
    uds_inbox_item_data_set_notification_state(item, false); // chỉ đổi badge, không muốn trigger lại effects

    int rc = uds_item_updated(m_udsHandle, item);
    uds_inbox_item_data_destroy(item);

    if (rc != UDS_SUCCESS) {
        // Không fallback sang add() ở đây: nếu update fail nghĩa là item
        // chưa từng tồn tại phía Hub (user "đọc" 1 thread chưa từng có
        // notification nào gửi lên Hub) — không có gì để mark-read cả,
        // đây không phải lỗi thật sự cần log ồn.
        qDebug() << "[Hub] markThreadRead: item chưa tồn tại hoặc update lỗi cho thread"
                  << threadId << "rc=" << rc;
    }
}

void HubIntegration::removeThreadItem(const QString &threadId)
{
    if (threadId.isEmpty() || !m_ready) return;

    QByteArray threadIdUtf8 = threadId.toUtf8();
    int rc = uds_item_removed(m_udsHandle, ACCOUNT_ID, const_cast<char*>(threadIdUtf8.constData()));
    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] removeThreadItem failed for thread" << threadId << "rc=" << rc;
        return;
    }
    m_knownThreadIds.remove(threadId);
    m_unreadCounts.remove(threadId);
}
