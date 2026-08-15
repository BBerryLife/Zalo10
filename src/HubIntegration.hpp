#ifndef HUBINTEGRATION_HPP
#define HUBINTEGRATION_HPP

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QUrl>

// Forward declare thay vì include <bb/pim/unified/unified_data_source.h> ở
// đây — header đó là C API thuần (không phải Cascades/QObject), kéo vào
// header này sẽ leak ra mọi file include HubIntegration.hpp. uds_context_t
// là typedef void*, nên forward declare bằng void* thẳng trong class là đủ,
// #include thật nằm trong HubIntegration.cpp.

/*!
 * HubIntegration — bọc thư viện UDS (Unified Data Source) của BB10.
 *
 * Vì sao cần class riêng thay vì chỉ dùng bb::platform::Notification như
 * trước: theo header <bb/platform/Notification>, "Instant previews are
 * disabled for your app by default, unless the app has an account in the
 * BlackBerry Hub." Notification hiện tại (sendHubNotification trong
 * ZaloService_Messages.cpp) chỉ post một notification rời rạc — nó rơi vào
 * mục "Notifications" chung của Hub (ảnh chụp màn hình Hub gửi kèm), không
 * có tab riêng, và vì app chưa có "account" nào đăng ký với Hub nên preview
 * nội dung tin nhắn trên lock screen (instant preview) không được kích hoạt
 * cho app.
 *
 * TBBX/Telegram (thấy trong ảnh Hub — có icon + badge riêng ngang hàng BBM,
 * Text Messages) làm được điều này vì nó đăng ký một UDS account riêng.
 * HubIntegration làm đúng việc đó cho Zalo10:
 *   - uds_init() + uds_register_client(): mở kết nối tới Hub, đăng ký app.
 *   - uds_account_added(): tạo 1 tab "Zalo10" riêng trong Hub, icon lấy từ
 *     PreviewNoti.png (assets/public/PreviewNoti.png).
 *   - uds_item_added()/uds_item_updated(): mỗi thread (DM hoặc group) là 1
 *     inbox item, source_id = threadId, gộp tin nhắn mới vào cùng 1 dòng
 *     (update timestamp/preview/unread_count) thay vì tạo dòng mới mỗi tin
 *     nhắn — giống cách Hub hiển thị hội thoại, không phải log tin nhắn rời.
 *
 * Một khi account đã tồn tại, gọi lại bb::platform::Notification::notify()
 * (vẫn ở sendHubNotification/ZaloService_Messages.cpp, không đổi) kèm
 * setIconUrl(PreviewNoti.png) sẽ tự động có instant preview trên lock
 * screen — đó là hành vi mặc định của Hub khi app có account, không phải
 * API riêng phải gọi thêm.
 *
 * Toàn bộ hàm ở đây gọi thư viện uds đồng bộ (uds_init(..., async=false)).
 * Đây là các lệnh IPC nhẹ (không phải network call), nên chạy trên main
 * thread như các Q_INVOKABLE khác của ZaloService là chấp nhận được — cùng
 * pattern các hàm sqlite3_exec đồng bộ khác trong codebase này.
 */
class HubIntegration : public QObject
{
    Q_OBJECT
public:
    explicit HubIntegration(QObject *parent = 0);
    virtual ~HubIntegration();

    // Mở kết nối UDS + đăng ký account "Zalo10" nếu chưa có. An toàn để gọi
    // nhiều lần (no-op nếu đã init thành công). Trả về false nếu UDS không
    // khởi tạo được (ví dụ chạy trên Simulator thiếu service) — mọi hàm
    // khác trong class này tự kiểm tra m_ready và no-op êm nếu init lỗi,
    // để không bao giờ làm sendHubNotification() hiện tại crash hay bị
    // chặn bởi lỗi ở phần Hub-account (vốn là tính năng cộng thêm).
    bool init();

    // Thêm/cập nhật dòng hội thoại cho 1 thread trong tab Zalo10 của Hub.
    // Gọi mỗi khi có tin nhắn mới đến (từ sendHubNotification call site).
    //   threadId    : id hội thoại (DM uid hoặc group id) — dùng thẳng làm
    //                 uds_source_id, ổn định qua các lần gọi.
    //   isGroup     : group hay DM, chỉ ảnh hưởng icon/log, không đổi logic.
    //   title       : tên hiện ở dòng đầu item (tên nhóm, hoặc "Zalo10" cho DM).
    //   preview     : text hiện ở dòng mô tả ("Tên: nội dung tin nhắn").
    //   timestampMs : mốc thời gian UNIX ms, quyết định thứ tự trong Hub.
    void upsertThreadItem(const QString &threadId, bool isGroup,
                           const QString &title, const QString &preview,
                           qint64 timestampMs);

    // Đánh dấu đã đọc (unread_count=0) khi user mở thread — gọi từ
    // ZaloService::setActiveThread(). Không xoá item khỏi Hub, chỉ tắt badge.
    void markThreadRead(const QString &threadId);

