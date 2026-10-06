#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>
#include <QDir>
#include <QStringList>
#include <QSettings>

// icon account (tab Zalo10 trong Hub) — icon "thương hiệu" chung, không đổi
// theo trạng thái đọc/chưa đọc. Phải nằm trong thư mục asset truyền vào
// uds_register_client() (xem publicAssetPath()/init()). File nằm tại
// assets/public/ic_hub.png trong source tree, khai báo public="true"
// riêng trong bar-descriptor.xml — xem comment dài ở đó giải thích vì sao
// thư mục này phải TÁCH RIÊNG khỏi assets/ chung, không được lồng.
static const char *HUB_ICON_FILE = "ic_hub.png";
// icon riêng cho từng inbox item, đổi theo trạng thái đọc/chưa đọc — cả 2
// đều phải khai báo public="true" trong bar-descriptor.xml giống
// ic_hub.png ở trên, nếu không Hub cũng không đọc được (im lặng dùng
// icon rỗng/mặc định, không báo lỗi).
static const char *HUB_ICON_UNREAD_FILE = "ic_hub_unread.png";
static const char *HUB_ICON_READ_FILE   = "ic_hub_read.png";
// Icon của các item context action (long-press menu). Cùng thư mục res_hub/.
static const char *HUB_ICON_MARK_OPENED_FILE   = "ic_mark_opened.png";
static const char *HUB_ICON_MARK_UNOPENED_FILE = "ic_mark_unopened.png";
// Mark read / Mark unread xử lý ở HEADLESS SERVICE (không bật
// UI lên foreground). "Open in Zalo10" vẫn trỏ vào HUB_INVOKE_TARGET (UI).
static const char *HUB_SERVICE_TARGET = "com.BerryLife.Zalo10.service";
static const char *HUB_SERVICE_URL = "com.BerryLife.Zalo10.hub";

// ===== SHORT-TAP MỞ ITEM TRONG HUB (đã sửa, theo BBCord/Beeper10) =====
//
// Nguyên nhân short-tap im lặng (log không có cả "onInvoked() ENTERED") là
// do Hub không tự khớp được item -> <invoke-target>: mime type chung chung
// ("text/plain"/"plain/message") + filter thiếu property "uris". Cấu hình
// đúng (đã chạy thật ở BBCord, đối chiếu từ Beeper10):
//   1. mime type RIÊNG của app, dùng thống nhất cho item + item context
//      action + <filter> trong bar-descriptor.xml.
//   2. <filter> có <property var="uris" value="pim:<mime type>"/>.
//   3. Item action UDS_PLACEMENT_DEFAULT; account target_name = id của
//      <invoke-target> (không phải app id).
// Long-press ("Open in Zalo10") dùng chính target này qua action context.
static const char *HUB_INVOKE_TARGET = "com.BerryLife.Zalo10.invoke";
// PHẢI khớp bar-descriptor.xml (<mime-type> và pim: uris).
static const char *HUB_MIME_TYPE_MESSAGE = "application/vnd.zalo10.hub.chat";

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
// Mỗi item mang đúng 1 bit READ hoặc UNREAD. Action chỉ hiện khi context_mask
// của nó trùng ít nhất 1 bit với context_state của item.
//   Mark read : mask UNREAD     Mark unread : mask READ

// Tên action Hub gửi tới invoke-target cho các lệnh item. Phải khớp
// <action> trong bar-descriptor.xml.
const char *HubIntegration::ACTION_OPEN        = "bb.action.OPEN";
const char *HubIntegration::ACTION_MARK_READ   = "bb.action.MARKREAD";
const char *HubIntegration::ACTION_MARK_UNREAD = "bb.action.MARKUNREAD";
const char *HubIntegration::ACTION_DELETE      = "bb.action.DELETE";

bool HubIntegration::isItemAction(const QString &action)
{
    return action == QLatin1String(ACTION_MARK_READ)
        || action == QLatin1String(ACTION_MARK_UNREAD)
        || action == QLatin1String(ACTION_DELETE);
}

