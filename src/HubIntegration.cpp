#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>

// icon account (tab Zalo10 trong Hub) — icon "thương hiệu" chung, không đổi
// theo trạng thái đọc/chưa đọc. Phải nằm trong thư mục asset truyền vào
// uds_register_client() (xem publicAssetPath()/init()). File nằm tại
// assets/public/PreviewNoti.png trong source tree, khai báo public="true"
// riêng trong bar-descriptor.xml — xem comment dài ở đó giải thích vì sao
// thư mục này phải TÁCH RIÊNG khỏi assets/ chung, không được lồng.
static const char *HUB_ICON_FILE = "PreviewNoti.png";
// icon riêng cho từng inbox item, đổi theo trạng thái đọc/chưa đọc — cả 2
// đều phải khai báo public="true" trong bar-descriptor.xml giống
// PreviewNoti.png ở trên, nếu không Hub cũng không đọc được (im lặng dùng
// icon rỗng/mặc định, không báo lỗi).
static const char *HUB_ICON_UNREAD_FILE = "PreviewNotiUNRead.png";
static const char *HUB_ICON_READ_FILE   = "PreviewNotiRead.png";
static const char *HUB_SERVICE_URL = "com.BerryLife.Zalo10.hub";
// Phải khớp <invoke-target id> trong bar-descriptor.xml — dùng chung target
// với sendHubNotification() hiện có, để tap vào item/account trong Hub mở
// đúng app qua cùng 1 cơ chế invoke đã hoạt động.
static const char *HUB_INVOKE_TARGET = "com.BerryLife.Zalo10.invoke";

// Bit context state cho item — PHẢI khớp với context_mask của action "Open
// in Zalo10" (uds_item_action_data_set_context_mask, xem init()). Đây chính
// là mảnh còn thiếu gây ra bug "tap không phản ứng gì cả" (đã xác nhận qua
// đọc doc uds_inbox_item_data_set_context_state(): "the context mask is
// used to query the context state of the inbox item, determining the
// actions that should appear" — nghĩa là item KHÔNG set context_state thì
// Hub không tìm được action nào khớp cho item đó, kể cả action đã đăng ký
// UDS_PLACEMENT_DEFAULT ở cấp account. Trước đây upsertThreadItem()/
// markThreadRead() chưa từng gọi uds_inbox_item_data_set_context_state() —
// so với code mẫu chính thức trong unified_data_source.h (dòng ví dụ
// "uds_inbox_item_data_set_context_state(pInboxItem,Read)"), đây là dòng
// duy nhất bị thiếu so với mẫu chuẩn.
static const unsigned int HUB_CONTEXT_STATE_READ   = 0x01;
static const unsigned int HUB_CONTEXT_STATE_UNREAD = 0x02;

// category_id: field DUY NHẤT còn thiếu so với code mẫu chính thức trong
// unified_data_source.h (dòng ví dụ "uds_inbox_item_data_set_category_id(
// pInboxItem,1)", ngay trước context_state). Doc mô tả field này chỉ để
// "sort/filter theo category (kiểu folder)", KHÔNG có dòng nào nói nó liên
// quan tới preview pane khi short-tap — đây là suy đoán dựa trên "chênh
// lệch duy nhất còn lại so với mẫu chuẩn", KHÔNG phải nguyên nhân đã xác
// nhận chắc chắn. Set = 1 (giá trị mẫu dùng, không có ý nghĩa đặc biệt gì
// khác ngoài "1 category duy nhất cho toàn bộ item Zalo10").
static const long long HUB_CATEGORY_ID = 1;

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
// Đã xác nhận công thức này đúng qua chính log runtime thực tế (uds_register_client
// OK ở đúng path này) VÀ đối chiếu 1 app BB10 thật khác có Hub integration
// (txtmpp, github.com/singpolyma/txtmpp/blob/master/bbui/src/BlackberryHub.hs)
// dùng chung 1 pattern "/apps/<id+hash>/public/<dest>/" y hệt.
extern char *__progname;