    // Xoá hẳn 1 dòng khỏi tab Zalo10 (ví dụ khi user xoá/rời hội thoại).
    // Hiện chưa có call site bắt buộc — public để dùng khi cần, tránh phải
    // sửa lại header nếu sau này ZaloService_Contacts.cpp cần gọi.
    void removeThreadItem(const QString &threadId);

    // Đường dẫn tuyệt đối tới thư mục asset PUBLIC đã cài đặt của app trên
    // máy ("/apps/<progname>/public/assets/images/"), dùng làm pAssetPath
    // cho uds_register_client() bên trong class này, và cũng cần dùng lại
    // ở ZaloService_Messages.cpp (Notification::setIconUrl()) để 2 chỗ luôn
    // trỏ cùng 1 icon vật lý — public static để không phải copy-paste công
    // thức __progname ở 2 nơi. Xem cài đặt trong .cpp để biết vì sao dùng
    // __progname thay vì QDir::currentPath() (khuyến nghị chính thức từ
    // kinh nghiệm thực chiến UDS trên BB10, xem comment trong .cpp).
    static QString publicAssetPath();
    // Tiện ích: full file:// URI của PreviewNoti.png trong thư mục trên,
    // đúng định dạng bb::platform::Notification::setIconUrl() yêu cầu
    // ("file URI to a public asset", không phải "asset:///").
    static QUrl hubIconUrl();

private:
    Q_DISABLE_COPY(HubIntegration)

    // uds_item_updated() KHÔNG patch từng field — nó THAY THẾ TOÀN BỘ
    // record bằng đúng những gì được set trong lệnh gọi đó; field nào
    // không set sẽ bị reset về rỗng/0 (đã tận mắt thấy: markThreadRead()
    // trước đây chỉ set 5/9 field, kết quả Hub hiện tên rỗng (fallback về
    // tên account), mô tả rỗng, timestamp về epoch "Thursday, January 1,
    // 1970"). Vì vậy MỌI lần gọi uds_item_updated() phải cung cấp ĐẦY ĐỦ
    // toàn bộ field hiện tại của item, không chỉ phần muốn đổi — struct này
    // lưu lại đúng những gì cần để tái tạo đầy đủ.
    struct ThreadItemState {
        QString title;
        QString preview;
        qint64  timestampMs;
    };
    // threadId -> state đầy đủ gần nhất đã gửi cho item đó, để
    // markThreadRead() (và bất kỳ update một-phần nào khác sau này) có thể
    // tái tạo lại toàn bộ record thay vì vô tình xoá mất name/description/
    // timestamp cũ.
    QMap<QString, ThreadItemState> m_threadItemState;

    void *m_udsHandle;      // uds_context_t thật, xem HubIntegration.cpp
    bool  m_ready;          // true nếu init() + account_added() thành công
    bool  m_initAttempted;  // tránh log spam / retry init() lặp lại mỗi tin nhắn

    // threadId -> unread_count hiện tại đang hiển thị trên item đó, để
    // upsertThreadItem() cộng dồn thay vì Hub luôn nhảy về 1.
    QMap<QString, int> m_unreadCounts;
    // threadId đã từng uds_item_added() thành công — quyết định add vs update.
    QSet<QString> m_knownThreadIds;

    // Đổi từ 424242001 -> 424242002: nghi ngờ account cũ bị dính trạng thái
    // "half-broken" trong database của Hub (hệ thống, độc lập với app) do
    // rất nhiều lần add/update lộn xộn trong lúc debug (icon sai path, đổi
    // qua đổi lại notification_state...). Gỡ-cài-lại app KHÔNG xoá được
    // trạng thái này vì nó nằm ở phía Hub, không phải trong app. Đổi ID là
    // cách rẻ nhất để loại trừ khả năng này — buộc Hub tạo 1 bản ghi hoàn
    // toàn mới, sạch, thay vì update lên bản ghi cũ khả nghi.
    // Đổi tiếp 424242002 -> 424242003: đang thử account type khác (IM ->
    // TEXT_MESSAGE), đổi ID luôn để chắc chắn Hub tạo bản ghi hoàn toàn
    // mới với type mới, không phải update lên account cũ đã từng đăng ký
    // type=IM (tránh khả năng Hub giữ/cache type cũ khi chỉ update).
    // Đổi tiếp 424242003 -> 424242004: vừa trả account type từ TEXT_MESSAGE
    // về lại IM — đổi ID để Hub tạo bản ghi hoàn toàn sạch với type đúng,
    // không update lên bản ghi vừa test type khác.
    // Đổi tiếp 424242004 -> 424242005: vừa thêm item context action "Open in
    // Zalo10" (uds_register_item_context_action) — chưa có cơ chế "update"
    // action cho account cũ chưa từng đăng ký nó, đổi ID để chắc chắn có
    // bản đăng ký sạch từ đầu, kèm action mới.
    static const long long ACCOUNT_ID = 424242005LL;
};

#endif // HUBINTEGRATION_HPP