unsigned int HubIntegration::contextStateFor(bool unread)
{
    return unread ? HUB_CONTEXT_STATE_UNREAD : HUB_CONTEXT_STATE_READ;
}

// Đăng ký 1 item context action (menu long-press / select more).
static int registerItemAction(void *udsHandle, long long accountId, const char *action,
                              const char *target, const char *icon,
                              const char *title, unsigned int contextMask,
                              uds_placement_type_t placement)
{
    uds_item_action_data_t *a = uds_item_action_data_create();
    uds_item_action_data_set_action(a, action);
    uds_item_action_data_set_target(a, target);
    uds_item_action_data_set_type(a, "service");
    uds_item_action_data_set_title(a, title);
    uds_item_action_data_set_image_source(a, icon);
    uds_item_action_data_set_mime_type(a, HUB_MIME_TYPE_MESSAGE);
    uds_item_action_data_set_placement(a, placement);
    uds_item_action_data_set_context_mask(a, contextMask);
    int rc = uds_register_item_context_action(static_cast<uds_context_t>(udsHandle), accountId, a);
    uds_item_action_data_destroy(a);
    qDebug() << "[Hub] register item action" << action << title << "rc=" << rc;
    return rc;
}

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
// thực thi lúc runtime). Ý TƯỞNG BAN ĐẦU (SAI, xem FIX LẦN 11 bên dưới):
// BB10 map "/apps/<progname>/public/..." tới đúng thư mục asset public đã
// cài đặt của app hiện tại — dựa theo "Tips for Hub Integration on
// BlackBerry 10", H.E.C. Geek, 2014, và đối chiếu txtmpp
// (github.com/singpolyma/txtmpp/blob/master/bbui/src/BlackberryHub.hs)
// dùng pattern "/apps/<id+hash>/public/<dest>/". CÔNG THỨC ĐÚNG cho build
// Debug (nơi __progname tình cờ gần giống phần đầu app-id thật, "Zalo10"),
// nhưng SAI cho build Release: entry point Release trong bar-descriptor.xml
// là "Zalo10.so" (Qnx/Cascades type), nên __progname trả về "Zalo10.so" —
// khác hẳn app-id thật trên filesystem
// ("com.BerryLife.Zalo10.testRel_Life_Zalo108ff545d2", xác nhận qua Target
// File System Navigator thực tế trên thiết bị, 2026-08-30). Đây chính là
// nguyên nhân gốc của bug "icon blank khi cài bản .bar" — không phải do
// UDS/Hub cache gì cả (đã loại trừ ở Fix Lần 10: remove-before-add chạy
// đúng, rc=0, nhưng icon vẫn blank vì bản thân assetPath TÍNH SAI, trỏ tới
// 1 thư mục "/apps/Zalo10.so/public/res_hub/" không hề tồn tại, trong khi
// file thật nằm ở "/apps/<app-id-that>/public/res_hub/").
extern char *__progname;