QString HubIntegration::publicAssetPath()
{
    // "hubicons" phải khớp CHÍNH XÁC với dest trong bar-descriptor.xml:
    // <asset path="hubicons" public="true">hubicons</asset>
    // Tên KHÔNG được bắt đầu bằng chữ "assets" — Momentics (NDK 10.3.1) có
    // vẻ chặn theo tiền tố tên chuỗi trùng với rule "assets" đã khai báo
    // (từng thử "assets-public" dù là thư mục top-level ngang hàng thật sự,
    // vẫn bị chặn) — xem comment dài trong bar-descriptor.xml.
    return QString("/apps/%1/public/hubicons/").arg(QString::fromLatin1(__progname));
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
    int serviceId = uds_get_service_id(m_udsHandle);
    // So sánh serviceId + status giữa các lần chạy: nếu serviceId đổi mỗi
    // lần build lại (qua Momentics, __progname hash đổi) VÀ status luôn là
    // 1 (UDS_REGISTRATION_NEW, không bao giờ 2=UDS_REGISTRATION_EXISTS),
    // xác nhận Hub coi mỗi build là 1 service hoàn toàn mới — đây là bằng
    // chứng cho nghi vấn "account_id cố định dính state cũ từ service_id
    // trước" ghi trong HubIntegration.hpp (xem comment ACCOUNT_ID). Log lần
    // này dùng ACCOUNT_ID mới (424242006) để so sánh: nếu tap vẫn im lặng
    // dù account_id sạch hoàn toàn, giả thuyết này coi như bị loại.
    qDebug() << "[Hub] uds_register_client OK, serviceId=" << serviceId
              << "status=" << regStatus
              << "(1=NEW 2=EXISTS)"
              << "assetPath=" << assetPath
              << "accountId=" << ACCOUNT_ID;

    // Header UDS chính thức (unified_data_source.h,
    // uds_account_data_set_icon()) ghi 81x81 là kích thước khuyến nghị,
    // nhưng file 72x72 hiện tại (PreviewNoti/Read/UNRead.png) đã hiển thị
    // đúng, cân đối trong Hub trước đây — GIỮ NGUYÊN 72x72, không resize.
    // Đã thử đổi 81x81 và bị lệch/to hơn mong muốn trên thực tế thiết bị —
    // không dùng hướng này. Nghi vấn về kích thước icon coi như bị loại;
    // hướng xử lý còn lại là uds_account_removed() bên dưới.

    // uds_account_removed() trước khi add lại: ép Hub xoá sạch bản ghi
    // account cũ (icon path, v.v.) thay vì chỉ update. Lý do: quan sát
    // thực tế — icon hiện đúng khi build lại qua Momentics (assetPath
    // trùng cache cũ do __progname có suffix testDev_... cố định), nhưng
    // mất icon khi đổi sang bản export (assetPath đổi, __progname khác) dù
    // account_id giữ nguyên. Nghi vấn: uds_account_updated() không luôn
    // buộc Hub re-resolve icon từ assetPath mới. Gọi remove trước xoá khả
    // năng này. Bỏ qua lỗi nếu account chưa tồn tại (bình thường ở lần
    // chạy đầu tiên/cài mới) — không phải điều kiện fail của init().
    {
        int removeRc = uds_account_removed(m_udsHandle, ACCOUNT_ID);
        qDebug() << "[Hub] uds_account_removed (pre-add cleanup) rc=" << removeRc
                  << "(non-zero is expected/harmless if account didn't exist yet)";
    }

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
    // Đã thử UDS_ACCOUNT_TYPE_TEXT_MESSAGE (nghi ngờ ảnh hưởng đến việc Hub
    // có dựng trang preview kiểu "tin nhắn" khi tap hay không) — kết quả:
    // KHÔNG sửa được vụ tap, chỉ đổi thứ tự sắp xếp trong Hub (đứng trên
    // email thay vì dưới, không mong muốn). Trả lại IM như cũ.
    uds_account_data_set_type(account, UDS_ACCOUNT_TYPE_IM);

    // Sau khi đã remove ở trên, account chắc chắn không còn tồn tại phía
    // Hub tại thời điểm này — dùng uds_account_added() làm thao tác chính,
    // fallback sang updated() chỉ để an toàn nếu remove ở trên thất bại vì
    // lý do khác (mất kết nối, v.v.) và account cũ vẫn còn đó.
    rc = uds_account_added(m_udsHandle, account);
    if (rc != UDS_SUCCESS) rc = uds_account_updated(m_udsHandle, account);
    uds_account_data_destroy(account);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] account add/update failed, rc=" << rc;
        return false;
    }

    qDebug() << "[Hub] Zalo10 account registered in BlackBerry Hub, id=" << ACCOUNT_ID;
    m_ready = true;

    // Đăng ký "Open in Zalo10" — item context action, tái tạo đúng dòng
    // "Open in TBBX" thấy trong menu long-press của TBBX (ảnh so sánh).
    //
    // ĐÃ XÁC NHẬN qua đọc trực tiếp unified_data_source.h (header Cascades
    // thật, không phải suy đoán): "Inbox item actions appear in the context
    // menu when a user presses and hold an inbox list item." — nghĩa là
    // action này CHỈ điều khiển menu long-press, KHÔNG liên quan gì đến
    // việc single-tap có mở app hay không. UDS_PLACEMENT_DEFAULT cũng chỉ
    // quyết định vị trí action trong action bar/overflow khi xem account,
    // không phải "default action khi tap 1 item" — nhầm lẫn trước đó ở
    // đúng chỗ này đã được gỡ (xem lịch sử: từng nghi ngờ placement ảnh
    // hưởng single-tap, giờ đã loại trừ).
    //
    // Nguyên nhân thật của vụ short-tap không mở được: xem
    // ApplicationUI::onInvoked() (applicationui.cpp) — mất payload
    // threadId/isGroup khi Hub tự soạn InvokeRequest lúc user tap item
    // (khác hẳn case app tự soạn InvokeRequest, ví dụ banner cũ). Action
    // "Open in Zalo10" ở đây vẫn cần thiết — nó lấp đúng chỗ trống long-press
    // menu (so với TBBX) — nhưng không phải fix cho vụ tap.
    //
    // Đăng ký 1 lần ở cấp account (áp dụng cho MỌI item của account này),
    // không phải per-item.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    // "APPLICATION": target là 1 app thật (khớp <invoke-target-type>application</invoke-target-type>
    // trong bar-descriptor.xml) — không phải "service" hay "card.composer"
    // như 2 ví dụ duy nhất có trong doc (không có ví dụ nào cho trường hợp
    // target là application thường, đây là suy luận theo quy ước, chưa
    // xác nhận được casing/giá trị chính xác).
    uds_item_action_data_set_type(openAction, "APPLICATION");
    uds_item_action_data_set_title(openAction, "Open in Zalo10");
    uds_item_action_data_set_image_source(openAction, HUB_ICON_FILE);
    uds_item_action_data_set_mime_type(openAction, "text/plain");
    uds_item_action_data_set_placement(openAction, UDS_PLACEMENT_DEFAULT);
    // Read=0x01, Unread=0x02 (xem doc uds_item_action_data_set_context_mask)
    // — hiện action này bất kể item đang ở trạng thái đọc hay chưa đọc.
    uds_item_action_data_set_context_mask(openAction, HUB_CONTEXT_STATE_READ | HUB_CONTEXT_STATE_UNREAD);

    int actionRc = uds_register_item_context_action(m_udsHandle, ACCOUNT_ID, openAction);
    uds_item_action_data_destroy(openAction);
    if (actionRc != UDS_SUCCESS) {
        qDebug() << "[Hub] uds_register_item_context_action (Open in Zalo10) failed, rc=" << actionRc
                  << "- item vẫn hiện trong Hub nhưng có thể không mở được khi tap/long-press.";
        // Không return false: account đã đăng ký thành công (m_ready=true),
        // tab Zalo10 vẫn hiển thị bình thường dù thiếu action "Open" —
        // chỉ là 1 hạn chế, không phải lỗi chặn toàn bộ tính năng Hub.
    } else {
        // Trước đây KHÔNG log dòng thành công — nên lần test trước không
        // biết được rc thật sự là gì khi "Open in Zalo10" không hiện (im
        // lặng thành công nhưng không hiện, hay âm thầm fail?). Thêm dòng
        // này để lần test sau biết chắc.
        qDebug() << "[Hub] uds_register_item_context_action (Open in Zalo10) OK";
    }

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
    uds_inbox_item_data_set_icon(item, HUB_ICON_UNREAD_FILE);
    uds_inbox_item_data_set_mime_type(item, "text/plain");
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, timestampMs);
    uds_inbox_item_data_set_unread_count(item, unread);
    uds_inbox_item_data_set_total_count(item, unread);
    // QUAN TRỌNG (xem giải thích đầy đủ ở khai báo HUB_CONTEXT_STATE_* đầu
    // file): thiếu dòng này khiến Hub không tìm được action nào khớp cho
    // item — tap không phản ứng gì cả, kể cả nháy/highlight. Item luôn còn
    // ít nhất 1 tin chưa đọc tại thời điểm gọi hàm này (unread vừa +1 ở
    // trên), nên context_state luôn là Unread ở đây.
    uds_inbox_item_data_set_context_state(item, HUB_CONTEXT_STATE_UNREAD);
    // true: từ giờ item này là NGUỒN DUY NHẤT chịu trách nhiệm cả dòng hiển
    // thị trong Hub lẫn hiệu ứng cảnh báo (banner/sound/lock-screen instant
    // preview) — sendHubNotification() (ZaloService_Messages.cpp) đã BỎ
    // hẳn bb::platform::Notification::notify() song song (từng gây trùng
    // dòng, xem comment ở đó để biết chi tiết tại sao đặt false trước đó
    // KHÔNG giải quyết được vụ trùng dòng: notification_state không quyết
    // định item có hiện dòng hay không — uds_item_added()/uds_item_updated()
    // luôn luôn tạo dòng bất kể cờ này; nó chỉ quyết định có tự bắn thêm
    // cảnh báo hay không). Giờ chỉ còn 1 nguồn nên phải bật true để không
    // mất hẳn banner/sound/instant-preview.
    uds_inbox_item_data_set_notification_state(item, true);

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
        // Lưu lại đầy đủ state vừa gửi — markThreadRead() cần tái tạo lại
        // TOÀN BỘ field này khi update (chỉ đổi icon/unread_count), vì
        // uds_item_updated() thay thế toàn bộ record chứ không patch từng
        // field (xem comment ở struct ThreadItemState trong .hpp).
        ThreadItemState st;
        st.title = title;
        st.preview = preview;
        st.timestampMs = timestampMs;
        st.isGroup = isGroup;
        m_threadItemState[threadId] = st;
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
    if (!m_threadItemState.contains(threadId)) {
        // Chưa từng có state đầy đủ nào được lưu cho thread này (item chưa
        // từng qua upsertThreadItem() thành công) — không có gì để tái tạo
        // đầy đủ, bỏ qua thay vì gửi 1 update thiếu field (sẽ tạo ra đúng
        // bug đã gặp: Hub hiện item với tên/mô tả rỗng, timestamp epoch).
        return;
    }

    m_unreadCounts[threadId] = 0;
    const ThreadItemState &st = m_threadItemState[threadId];

    QByteArray threadIdUtf8 = threadId.toUtf8();
    QByteArray titleUtf8    = st.title.toUtf8();
    QByteArray previewUtf8  = st.preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    // QUAN TRỌNG: uds_item_updated() THAY THẾ TOÀN BỘ record, không patch
    // từng field — phải gửi lại ĐẦY ĐỦ name/description/timestamp/mime_type/
    // total_count y hệt lần upsertThreadItem() gần nhất, chỉ đổi đúng phần
    // muốn thay đổi thật sự (icon: Read thay vì Unread; unread_count: 0;
    // notification_state: false). Thiếu bất kỳ field nào ở đây = Hub hiện
    // item với field đó bị reset rỗng/0 (đã tận mắt thấy: tên rỗng,
    // timestamp về epoch "Thursday, January 1, 1970").
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_mime_type(item, "text/plain");
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_total_count(item, 0);
    uds_inbox_item_data_set_icon(item, HUB_ICON_READ_FILE);
    uds_inbox_item_data_set_unread_count(item, 0);
    // Đổi Unread -> Read (xem giải thích đầy đủ ở khai báo HUB_CONTEXT_STATE_*
    // đầu file) — nếu không đổi, item vẫn giữ context_state=Unread cũ từ lần
    // upsertThreadItem() gần nhất dù unread_count đã về 0 (uds_item_updated()
    // thay thế toàn bộ record, field nào không set lại sẽ mất, không phải
    // "giữ nguyên giá trị cũ").
    uds_inbox_item_data_set_context_state(item, HUB_CONTEXT_STATE_READ);
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
    m_threadItemState.remove(threadId);
}

bool HubIntegration::isGroupThread(const QString &threadId) const
{
    QMap<QString, ThreadItemState>::const_iterator it = m_threadItemState.find(threadId);
    if (it == m_threadItemState.end()) return false; // chưa từng có state -> mặc định DM
    return it.value().isGroup;
}
