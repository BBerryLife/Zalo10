#ifndef ApplicationUI_HPP_
#define ApplicationUI_HPP_

#include <QObject>
#include <QString>
#include <QSettings>
#include <QNetworkAccessManager>
#include <bb/system/InvokeManager>
#include <bb/cascades/ThemeSupport>

namespace bb { namespace cascades { class LocaleHandler; class AbstractPane; } }
class QTranslator;
class QNetworkReply;
class QSslError;
class ZaloService;

class ApplicationUI : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationUI();
    virtual ~ApplicationUI() {}

public slots:
    void invokeEmail(const QString &to, const QString &subject);
    void minimizeApp();
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE bool getDarkTheme();
    // Khi bật: tin nhắn thu hồi vẫn hiển thị nội dung gốc (kèm tag), thay vì
    // bị thay bằng bubble placeholder "This message was recalled".
    Q_INVOKABLE void setShowRecalledMessages(bool show);
    Q_INVOKABLE bool getShowRecalledMessages();
    Q_INVOKABLE QString appVersion();
    // Copy log của phiên hiện tại ra thư mục Documents dùng chung, mỗi lần
    // export ra 1 file riêng có timestamp. Trả về path nếu thành công, rỗng nếu lỗi.
    Q_INVOKABLE QString exportLog();

    // ---- Update check / changelog (About screen) -----------------------------
    // Cả hai đọc chung 1 file JSON manifest host trên BBerryLife.github.io.
    // checkForUpdate(): so latestVersion trong manifest với appVersion(),
    // kết quả trả về qua updateCheckResult().
    Q_INVOKABLE void checkForUpdate();
    // fetchChangelog(): lấy mảng changelog trong manifest, render ra HTML
    // sẵn để hiển thị, kết quả trả về qua changelogReady().
    Q_INVOKABLE void fetchChangelog();

    // ---- Copy & Share (ChatView bubble hold-menu) -----------------------------
    // Copy: ghi thẳng vào clipboard hệ điều hành.
    Q_INVOKABLE void copyToClipboard(const QString &text);
    // Copy ảnh thật (bytes) vào clipboard dưới MIME type image/*, đọc từ file
    // local, để paste sang app khác ra đúng tấm ảnh thay vì text JSON/URL.
    // localPath có thể là path thường hoặc URI "file://". Trả về true nếu ok.
    Q_INVOKABLE bool copyImageToClipboard(const QString &localPath);
    // Share: query các app đăng ký bb.action.SHARE + text/plain trên máy.
    // Kết quả trả về async qua shareTargetsReady(); QML hiện picker sheet
    // rồi gọi invokeShareTarget() với target user chọn.
    Q_INVOKABLE void queryShareTargets(const QString &text);
    Q_INVOKABLE void invokeShareTarget(const QString &target, const QString &action, const QString &text);
    // Bản dành cho ảnh của 2 hàm trên: query target đăng ký image/* thay vì
    // text/plain, và gửi kèm file ảnh thay vì text. Dùng khi share 1 bubble ảnh.
    Q_INVOKABLE void queryShareTargetsForImage(const QString &localPath);
    Q_INVOKABLE void invokeShareTargetForImage(const QString &target, const QString &action, const QString &localPath);
    // Mở 1 file local bằng app ngoài phù hợp (video player, v.v.) qua
    // bb.action.OPEN với target rỗng — hệ thống tự chọn app theo MIME type
    // suy ra từ đuôi file. Dùng cho tap-to-play video bubble trong ChatView.
    // localPath có thể là path thường hoặc URI "file://".
    Q_INVOKABLE void openLocalFile(const QString &localPath);

    // ---- Calendar event creation -----------------------------------------
    // Tạo event thật trong calendar mặc định của máy qua CalendarService.
    // Luôn tạo cho HÔM NAY — startTime/endTime tự tính từ giờ hiện tại,
    // không nhận từ tham số, để tránh tạo nhầm ngày. subject/body từ QML,
    // durationMinutes quyết định độ dài event. Kết quả trả về async qua
    // eventCreated().
    Q_INVOKABLE void createTodayEvent(const QString &subject, const QString &body, int durationMinutes);

signals:
    void openThreadRequested(const QString &threadId, bool isGroup);
    // Bắn khi setShowRecalledMessages() thay đổi, để ChatView đang mở
    // re-render lại bubble ngay mà không cần rời/mở lại thread.
    void showRecalledMessagesChanged(bool show);
    // isLatest: true nếu appVersion() đã >= latestVersion.
    // error chỉ khác rỗng khi network/parse lỗi.
    void updateCheckResult(bool isLatest, const QString &latestVersion, const QString &downloadUrl, const QString &error);
    // html: sẵn sàng đưa vào WebView::loadHtml(). error khác rỗng nếu lỗi.
    void changelogReady(const QString &html, const QString &error);
    // Kết quả của queryShareTargets() — list {label, target, action, icon, isNative}
    void shareTargetsReady(const QVariantList &targets);
    // Kết quả của createTodayEvent(): success true/false, error message (rỗng nếu ok)
    void eventCreated(bool success, const QString &error);

private slots:
    void onSystemLanguageChanged();
    void onManualExit();
    // Đọc pipe từ signal handler SIGTERM (main.cpp) rồi gọi onManualExit().
    void onTermSignal(int fd);
    void onInvoked(const bb::system::InvokeRequest &request);
    void onUpdateCheckFetchDone();
    void onChangelogFetchDone();
    void onManifestSslErrors(const QList<QSslError> &errors);
    void onQueryTargetsFinished();

private:
    QString buildChangelogHtml(const QVariantList &versions) const;

    QTranslator*                  m_pTranslator;
    bb::cascades::LocaleHandler*  m_pLocaleHandler;
    bb::system::InvokeManager*    m_pInvokeManager;
    ZaloService*                  m_zService;
    QNetworkAccessManager*        m_updateManager;
    bool                          m_exitHandled; // chống chạy onManualExit() 2 lần nếu cả manualExit/aboutToQuit/SIGTERM đều bắn
    QString                       m_pendingShareMimeType; // "text/plain" or "image/*" — set right before queryTargets(), read in onQueryTargetsFinished()
};

#endif