// ===== FIX LẦN 11 — LẤY APP-ID TỪ QDir::homePath() THAY VÌ __progname =====
// QDir::homePath() trên BB10 trả về
// "/accounts/1000/appdata/<app-id-that>/data" — đã XÁC NHẬN qua log thực tế
// (dòng "[Zalo] SQLite DB opened") khớp ĐÚNG 100% với app-id thật thấy trên
// Target File System Navigator, ở CẢ HAI build (Debug lẫn Release), không
// như __progname chỉ đúng tình cờ ở Debug. ZaloService.cpp cũng đang dùng
// QDir::homePath() cho DB path — cùng API, đã chứng minh đáng tin cậy qua
// nhiều lần chạy thực tế trên thiết bị.
//
// Cách lấy: tách "/accounts/1000/appdata/<app-id>/data" theo dấu "/", lấy
// phần tử ngay trước "data" (phần tử cuối cùng của homePath). Có fallback
// về __progname (công thức cũ) nếu vì lý do gì đó homePath() không đúng
// định dạng mong đợi (ví dụ đổi hệ điều hành/thay đổi convention BB10 sau
// này) — không bao giờ để publicAssetPath() trả về chuỗi rỗng hay crash.
static QString appIdFromHomePath()
{
    // homePath() dạng "/accounts/1000/appdata/<app-id>/data" — QDir tự
    // chuẩn hoá dấu "/" nên split đơn giản theo "/" là đủ, không cần lo
    // dấu "/" kép hay ký tự đặc biệt khác trên BB10.
    QStringList parts = QDir::homePath().split(QLatin1Char('/'), QString::SkipEmptyParts);
    // parts cuối cùng phải là "data" (theo đúng pattern quan sát được qua
    // log), phần tử NGAY TRƯỚC đó là app-id cần lấy.
    if (parts.size() >= 2 && parts.last() == QLatin1String("data")) {
        QString appId = parts.at(parts.size() - 2);
        qDebug() << "[Hub] appId tu homePath =" << appId << "(homePath=" << QDir::homePath() << ")";
        return appId;
    }
    // Fallback: homePath() không đúng dạng mong đợi -> dùng __progname như
    // công thức cũ, thà sai path còn hơn crash hoàn toàn (Hub integration
    // là tính năng cộng thêm, không được phép làm app hỏng chức năng khác).
    qDebug() << "[Hub] homePath khong dung dang mong doi (" << QDir::homePath()
              << "), fallback ve __progname cho publicAssetPath().";
    return QString::fromLatin1(__progname);
}

QString HubIntegration::publicAssetPath()
{
    // "res_hub" phải khớp CHÍNH XÁC với dest trong bar-descriptor.xml:
    // <asset path="res_hub" public="true">res_hub</asset>
    // Tên KHÔNG được bắt đầu bằng chữ "assets" — Momentics (NDK 10.3.1) có
    // vẻ chặn theo tiền tố tên chuỗi trùng với rule "assets" đã khai báo
    // (từng thử "assets-public" dù là thư mục top-level ngang hàng thật sự,
    // vẫn bị chặn) — xem comment dài trong bar-descriptor.xml.
    return QString("/apps/%1/public/res_hub/").arg(appIdFromHomePath());
}

QUrl HubIntegration::hubIconUrl()
{
    return QUrl::fromLocalFile(publicAssetPath() + QLatin1String(HUB_ICON_FILE));
}

