#include "HubIntegration.hpp"

#include <bb/pim/unified/unified_data_source.h>

#include <QDebug>
#include <QByteArray>
#include <QLatin1String>
#include <QDir>
#include <QStringList>

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

// ===== LỊCH SỬ ĐIỀU TRA BUG "SINGLE-TAP KHÔNG MỞ APP" — 3 lần test log
// thật, xem đầy đủ trong /areas/zalo10.md nếu cần tra lại =====
//
// Bài học cốt lõi: "target" cho LONG-PRESS (item context action) và
// "target_name" cho SINGLE-TAP (account) là 2 CƠ CHẾ HOÀN TOÀN KHÁC NHAU
// của Invocation Framework:
//
//   - Long-press ("Open in Zalo10") đi qua uds_register_item_context_action()
//     -> Hub soạn InvokeRequest có action="bb.action.OPEN" + mimeType,
//     RỒI tìm <invoke-target> nào trong bar-descriptor.xml có <filter>
//     khớp action+mimeType đó. Item action's "target" = HUB_INVOKE_TARGET
//     ("com.BerryLife.Zalo10.invoke"), id của 1 <invoke-target> có filter
//     khai báo sẵn. ĐÃ XÁC NHẬN hoạt động đúng qua CẢ 3 LẦN test log thật:
//     "onInvoked() ENTERED", action: "bb.action.OPEN" target:
//     "com.BerryLife.Zalo10.invoke" — long-press luôn mở đúng thread,
//     không đổi qua các lần sửa khác nhau.
//
//   - Single-tap: 3 lần test, 2 giá trị target_name khác nhau
//     (HUB_INVOKE_TARGET rồi HUB_APP_ID = "com.BerryLife.Zalo10", app ID
//     gốc), CẢ 2 LẦN ĐỀU CHO KẾT QUẢ GIỐNG HỆT NHAU: short-tap hoàn toàn
//     im lặng, không 1 dòng log nào (kể cả "onInvoked() ENTERED"), suốt
//     nhiều lần tap liên tục trong log. Kết luận: target_name KHÔNG PHẢI
//     biến số quyết định ở đây — đổi giá trị của nó không tạo khác biệt
//     quan sát được nào cả. Giữ HUB_APP_ID cho target_name (đúng theo
//     txtmpp source thật đã đối chiếu — xem git history) vì không có bằng
//     chứng nó SAI, chỉ là chưa đủ để tap hoạt động.
//
//     Người dùng xác nhận: có ít nhất 1 app đối thủ khác (closed-source,
//     không có access) làm được single-tap-to-open bình thường trên chính
//     nền tảng BB10 này — LOẠI TRỪ hoàn toàn giả thuyết "giới hạn nền
//     tảng, third-party app không hỗ trợ được single-tap". Chắc chắn có 1
//     tổ hợp UDS config đúng nào đó chưa tìm ra, không phải giới hạn OS.
//
//     Giả thuyết đang thử (CHƯA XÁC NHẬN, lần thứ 4): mime type. Toàn bộ
//     code trước đây dùng "text/plain" xuyên suốt (item, item action, bar-
//     descriptor filter) — nhưng đọc lại kỹ đoạn code MẪU CHÍNH THỨC ngay
//     trong unified_data_source.h (comment @code ở đầu file, ví dụ
//     uds_item_action_data_set_mime_type/uds_inbox_item_data_set_mime_type)
//     dùng "plain/message", KHÔNG PHẢI "text/plain". Nghi ngờ "plain/
//     message" là mime type QUY ƯỚC RIÊNG của BlackBerry Hub để đánh dấu
//     "đây là 1 tin nhắn/message item" — "text/plain" là mime type chuẩn
//     IANA chung chung, pass validation lúc register (UDS không validate
//     theo whitelist) nhưng có thể KHÔNG được Hub coi là loại nội dung
//     "message" hợp lệ khi Hub TỰ soạn InvokeRequest lúc single-tap dựa
//     theo mime_type của item — trong khi long-press vẫn hoạt động vì nó
//     đi qua action "bb.action.OPEN" tường minh do CHÍNH APP đăng ký sẵn
//     (không phụ thuộc Hub tự suy luận loại nội dung từ mime_type).
//     Đã đổi: item's mime_type (cả 2 nơi: upsertThreadItem + markAsRead)
//     sang HUB_MIME_TYPE_MESSAGE = "plain/message". Bar-descriptor.xml đã
//     thêm filter mới cho "plain/message" song song (không xoá filter
//     "text/plain" cũ, để không phá long-press). Item action's mime_type
//     GIỮ NGUYÊN "text/plain" (không đổi — long-press đang hoạt động,
//     không có lý do đổi phần đang chạy đúng).
static const char *HUB_INVOKE_TARGET = "com.BerryLife.Zalo10.invoke";
static const char *HUB_APP_ID = "com.BerryLife.Zalo10";
static const char *HUB_MIME_TYPE_MESSAGE = "plain/message";

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
// 1 thư mục "/apps/Zalo10.so/public/hubicons/" không hề tồn tại, trong khi
// file thật nằm ở "/apps/<app-id-that>/public/hubicons/").
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
    // "hubicons" phải khớp CHÍNH XÁC với dest trong bar-descriptor.xml:
    // <asset path="hubicons" public="true">hubicons</asset>
    // Tên KHÔNG được bắt đầu bằng chữ "assets" — Momentics (NDK 10.3.1) có
    // vẻ chặn theo tiền tố tên chuỗi trùng với rule "assets" đã khai báo
    // (từng thử "assets-public" dù là thư mục top-level ngang hàng thật sự,
    // vẫn bị chặn) — xem comment dài trong bar-descriptor.xml.
    return QString("/apps/%1/public/hubicons/").arg(appIdFromHomePath());
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
    // nhưng file 72x72 hiện tại (PreviewNoti/Read/UNRead.png) đã hiển thị
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
    // Fix bug "single-tap không mở app" — lần sửa thứ 2, xem giải thích đầy
    // đủ + bằng chứng thực nghiệm (đối chiếu source code thật của txtmpp,
    // log runtime 2 lần test) ở khai báo HUB_APP_ID/HUB_INVOKE_TARGET đầu
    // file. Khác với item action's target (dùng HUB_INVOKE_TARGET, id của
    // 1 <invoke-target> có filter), target_name cấp account này phải là
    // <id> app THẬT (HUB_APP_ID) — 2 cơ chế khác nhau, không dùng chung.
    uds_account_data_set_target_name(account, HUB_APP_ID);
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

    // ===== FIX LẦN 7 CHO BUG "SINGLE-TAP KHÔNG MỞ APP" (CHƯA TEST) =====
    // Phát hiện quan trọng sau 6 lần thử thất bại (đổi target_name, mime_type,
    // placement — xem lịch sử đầy đủ ở khai báo HUB_MIME_TYPE_MESSAGE): đọc
    // lại kỹ phần tổng quan quy trình khởi tạo UDS chính thức trong
    // unified_data_source.h (đoạn mô tả "Synchronous mode" / "Asynchronous
    // mode" ở đầu file) — thứ tự BẮT BUỘC là:
    //   1. uds_init() -> 2. uds_register_client() -> 3. uds_account_added()
    //   -> 4. uds_category_added() -> 5. uds_item_added()
    // category_added() nằm GIỮA account và item — không phải bước tuỳ chọn.
    // Code trước đây BỎ QUA HOÀN TOÀN bước này: mọi item chỉ gán
    // uds_inbox_item_data_set_category_id(item, HUB_CATEGORY_ID=1) — tức
    // GÁN 1 CON SỐ trỏ tới 1 category CHƯA TỪNG ĐƯỢC TẠO qua
    // uds_category_added(). Nghi ngờ: Hub vẫn hiển thị được item vào danh
    // sách (khoan dung khi render), nhưng vì category_id không trỏ tới 1
    // category hợp lệ nào, Hub không có đủ metadata để coi item này là
    // "actionable" khi tap — khớp đúng triệu chứng đã quan sát (Hub UI
    // không phản ứng gì cả, không chỉ riêng app không mở). Long-press vẫn
    // hoạt động vì nó dùng registry item-action cấp ACCOUNT (không phụ
    // thuộc category) qua uds_register_item_context_action(), đây là 2 cơ
    // chế Hub tra cứu khác nhau.
    //
    // Đăng ký category id=HUB_CATEGORY_ID cho account này TRƯỚC khi có bất
    // kỳ item nào dùng category_id đó. Chạy 1 lần trong init(), giống hệt
    // thứ tự account_added() bên trên.
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

    // Đăng ký "Open in Zalo10" — item context action, tái tạo đúng dòng
    // "Open in TBBX" thấy trong menu long-press của TBBX (ảnh so sánh).
    //
    // Nguyên nhân thật của vụ short-tap không mở được: XEM comment đầy đủ ở
    // khai báo HUB_MIME_TYPE_MESSAGE đầu file — 4 lần test log thật, đã
    // loại trừ target_name (2 giá trị khác nhau, kết quả giống hệt) và
    // mime_type (đổi "text/plain" -> "plain/message", vẫn im lặng). Xác
    // nhận thêm qua hỏi trực tiếp: khi short-tap, HUB UI không phản ứng
    // GÌ CẢ (không phải chỉ app Zalo10 không mở — cả màn hình Hub cũng
    // đứng yên tuyệt đối). Điều này gợi ý Hub không hề coi item này là
    // "actionable" khi tap, tức là thiếu 1 cấu hình khiến Hub đăng ký
    // item ở dạng "chỉ xem", không có action mặc định gắn liền.
    //
    // Giả thuyết đang thử (lần 5, CHƯA XÁC NHẬN): UDS_PLACEMENT_DEFAULT
    // (bên dưới, trước đây dùng) đọc kỹ lại doc mới thấy: "the action
    // should be placed in its default location, which is TYPICALLY IN THE
    // OVERFLOW MENU" — nghĩa đen chỉ là "vào menu overflow của context
    // menu", KHÔNG liên quan gì đến việc đây có phải action mặc định khi
    // tap hay không, dù tên gọi "DEFAULT" dễ gây hiểu lầm (lần trước đã
    // hiểu lầm chính chỗ này, tưởng đã "loại trừ" placement nhưng thực ra
    // chưa từng thử FIXED). UDS_PLACEMENT_FIXED mô tả: "the action
    // placement is fixed (for example, a delete or archive action)" — tức
    // dành cho hành động CÓ VỊ TRÍ ĐẶC BIỆT/CỐ ĐỊNH trong context menu
    // (khác hẳn overflow chung chung), giống cách "Delete" luôn có vị trí
    // riêng không lẫn vào menu tràn. Nghi ngờ: action "Open" (mở item)
    // cũng thuộc nhóm hành động đặc biệt này — cần placement FIXED để Hub
    // nhận diện nó là action CHÍNH của item (và do đó áp dụng khi tap),
    // không phải 1 trong nhiều action chung chung nằm trong overflow.
    //
    // Đăng ký 1 lần ở cấp account (áp dụng cho MỌI item của account này),
    // không phải per-item.
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
    uds_item_action_data_set_mime_type(openAction, "text/plain");
    // FIXED thay vì DEFAULT — xem giải thích đầy đủ ở comment trên. Thay
    // đổi CHƯA TEST, giả thuyết lần 5 cho bug single-tap.
    uds_item_action_data_set_placement(openAction, UDS_PLACEMENT_FIXED);
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
    // "plain/message" (không phải "text/plain"): đúng mime type dùng trong
    // mẫu code chính thức của unified_data_source.h cho inbox item — xem
    // giải thích đầy đủ ở khai báo HUB_MIME_TYPE_MESSAGE đầu file. Đây là
    // fix thử nghiệm CHƯA XÁC NHẬN cho bug "single-tap không mở app".
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
    uds_inbox_item_data_set_mime_type(item, HUB_MIME_TYPE_MESSAGE);
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