// ===== FIX LẦN 10 — BỎ CƠ CHẾ CACHE FILE ASSETPATH (SAI HƯỚNG), QUAY LẠI
// REMOVE-BEFORE-ADD MỖI LẦN KHỞI ĐỘNG =====
//
// Fix Lần 9.5 (thử trước, đã revert): lưu assetPath vào file dưới
// QDir::homePath() để so sánh giữa các lần init(), chỉ remove-before-add
// khi phát hiện đổi. SAI HƯỚNG — đã xác nhận qua log thực tế 2 lần chạy
// (Momentics run vs .bar install): QDir::homePath() bản thân nó CŨNG đổi
// theo build, vì mỗi biến thể build (debug/testDev vs release/testRel) có
// sandbox filesystem RIÊNG BIỆT trên BB10
// ("/accounts/1000/appdata/<app-id-hash>/data/", app-id-hash chứa
// "testDev_..." hay "testRel_..." tuỳ build — thấy rõ qua dòng log "SQLite
// DB opened" của 2 lần chạy khác nhau). Nghĩa là file cache ghi ở build A
// nằm trong sandbox A, build B đọc từ sandbox B hoàn toàn khác — không bao
// giờ đọc được cache của build trước. Cơ chế so sánh vô nghĩa, đã loại bỏ.
//
// Về giả thuyết gốc của Fix Lần 9 (remove-before-add phá single-tap): test
// thực tế trên thiết bị (2026-08-30) cho thấy single-tap KHÔNG hoạt động ở
// CẢ HAI trường hợp — cả bản Fix Lần 9 (không remove, chỉ update-first) lẫn
// khi chạy qua Momentics (nơi trước đây nghi ngờ remove liên tục là thủ
// phạm). Tức là thiếu bằng chứng remove-before-add THỰC SỰ là nguyên nhân
// gây bug single-tap — 2 bug (single-tap, icon blank khi đổi build) nhiều
// khả năng ĐỘC LẬP với nhau, không phải cùng 1 nguyên nhân. Vì vậy quay lại
// remove-before-add mỗi lần khởi động là an toàn để ưu tiên fix bug icon
// (có bằng chứng rõ ràng, dễ tái hiện), không có rủi ro thêm cho single-tap
// vì single-tap đang hỏng ở cả 2 cách. Nếu sau này single-tap được fix bằng
// hướng khác (không liên quan uds_account_removed), quay lại kiểm tra xem
// remove-before-add có ảnh hưởng gì không lúc đó.

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
    // nhưng file 72x72 hiện tại (ic_hub*.png) đã hiển thị
    // đúng, cân đối trong Hub trước đây — GIỮ NGUYÊN 72x72, không resize.
    // Đã thử đổi 81x81 và bị lệch/to hơn mong muốn trên thực tế thiết bị —
    // không dùng hướng này. Nghi vấn về kích thước icon coi như bị loại.
    // (Trước đây "hướng xử lý còn lại" ở đây là uds_account_removed() mỗi
    // lần khởi động — đã BỎ ở Fix Lần 9, RỒI THÊM LẠI ở Fix Lần 10 bên dưới
    // sau khi Fix Lần 9 không giải quyết được gì và gây bug icon quay lại.
    // Xem giải thích ở khối FIX LẦN 9 ngay dưới, và FIX LẦN 10 sau nó.)

    // ===== FIX LẦN 9 (ĐÃ TEST — KHÔNG FIX ĐƯỢC SINGLE-TAP, GÂY BUG ICON
    // QUAY LẠI, ĐÃ SUPERSEDE BỞI FIX LẦN 10 BÊN DƯỚI) =====
    // Giữ lại đoạn comment gốc để tham khảo lý do/giả thuyết ban đầu —
    // KHÔNG còn áp dụng, code thực thi đã đổi sang Fix Lần 10.
    // Đọc lại kỹ blog H.E.C. Geek (đối chiếu nguồn thực chiến duy nhất còn
    // tồn tại về hub integration BB10), mục "Create/Update Operations":
    // "the hub has no query API... The safest way to deal with this is to
    // implement a pattern that will either add or update depending on the
    // result of the corresponding operation... the typical pattern is to
    // TRY UPDATING FIRST, and if that fails..., try adding instead."
    //
    // Code trước đây làm NGƯỢC hoàn toàn cho account: gọi
    // uds_account_removed() ÉP XOÁ account MỖI LẦN app khởi động (để fix 1
    // bug icon khác — xem lịch sử git), rồi luôn add lại từ đầu. Nghi ngờ
    // mới: hành vi remove+add liên tục mỗi lần chạy có thể đang liên tục
    // phá vỡ liên kết nội bộ mà Hub cần để route single-tap → invoke —
    // trong khi category/item action đăng ký NGAY SAU đó vẫn "trông" đúng
    // (API trả UDS_SUCCESS) vì UDS không validate sâu, đúng như blog cảnh
    // báo ("these items got added in a way that they're not associated
    // with any account... may still trigger invoke to open them [long-
    // press vẫn qua được] but..." — có thể áp dụng tương tự cho link tap-
    // to-open dù blog không nói thẳng trường hợp này). Đổi sang đúng
    // pattern blog khuyến nghị: update trước, add chỉ khi update fail.
    // Bỏ hẳn remove-before-add. Rủi ro đã biết: bug icon (mất icon khi
    // build export do assetPath đổi) có thể quay lại — nếu vậy, xử lý
    // riêng bằng cách khác (ví dụ so sánh assetPath cũ/mới) thay vì remove
    // toàn bộ account mỗi lần.

    // FIX LẦN 10: remove-before-add mỗi lần khởi động, KHÔNG điều kiện — xem
    // giải thích đầy đủ ở khối comment "FIX LẦN 10" phía trên hàm này (vì
    // sao bỏ cơ chế cache-so-sánh của Fix Lần 9.5, và vì sao giả thuyết
    // "remove phá single-tap" thiếu bằng chứng ở thời điểm này). Đảm bảo
    // icon luôn được Hub re-resolve từ assetPath hiện tại của build đang
    // chạy, bất kể build trước đó (nếu có) dùng __progname/sandbox nào.
    int removeRc = uds_account_removed(m_udsHandle, ACCOUNT_ID);
    qDebug() << "[Hub] uds_account_removed (pre-add cleanup, Fix Lan 10) rc=" << removeRc
              << "(bo qua neu account chua tung ton tai / lan cai dat dau tien)";

    uds_account_data_t *account = uds_account_data_create();
    uds_account_data_set_id(account, ACCOUNT_ID);
    uds_account_data_set_name(account, "Zalo10");
    uds_account_data_set_description(account, "Zalo10 messages");
    uds_account_data_set_icon(account, HUB_ICON_FILE);
    // target_name = id của <invoke-target> (HUB_INVOKE_TARGET), giống BBCord.
    // Đổi sang SERVICE: các nút tích hợp sẵn của Hub (Delete thùng rác, select
    // more -> Mark read/unread) luôn invoke target của account; trỏ vào UI
    // thì app bị bật lên. Service nhận rồi tự chuyển tiếp OPEN/VIEW sang UI.
    uds_account_data_set_target_name(account, HUB_SERVICE_TARGET);
    // false: account này không hỗ trợ tạo tin nhắn mới thẳng từ Hub (chưa
    // có handler cho action "bb.action.CREATE" phía app) — chỉ hiển thị +
    // mở tới thread có sẵn qua sendHubNotification()'s InvokeRequest.
    uds_account_data_set_supports_compose(account, false);
    // Đã thử UDS_ACCOUNT_TYPE_TEXT_MESSAGE (nghi ngờ ảnh hưởng đến việc Hub
    // có dựng trang preview kiểu "tin nhắn" khi tap hay không) — kết quả:
    // KHÔNG sửa được vụ tap, chỉ đổi thứ tự sắp xếp trong Hub (đứng trên
    // email thay vì dưới, không mong muốn). Trả lại IM như cũ.
    uds_account_data_set_type(account, UDS_ACCOUNT_TYPE_IM);

    // Vừa remove ở trên (Fix Lần 10) nên account chắc chắn không còn tồn
    // tại -> add thẳng, không cần thử update trước nữa (update sẽ luôn fail
    // ngay sau remove, gọi thêm chỉ tốn 1 lượt IPC không cần thiết).
    rc = uds_account_added(m_udsHandle, account);
    uds_account_data_destroy(account);

    if (rc != UDS_SUCCESS) {
        qDebug() << "[Hub] account add failed, rc=" << rc;
        return false;
    }

    qDebug() << "[Hub] Zalo10 account registered in BlackBerry Hub, id=" << ACCOUNT_ID;
    m_ready = true;

    // uds_category_added() phải chạy SAU account_added() và TRƯỚC mọi
    // uds_item_added() dùng category_id này (đúng thứ tự trong
    // unified_data_source.h). Đăng ký 1 lần trong init().
    {
        uds_category_data_t *category = uds_category_data_create();
        uds_category_data_set_id(category, HUB_CATEGORY_ID);
        uds_category_data_set_account_id(category, ACCOUNT_ID);
        uds_category_data_set_name(category, "Zalo10");
        // parent_id: không có category cha (category gốc/duy nhất của
        // account này) — không gọi set_parent_id, để mặc định.
        int categoryRc = uds_category_added(m_udsHandle, category);
        if (categoryRc != UDS_SUCCESS) categoryRc = uds_category_updated(m_udsHandle, category);
        uds_category_data_destroy(category);
        qDebug() << "[Hub] uds_category_added rc=" << categoryRc
                  << "id=" << HUB_CATEGORY_ID;
    }

    // Đăng ký "Open in Zalo10" — item context action (long-press), áp dụng
    // cho MỌI item của account (đăng ký 1 lần ở cấp account, không per-item).
    // Mime type/placement/target phải theo đúng cấu hình ghi ở đầu file để
    // short-tap và long-press cùng resolve được tới <invoke-target>.
    uds_item_action_data_t *openAction = uds_item_action_data_create();
    uds_item_action_data_set_action(openAction, "bb.action.OPEN");
    uds_item_action_data_set_target(openAction, HUB_INVOKE_TARGET);
    // "service": doi tu "APPLICATION" (suy doan sai truoc day, khong ton
    // tai trong header unified_data_source.h chinh thuc — da doi chieu truc
    // tiep). Header chi liet ke DUY NHAT 2 gia tri hop le cho targetType:
    // "card.composer" (target la 1 Compose card) va "service" (moi truong
    // hop khac, ke ca khi target la 1 application thuong nhu truong hop
    // nay) — vi du mau chinh thuc dung dung cap "service" + target la ten
    // app ("UDSTestApp") y het cau truc HUB_INVOKE_TARGET o day. UDS chi
    // validate cu phap luc register (khong bao gio fail voi type sai), nen
    // "APPLICATION" truoc day van tra ve UDS_SUCCESS binh thuong dua den
    // nham lan — loi chi lo ra luc THUC SU invoke (Hub khong biet cach
    // dung target voi 1 type khong ton tai, single-tap im lang khong lam
    // gi ca, dung trieu chung da quan sat duoc qua nhieu lan test).
    uds_item_action_data_set_type(openAction, "service");
    uds_item_action_data_set_title(openAction, "Open in Zalo10");
    uds_item_action_data_set_image_source(openAction, HUB_ICON_FILE);
    // Cùng mime type riêng với item để long-press và short-tap resolve giống nhau.
    uds_item_action_data_set_mime_type(openAction, HUB_MIME_TYPE_MESSAGE);
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

    // Mark read / Mark unread — hiện trong menu
    // long-press cùng chỗ với "Open in Zalo10" và là các action mà "select
    // more" của Hub cũng dùng. Thiếu đăng ký này thì Hub không có gì để
    // invoke khi user chọn các lệnh đó (trước đây nút có nhưng không tác dụng).
    registerItemAction(m_udsHandle, ACCOUNT_ID, ACTION_MARK_READ, HUB_SERVICE_TARGET,
                       HUB_ICON_MARK_OPENED_FILE, "Mark Read",
                       HUB_CONTEXT_STATE_UNREAD, UDS_PLACEMENT_DEFAULT);
    registerItemAction(m_udsHandle, ACCOUNT_ID, ACTION_MARK_UNREAD, HUB_SERVICE_TARGET,
                       HUB_ICON_MARK_UNOPENED_FILE, "Mark Unread",
                       HUB_CONTEXT_STATE_READ, UDS_PLACEMENT_DEFAULT);
    // KHÔNG đăng ký "Delete": Hub đã có nút Delete (thùng rác) riêng. Nếu Hub
    // invoke bb.action.DELETE tới target thì handleHubAction() vẫn xử lý.

    return true;
}

void HubIntegration::upsertThreadItem(const QString &threadId, bool isGroup,
                                       const QString &title, const QString &preview,
                                       qint64 timestampMs)
{
    if (threadId.isEmpty()) return;
    if (!init()) return; // init() tự no-op nếu đã ready; false nghĩa là Hub không khả dụng

    // Nạp state bền (nếu process này chưa từng thấy thread) để cộng dồn unread
    // qua restart / giữa UI và service.
    // Đọc lại từ đĩa (bỏ bản trong RAM): service và UI là 2 process, action
    // Mark read/unread có thể vừa được service xử lý.
    m_threadItemState.remove(threadId);
    m_unreadCounts.remove(threadId);
    ensureState(threadId);
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
    // Mime type riêng của app — phải khớp bar-descriptor.xml và item action
    // (xem giải thích ở đầu file).
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, timestampMs);
    uds_inbox_item_data_set_unread_count(item, unread);
    uds_inbox_item_data_set_total_count(item, unread);
    // QUAN TRỌNG (xem giải thích đầy đủ ở khai báo HUB_CONTEXT_STATE_* đầu
    // file): thiếu dòng này khiến Hub không tìm được action nào khớp cho
    // item — tap không phản ứng gì cả, kể cả nháy/highlight. Item luôn còn
    // ít nhất 1 tin chưa đọc tại thời điểm gọi hàm này (unread vừa +1 ở
    // trên), nên context_state luôn là Unread ở đây.
    uds_inbox_item_data_set_context_state(item, contextStateFor(true));
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
        st.unread = unread;
        st.total  = unread;
        m_threadItemState[threadId] = st;
        saveState(threadId);
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
    if (!ensureState(threadId)) return; // chưa từng có item cho thread này
    if (m_threadItemState[threadId].unread == 0 && m_unreadCounts.value(threadId, 0) == 0)
        return; // đã đọc sẵn, tránh gọi IPC thừa

    m_unreadCounts[threadId] = 0;
    m_threadItemState[threadId].unread = 0;
    saveState(threadId);

    // Không fallback add: nếu item không còn phía Hub thì không có gì để
    // đánh dấu đọc. publishState() gửi lại ĐẦY ĐỦ field (uds_item_updated()
    // thay thế cả record), chỉ đổi icon/unread/context_state.
    if (!publishState(threadId, false /* notify */, false /* allowAdd */)) {
        qDebug() << "[Hub] markThreadRead: item chưa tồn tại hoặc update lỗi cho thread" << threadId;
    }
}

void HubIntegration::markThreadUnread(const QString &threadId)
{
    if (threadId.isEmpty()) return;
    if (!ensureState(threadId)) {
        qDebug() << "[Hub] markThreadUnread: không có state cho thread" << threadId;
        return;
    }
    if (m_threadItemState[threadId].unread < 1) m_threadItemState[threadId].unread = 1;
    if (m_threadItemState[threadId].total  < 1) m_threadItemState[threadId].total  = 1;
    m_unreadCounts[threadId] = m_threadItemState[threadId].unread;
    saveState(threadId);
    // Cập nhật TẠI CHỖ (giống Mark Read, chỉ đổi icon/unread/context_state).
    // Hub tự phát âm báo khi item update làm total_count TĂNG (coi như có tin
    // mới), bất kể notification_state=false: trước đây Mark Read đặt total=0
    // nên Mark Unread (0 -> 1) kêu present.m4a. Giờ total_count giữ nguyên >=1
    // (xem publishState) nên chỉ unread_count đổi 0 <-> 1, giống đánh dấu
    // đọc/chưa đọc của mail. Gỡ+add lại từng thử: hết kêu nhưng item hiện như
    // tin mới nên bỏ.
    publishState(threadId, false /* notify: không phát âm */, true);
}

bool HubIntegration::ensureState(const QString &threadId)
{
    if (m_threadItemState.contains(threadId)) return true;
    QSettings s("BerryLife", "Zalo10");
    s.sync(); // lấy thay đổi do process kia (UI <-> service) vừa ghi
    s.beginGroup("hubItems");
    s.beginGroup(threadId);
    if (!s.contains("title")) return false;
    ThreadItemState st;
    st.title       = s.value("title").toString();
    st.preview     = s.value("preview").toString();
    st.timestampMs = s.value("ts").toLongLong();
    st.isGroup     = s.value("group", false).toBool();
    st.unread      = s.value("unread", 0).toInt();
    st.total       = s.value("total", qMax(st.unread, 1)).toInt();
    m_threadItemState[threadId] = st;
    if (!m_unreadCounts.contains(threadId)) m_unreadCounts[threadId] = st.unread;
    return true;
}

void HubIntegration::saveState(const QString &threadId)
{
    if (!m_threadItemState.contains(threadId)) return;
    const ThreadItemState &st = m_threadItemState[threadId];
    QSettings s("BerryLife", "Zalo10");
    s.beginGroup("hubItems");
    s.beginGroup(threadId);
    s.setValue("title", st.title);
    s.setValue("preview", st.preview);
    s.setValue("ts", st.timestampMs);
    s.setValue("group", st.isGroup);
    s.setValue("unread", st.unread);
    s.setValue("total", st.total);
}

void HubIntegration::forgetState(const QString &threadId)
{
    QSettings s("BerryLife", "Zalo10");
    s.beginGroup("hubItems");
    s.remove(threadId);
    m_knownThreadIds.remove(threadId);
    m_unreadCounts.remove(threadId);
    m_threadItemState.remove(threadId);
}

bool HubIntegration::publishState(const QString &threadId, bool notify, bool allowAdd)
{
    if (!init()) return false;
    if (!ensureState(threadId)) return false;
    const ThreadItemState st = m_threadItemState[threadId];

    QByteArray threadIdUtf8 = threadId.toUtf8();
    QByteArray titleUtf8    = st.title.toUtf8();
    QByteArray previewUtf8  = st.preview.toUtf8();

    uds_inbox_item_data_t *item = uds_inbox_item_data_create();
    uds_inbox_item_data_set_account_id(item, ACCOUNT_ID);
    uds_inbox_item_data_set_source_id(item, const_cast<char*>(threadIdUtf8.constData()));
    uds_inbox_item_data_set_name(item, titleUtf8.constData());
    uds_inbox_item_data_set_description(item, previewUtf8.constData());
    uds_inbox_item_data_set_icon(item, st.unread > 0 ? HUB_ICON_UNREAD_FILE : HUB_ICON_READ_FILE);
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
    uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID);
    uds_inbox_item_data_set_timestamp(item, st.timestampMs);
    uds_inbox_item_data_set_unread_count(item, st.unread);
    // total_count không về 0 khi đọc: đổi unread 0<->1 mà total không tăng.
    uds_inbox_item_data_set_total_count(item, qMax(st.total, qMax(st.unread, 1)));
    uds_inbox_item_data_set_context_state(item, contextStateFor(st.unread > 0));
    uds_inbox_item_data_set_notification_state(item, notify);

    int rc = uds_item_updated(m_udsHandle, item);
    if (rc != UDS_SUCCESS && allowAdd) rc = uds_item_added(m_udsHandle, item);
    uds_inbox_item_data_destroy(item);

    if (rc == UDS_SUCCESS) m_knownThreadIds.insert(threadId);
    return rc == UDS_SUCCESS;
}

void HubIntegration::removeThreadItem(const QString &threadId)
{
    if (threadId.isEmpty()) return;

    if (m_ready) {
        QByteArray threadIdUtf8 = threadId.toUtf8();
        int rc = uds_item_removed(m_udsHandle, ACCOUNT_ID, const_cast<char*>(threadIdUtf8.constData()));
        if (rc != UDS_SUCCESS) {
            // Item có thể đã mất phía Hub (account bị tạo lại lúc khởi động) —
            // vẫn phải xoá state bền để không bị dựng lại ngoài ý muốn.
            qDebug() << "[Hub] removeThreadItem: uds_item_removed rc=" << rc << "thread" << threadId;
        }
    }
    forgetState(threadId);
}

bool HubIntegration::isGroupThread(const QString &threadId) const
{
    QMap<QString, ThreadItemState>::const_iterator it = m_threadItemState.find(threadId);
    if (it == m_threadItemState.end()) {
        // Process này chưa thấy thread (vd UI vừa được Hub mở sau khi service
        // tạo item) -> đọc state bền.
        QSettings s("BerryLife", "Zalo10");
        return s.value(QString("hubItems/%1/group").arg(threadId), false).toBool();
    }
    return it.value().isGroup;
}
